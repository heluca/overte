//
//  piano-screen-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The screens' logic: the song list in pages, the page's events, the messages results and
//  refusals leave, the transport buttons' looks, both views, and where the picker floats.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import {
    parseSongs, songPage, parseScreenEvent, resultNotice, waitingNotice, transportButtons, pickerView, deskView,
    withView, pickerPlacement, NOTICE_MS, PICKER_PAGE_SIZE, DESK_PAGE_SIZE, TRANSPORT_ACTIONS
} from "../piano-screen-core.mjs";

const NOW = 1_000_000;
/** @param {number} n */
const songs = n => Array.from({ length: n }, (_, i) => ({ id: i + 1, title: "Song " + (i + 1), composer: null, duration: 60 + i }));
const STATUS = {
    state: "playing", title: "Gymnopedie No. 1", output: "synth", progress: 46, elapsed: null, duration: 180,
    queue: ["Clair de Lune", "Prelude in C"], queued: 2, synth: true, error: null
};

test("parseSongs reads the picker's list from showSongs and drops what it cannot show", () => {
    const json = JSON.stringify([
        { id: 1, title: "Gymnopedie No. 1", composer: "Erik Satie", duration: 180 },
        { id: 2, title: "Ode to Joy", composer: null, duration: null },
        { id: 0, title: "zero" }, { id: "3", title: "text id" }, { id: 4 }, null
    ]);
    assert.deepEqual(parseSongs(json), [
        { id: 1, title: "Gymnopedie No. 1", composer: "Erik Satie", duration: 180 },
        { id: 2, title: "Ode to Joy", composer: null, duration: null }
    ]);
    for (const bad of ["null", "rubbish", "{}", undefined]) {
        assert.equal(parseSongs(bad), null, String(bad));
    }
    assert.deepEqual(parseSongs("[]"), []);
});

test("songPage: pages of a fixed size, the page clamped into range", () => {
    const list = songs(12);
    assert.deepEqual(songPage(list, 0, 5).items.map(song => song.id), [1, 2, 3, 4, 5]);
    assert.deepEqual(songPage(list, 2, 5), {
        items: [{ id: 11, title: "Song 11", composer: null, length: "1:10" }, { id: 12, title: "Song 12", composer: null, length: "1:11" }],
        page: 2, pages: 3, total: 12
    });
    assert.equal(songPage(list, 9, 5).page, 2, "past the end: the last page");
    assert.equal(songPage(list, -1, 5).page, 0);
    assert.equal(songPage(list, 1.5, 5).page, 0);
    assert.deepEqual(songPage([], 3, 5), { items: [], page: 0, pages: 1, total: 0 });
    assert.equal(songPage(songs(10), 0, 5).pages, 2, "exactly two pages");
    assert.equal(songPage([{ id: 1, title: "x", composer: null, duration: null }], 0, 5).items[0].length, null);
});

test("parseScreenEvent: the page's buttons, exactly as piano-display.html sends them", () => {
    const sent = TRANSPORT_ACTIONS.map(action => `{"action":"${action}","type":"piano-display"}`);
    for (const message of sent) {
        assert.deepEqual(parseScreenEvent(message), { action: JSON.parse(message).action });
    }
    assert.deepEqual(parseScreenEvent('{"action":"ready","type":"piano-display"}'), { action: "ready" });
    assert.deepEqual(parseScreenEvent('{"action":"close","type":"piano-display"}'), { action: "close" });
    assert.deepEqual(parseScreenEvent('{"action":"request","songId":3,"type":"piano-display"}'), { action: "request", songId: 3 });
    assert.deepEqual(parseScreenEvent('{"action":"page","page":2,"type":"piano-display"}'), { action: "page", page: 2 });
    assert.deepEqual(parseScreenEvent('{"action":"tab","tab":"songs","type":"piano-display"}'), { action: "tab", tab: "songs" });
    for (const message of ["", "rubbish", "null", '{"type":"other","action":"stop"}', '{"type":"piano-display","action":"delete"}',
        '{"type":"piano-display","action":"request"}', '{"type":"piano-display","action":"request","songId":"3"}',
        '{"type":"piano-display","action":"request","songId":0}', '{"type":"piano-display","action":"page","page":-1}',
        '{"type":"piano-display","action":"tab","tab":"admin"}', '{"type":"piano-display","action":"select","songId":3}']) {
        assert.equal(parseScreenEvent(message), null, message);
    }
});

test("resultNotice: a request's place in the queue, the transport done, a refusal with its reason", () => {
    const until = NOW + NOTICE_MS;
    assert.deepEqual(resultNotice({ action: "request", ok: true, songId: 1, title: "Gymnopedie No. 1", position: 1 }, NOW),
        { text: "Gymnopedie No. 1: plays next", tone: "ok", until });
    assert.equal(resultNotice({ action: "request", ok: true, songId: 1, title: "Gymnopedie No. 1", position: 0 }, NOW).text,
        "Gymnopedie No. 1: playing now");
    assert.equal(resultNotice({ action: "request", ok: true, songId: 7, position: 3 }, NOW).text, "Song 7: number 3 in the queue");
    assert.equal(resultNotice({ action: "select", ok: true, songId: 1, title: "Ode" }, NOW).text, "Playing now: Ode");
    assert.equal(resultNotice({ action: "pause", ok: true }, NOW).text, "Paused");
    assert.equal(resultNotice({ action: "stop", ok: true }, NOW).text, "Stopped, the queue cleared");
    assert.equal(resultNotice({ action: "next", ok: true }, NOW).text, "Skipped to the next piece");
});

test("resultNotice: the refusals visitors meet, each naming what was refused", () => {
    const refused = [
        [{ action: "request", ok: false, songId: 1, title: "Gymnopedie No. 1", reason: "One request every 10 s; try again in 7 s" },
            "Gymnopedie No. 1: One request every 10 s; try again in 7 s"],
        [{ action: "request", ok: false, songId: 1, reason: "You already have a song waiting; it must start before you request another" },
            "Song 1: You already have a song waiting; it must start before you request another"],
        [{ action: "request", ok: false, songId: 2, title: "Ode", reason: "5 requests from this piano are already waiting (the cap is 5); try again when one has started" },
            "Ode: 5 requests from this piano are already waiting (the cap is 5); try again when one has started"],
        [{ action: "pause", ok: false, reason: "Too many controls: at most 5 every 30 s; try again in 29 s" },
            "Pause: Too many controls: at most 5 every 30 s; try again in 29 s"],
        [{ action: "next", ok: false, reason: "Nothing is queued to skip to" }, "Next: Nothing is queued to skip to"],
        [{ action: "play", ok: false }, "Play: refused"]
    ];
    for (const [result, text] of refused) {
        assert.deepEqual(resultNotice(/** @type {any} */ (result), NOW), { text, tone: "refused", until: NOW + NOTICE_MS });
    }
});

test("waitingNotice: shown from the click until the answer", () => {
    assert.deepEqual(waitingNotice("Asking for Ode...", NOW), { text: "Asking for Ode...", tone: "info", until: NOW + NOTICE_MS });
});

test("transportButtons: play lit while playing, pause lit while paused, dim what would be refused", () => {
    const looks = (/** @type {any} */ status) => Object.fromEntries(Object.entries(transportButtons(status))
        .map(([name, look]) => [name, (look.lit ? "lit" : "") + (look.dim ? "dim" : "")]));
    assert.deepEqual(looks(STATUS), { previous: "", play: "lit", pause: "", stop: "", next: "", replay: "" });
    assert.deepEqual(looks({ ...STATUS, state: "paused" }), { previous: "", play: "", pause: "lit", stop: "", next: "", replay: "" });
    assert.deepEqual(looks({ ...STATUS, state: "starting", queued: 0 }),
        { previous: "", play: "lit", pause: "", stop: "", next: "dim", replay: "" });
    assert.deepEqual(looks({ ...STATUS, state: "idle", title: null, queue: [], queued: 0 }),
        { previous: "", play: "", pause: "dim", stop: "dim", next: "dim", replay: "" });
    for (const status of [null, { ...STATUS, state: "offline" }]) {
        assert.ok(Object.values(transportButtons(/** @type {any} */ (status))).every(look => look.dim && !look.lit));
    }
});

test("pickerView: loading, Orpheus silent, a page of songs, and the notice while it is fresh", () => {
    const notice = { text: "Ode: plays next", tone: /** @type {const} */ ("ok"), until: NOW + 100 };
    assert.deepEqual(pickerView({ songs: undefined, page: 0, notice: null }, NOW),
        { type: "piano-view", kind: "picker", songs: { state: "loading" }, notice: null });
    assert.equal(pickerView({ songs: null, page: 0, notice: null }, NOW).songs.state, "error");
    const view = pickerView({ songs: songs(12), page: 1, notice }, NOW);
    assert.equal(view.songs.state, "ready");
    assert.deepEqual(view.notice, { text: "Ode: plays next", tone: "ok" });
    assert.equal(/** @type {any} */ (view.songs).items.length, PICKER_PAGE_SIZE);
    assert.equal(/** @type {any} */ (view.songs).items[0].id, PICKER_PAGE_SIZE + 1);
    assert.equal(pickerView({ songs: songs(1), page: 0, notice }, NOW + 100).notice, null, "an old notice is gone");
});

test("deskView: the title, progress, times and queue from the status", () => {
    const view = deskView(STATUS, { tab: "queue", songs: undefined, page: 0, notice: null }, NOW);
    assert.equal(view.kind, "desk");
    assert.equal(view.state, "playing");
    assert.equal(view.title, "Gymnopedie No. 1");
    assert.equal(view.progress, 46);
    assert.equal(view.elapsed, "1:23", "46% of 3:00, since Orpheus counts events, not time");
    assert.equal(view.duration, "3:00");
    assert.deepEqual(view.queue, { items: ["Clair de Lune", "Prelude in C"], more: 0 });
    assert.equal(view.songs, null, "the song list only on its tab");
    assert.equal(view.buttons.play.lit, true);
    assert.equal(deskView({ ...STATUS, elapsed: 12 }, { tab: "queue", songs: undefined, page: 0, notice: null }, NOW).elapsed, "0:12");
    assert.deepEqual(deskView({ ...STATUS, queued: 8, queue: ["a", "b", "c", "d", "e"] },
        { tab: "queue", songs: undefined, page: 0, notice: null }, NOW).queue, { items: ["a", "b", "c", "d", "e"], more: 3 });
});

test("deskView: idle, offline, waiting, the real piano, and the songs tab", () => {
    const desk = { tab: /** @type {const} */ ("queue"), songs: undefined, page: 0, notice: null };
    const idle = deskView({ ...STATUS, state: "idle", title: null, progress: null, duration: null, queue: [], queued: 0 }, desk, NOW);
    assert.deepEqual([idle.title, idle.progress, idle.elapsed, idle.duration], ["Nothing playing", null, null, null]);
    const offline = deskView({ ...STATUS, state: "offline", title: null, error: "Error: HTTP 401" }, desk, NOW);
    assert.deepEqual([offline.title, offline.detail, offline.progress], ["Orpheus is offline", "Error: HTTP 401", null]);
    const waiting = deskView(null, desk, NOW);
    assert.deepEqual([waiting.state, waiting.title, waiting.queue.items], ["waiting", "Waiting for Orpheus", []]);
    assert.equal(deskView({ ...STATUS, output: "piano" }, desk, NOW).detail, "on the real piano");
    const tab = deskView(STATUS, { tab: "songs", songs: songs(7), page: 1, notice: null }, NOW);
    assert.deepEqual(tab.songs && "items" in tab.songs && tab.songs.items.map(song => song.id), [6, 7]);
    assert.equal(tab.songs && "pages" in tab.songs && tab.songs.pages, Math.ceil(7 / DESK_PAGE_SIZE));
});

test("withView adds the view to the query, before any fragment", () => {
    assert.equal(withView("https://h/p/piano-display.html", "picker"), "https://h/p/piano-display.html?view=picker");
    assert.equal(withView("https://h/p.html?v=2", "desk"), "https://h/p.html?v=2&view=desk");
    assert.equal(withView("https://h/p.html#top", "picker"), "https://h/p.html?view=picker#top");
});

test("pickerPlacement: back along the line of sight, raised above the hit, its face toward the visitor", () => {
    const place = { height: 0.48, toward: 0.3, lift: 0.05 };
    // Looking north (-Z) and a little down at a score at the origin
    let { position, rotation } = pickerPlacement({ x: 0, y: 1, z: 0 }, { x: 0, y: -0.3, z: -0.95 }, place);
    assertNear(position, { x: 0, y: 1 + 0.05 + 0.24, z: 0.3 });
    assertNear(rotation, { x: 0, y: 0, z: 0, w: 1 });
    // Looking east (+X): the face (+Z) turns to point west, back at the visitor
    ({ position, rotation } = pickerPlacement({ x: 0, y: 1, z: 0 }, { x: 1, y: 0, z: 0 }, place));
    assertNear(position, { x: -0.3, z: 0 });
    const face = rotateZ(rotation);
    assertNear(face, { x: -1, y: 0, z: 0 });
    // Straight down: no heading, still upright
    ({ rotation } = pickerPlacement({ x: 0, y: 1, z: 0 }, { x: 0, y: -1, z: 0 }, place));
    assertNear(rotation, { x: 0, z: 0 });
});

/** The +Z axis turned by a unit quaternion. @param {{ x: number, y: number, z: number, w: number }} q */
function rotateZ(q) {
    return {
        x: 2 * (q.x * q.z + q.w * q.y),
        y: 2 * (q.y * q.z - q.w * q.x),
        z: 1 - 2 * (q.x * q.x + q.y * q.y)
    };
}

/** @param {Record<string, number>} actual @param {Record<string, number>} expected */
function assertNear(actual, expected) {
    for (const key of Object.keys(expected)) {
        assert.ok(Math.abs(actual[key] - expected[key]) < 1e-9, key + ": " + actual[key] + " != " + expected[key]);
    }
}
