//
//  piano-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano's pure logic: configuration, status compaction, backoff and the displayed text.
//  The Orpheus replies below have the shapes of orpheus/playback.py get_state(),
//  orpheus/synth_output.py status() and orpheus/db.py list_songs().
//

import { test } from "node:test";
import assert from "node:assert/strict";
import {
    parseConfig, withStatus, statusOf, compactStatus, pollDelay, Backoff, formatDuration, formatStatus, formatSongs,
    parseDisplayURL, displayGeometry, parseDisplayEvent,
    DEFAULT_VOLUME, ACTIVE_POLL_MS, IDLE_POLL_MS
} from "../piano-core.mjs";

const BASE = "http://orpheus.example:8080";

function playback(fields = {}) {
    return {
        is_playing: false, is_paused: false, status: "idle", connect_attempt: 0, connect_attempts_max: 6,
        title: null, playback_id: null, output: null, progress: 0.0, last_error: null, last_played: null, ...fields
    };
}

const SYNTH = {
    enabled: true, reason: null, soundfont: "/srv/sf2/piano.sf2", sample_rate: 48000, polyphony: 128,
    active_voices: 3, listeners: 1, render_resyncs: 0,
    stream: { path: "/api/synth/stream", content_type: "audio/ogg", codec: "opus" }
};

test("parseConfig takes orpheusBase and volume from userData.piano", () => {
    assert.deepEqual(parseConfig(JSON.stringify({ piano: { orpheusBase: BASE + "/", volume: 0.5 } })),
        { orpheusBase: BASE, volume: 0.5 });
    assert.deepEqual(parseConfig(JSON.stringify({ piano: { orpheusBase: "https://[::1]:8443/orpheus" } })),
        { orpheusBase: "https://[::1]:8443/orpheus", volume: DEFAULT_VOLUME });
});

test("parseConfig has no default service and refuses anything but an http(s) base", () => {
    for (const userData of ["", "not json", "{}", JSON.stringify({ piano: {} }), JSON.stringify({ piano: "x" })]) {
        assert.ok("error" in parseConfig(userData), userData);
    }
    for (const base of ["ftp://orpheus.example", "orpheus.example:8080", "http://<host>:<port>", "http://h/?q=1",
        "http://h/#x", "javascript:alert(1)", "", 42]) {
        assert.ok("error" in parseConfig(JSON.stringify({ piano: { orpheusBase: base } })), String(base));
    }
});

test("parseConfig falls back to the default volume outside 0 to 1", () => {
    for (const volume of [-0.1, 1.5, "0.5", null]) {
        const config = parseConfig(JSON.stringify({ piano: { orpheusBase: BASE, volume } }));
        assert.equal("volume" in config && config.volume, DEFAULT_VOLUME);
    }
});

test("withStatus replaces piano.status and keeps everything else", () => {
    const before = JSON.stringify({ owner: "kept", piano: { orpheusBase: BASE, volume: 0.3, status: { state: "idle" } } });
    const status = compactStatus(playback(), SYNTH);
    const after = JSON.parse(withStatus(before, status));
    assert.deepEqual(after, { owner: "kept", piano: { orpheusBase: BASE, volume: 0.3, status } });
    assert.deepEqual(JSON.parse(withStatus("", status)), { piano: { status } });
    assert.deepEqual(statusOf(withStatus(before, status)), status);
    assert.equal(statusOf(JSON.stringify({ piano: { orpheusBase: BASE } })), null);
    assert.equal(statusOf("garbage"), null);
});

test("compactStatus: idle, playing, paused and the start-up phases", () => {
    assert.deepEqual(compactStatus(playback(), SYNTH), {
        state: "idle", title: null, output: null, progress: null, elapsed: null, synth: true, error: null
    });
    const playing = playback({ is_playing: true, status: "playing", title: "Gymnopedie No. 1", playback_id: 7,
        output: "synth", progress: 0.4567 });
    assert.deepEqual(compactStatus(playing, SYNTH), {
        state: "playing", title: "Gymnopedie No. 1", output: "synth", progress: 46, elapsed: null, synth: true, error: null
    });
    assert.equal(compactStatus({ ...playing, is_paused: true }, SYNTH).state, "paused");
    assert.equal(compactStatus({ ...playing, status: "connecting" }, SYNTH).state, "connecting");
    assert.equal(compactStatus({ ...playing, status: "starting" }, SYNTH).state, "starting");
    assert.equal(compactStatus({ ...playing, elapsed_seconds: 12.9 }, SYNTH).elapsed, 12);
});

test("compactStatus: errors, a failed synth request and an unreachable Orpheus", () => {
    const failed = playback({ last_error: { title: "Song", message: "MIDI port gone", seconds_ago: 3.2 } });
    assert.equal(compactStatus(failed, SYNTH).error, "MIDI port gone");
    assert.equal(compactStatus(playback(), null).synth, false);
    assert.equal(compactStatus(playback(), { ...SYNTH, enabled: false }).synth, false);
    assert.deepEqual(compactStatus(null, null, "Error: HTTP 401"), {
        state: "offline", title: null, output: null, progress: null, elapsed: null, synth: false, error: "Error: HTTP 401"
    });
    // Polled replies that differ only in what is not displayed compact to the same status
    assert.deepEqual(compactStatus(failed, { ...SYNTH, active_voices: 9 }),
        compactStatus({ ...failed, last_error: { ...failed.last_error, seconds_ago: 8.0 } }, SYNTH));
});

test("pollDelay is fast only while something is active", () => {
    assert.equal(pollDelay(null), IDLE_POLL_MS);
    assert.equal(pollDelay(compactStatus(null, null)), IDLE_POLL_MS);
    assert.equal(pollDelay(compactStatus(playback(), SYNTH)), IDLE_POLL_MS);
    for (const status of ["playing", "starting", "connecting"]) {
        assert.equal(pollDelay(compactStatus(playback({ is_playing: true, status }), SYNTH)), ACTIVE_POLL_MS);
    }
    assert.equal(pollDelay(compactStatus(playback({ is_playing: true, is_paused: true }), SYNTH)), ACTIVE_POLL_MS);
});

test("Backoff doubles from 1 s to a 30 s cap and resets after a minute connected", () => {
    const backoff = new Backoff();
    assert.deepEqual([0, 0, 0, 0, 0, 0, 0, 0].map(ms => backoff.next(ms)),
        [1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000]);
    assert.equal(backoff.next(60_000), 30000, "exactly a minute is not long enough");
    assert.equal(backoff.next(60_001), 1000);
    assert.equal(backoff.next(500), 2000);
});

test("formatStatus shows the title and what is known about the piece", () => {
    assert.equal(formatStatus(null), "Piano\nwaiting for Orpheus");
    assert.equal(formatStatus(compactStatus(null, null, "Error: HTTP 503")), "Piano\noffline: Error: HTTP 503");
    assert.equal(formatStatus(compactStatus(playback(), SYNTH)), "Piano\nidle");
    assert.equal(formatStatus(compactStatus(playback({ last_error: { message: "boom" } }), SYNTH)), "Piano\nidle\nlast error: boom");
    const playing = playback({ is_playing: true, status: "playing", title: "Ode to Joy", output: "synth", progress: 0.25 });
    assert.equal(formatStatus(compactStatus({ ...playing, elapsed_seconds: 75 }, SYNTH)), "Ode to Joy\nplaying  1:15  25%");
    assert.equal(formatStatus(compactStatus({ ...playing, output: "piano", title: null, is_paused: true }, SYNTH)),
        "Untitled\npaused  25%  on the real piano");
});

test("formatSongs lists title, composer and duration, and says how many are left out", () => {
    const songs = [
        { id: 2, title: "Clair de Lune", composer: "Debussy", genre: "classical", duration_seconds: 302.4, created_at: "2026-01-01" },
        { id: 1, title: "Untitled sketch", composer: null, genre: null, duration_seconds: null, created_at: "2026-01-02" }
    ];
    assert.equal(formatSongs(songs), "Songs (2)\nClair de Lune - Debussy (5:02)\nUntitled sketch");
    assert.equal(formatSongs(songs, 1), "Songs (2)\nClair de Lune - Debussy (5:02)\n+1 more");
    assert.equal(formatSongs([]), "Songs\n(none yet)");
    assert.equal(formatSongs(null), "Songs\n(Orpheus did not answer)");
    assert.equal(formatDuration(59.9), "0:59");
    assert.equal(formatDuration(3600), "60:00");
});

test("parseDisplayURL: an http(s) page with no default and no placeholders", () => {
    const url = "https://assets.example:8443/piano/piano-display.html?v=2";
    assert.deepEqual(parseDisplayURL(JSON.stringify({ piano: { displayURL: url } })), { displayURL: url });
    for (const userData of ["", JSON.stringify({ piano: {} }), JSON.stringify({ piano: { displayURL: "ftp://x/y" } }),
        JSON.stringify({ piano: { displayURL: "https://<asset-host>/piano/piano-display.html" } })]) {
        assert.match(/** @type {any} */ (parseDisplayURL(userData)).error, /piano\.displayURL must be/, userData);
    }
});

test("displayGeometry: the 0.30 x 0.17 m desk glass at 720 page pixels, at any piano scale", () => {
    for (const width of [1.48, 0.74, undefined]) {
        const { dimensions, dpi } = displayGeometry(width === undefined ? undefined : { x: width });
        const scale = width === undefined ? 1 : width / 1.48;
        assert.ok(Math.abs(dimensions.x - 0.30 * scale) < 1e-9 && Math.abs(dimensions.y - 0.17 * scale) < 1e-9);
        assert.equal(Math.round(dimensions.x * 39.3701 * dpi), 720);
        assert.equal(Math.round(dimensions.y * 39.3701 * dpi), 408);
    }
});

test("parseDisplayEvent: the page's messages, exactly as piano-display.html sends them", () => {
    // Captured from the page in headless Chrome with a stub EventBridge, every button clicked
    const sent = ["previous", "pause", "stop", "next", "replay"].map(action => `{"type":"piano-display","action":"${action}"}`);
    for (const message of sent) {
        assert.deepEqual(parseDisplayEvent(message), { action: JSON.parse(message).action, songId: null });
    }
    assert.deepEqual(parseDisplayEvent('{"type":"piano-display","action":"play-song","songId":3}'), { action: "play-song", songId: 3 });
    for (const message of ["", "rubbish", '{"type":"other","action":"stop"}', '{"type":"piano-display","action":"delete"}',
        '{"type":"piano-display","action":"play-song"}', '{"type":"piano-display","action":"play-song","songId":"3"}']) {
        assert.equal(parseDisplayEvent(message), null, message);
    }
});
