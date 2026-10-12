//
//  piano-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano's logic without the engine: reading the configuration from userData, folding
//  Orpheus's two status replies into the compact status the piano publishes, the reconnect
//  backoff, the text both scripts show, and the desk display's URL, size and button events.
//  No engine globals, so it runs under Node.
//

export const STREAM_PATH = "/api/synth/stream.pcm";
export const PLAYBACK_STATUS_PATH = "/api/playback/status";
export const SYNTH_STATUS_PATH = "/api/synth/status";
export const SONGS_PATH = "/api/songs";
export const DEFAULT_VOLUME = 0.8;
export const ACTIVE_POLL_MS = 1000;
export const IDLE_POLL_MS = 5000;
export const SONG_LINES = 12;

// Scheme, then a host[:port] with none of the characters a placeholder like <host> carries,
// then an optional path. No query or fragment: paths are appended to it.
const BASE_URL = /^https?:\/\/[A-Za-z0-9.\-[\]:]+(\/[^\s?#<>]*)?$/i;

/**
 * @param {unknown} text
 * @returns {Record<string, any>} the parsed object, or an empty one for anything else
 */
export function parseObject(text) {
    try {
        const value = JSON.parse(typeof text === "string" ? text : "");
        return value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
    } catch (e) {
        return {};
    }
}

/**
 * Reads `piano` from the entity's userData. There is no default service: without a usable
 * `orpheusBase` the piano stays silent rather than reach for someone else's.
 * @param {unknown} userDataText
 * @returns {{ orpheusBase: string, volume: number } | { error: string }}
 */
export function parseConfig(userDataText) {
    const piano = parseObject(userDataText).piano;
    if (piano === null || typeof piano !== "object") {
        return { error: "userData has no \"piano\" object" };
    }
    const base = piano.orpheusBase;
    if (typeof base !== "string" || !BASE_URL.test(base)) {
        return { error: "piano.orpheusBase must be an http:// or https:// URL, got " + JSON.stringify(base) };
    }
    const volume = typeof piano.volume === "number" && piano.volume >= 0 && piano.volume <= 1
        ? piano.volume : DEFAULT_VOLUME;
    return { orpheusBase: base.replace(/\/+$/, ""), volume };
}

/**
 * Puts `status` under `piano.status`, keeping every other key of userData.
 * @param {unknown} userDataText
 * @param {PianoStatus} status
 * @returns {string}
 */
export function withStatus(userDataText, status) {
    const userData = parseObject(userDataText);
    const piano = userData.piano !== null && typeof userData.piano === "object" ? userData.piano : {};
    userData.piano = { ...piano, status };
    return JSON.stringify(userData);
}

/**
 * @param {unknown} userDataText
 * @returns {PianoStatus | null} the published status, if any
 */
export function statusOf(userDataText) {
    const piano = parseObject(userDataText).piano;
    const status = piano !== null && typeof piano === "object" ? piano.status : null;
    return status !== null && typeof status === "object" && typeof status.state === "string" ? status : null;
}

/**
 * @typedef {object} PianoStatus
 * @property {"offline" | "idle" | "starting" | "connecting" | "playing" | "paused"} state
 * @property {string | null} title
 * @property {string | null} output "piano" (the Kawai) or "synth" (what the piano entity plays)
 * @property {number | null} progress whole percent through the piece's events
 * @property {number | null} elapsed whole seconds, when the service reports them
 * @property {boolean} synth whether Orpheus's synth output is enabled
 * @property {string | null} error
 */

/**
 * Folds the replies of /api/playback/status and /api/synth/status into what the piano
 * publishes. Only what changes the display goes in, so an unchanged status is not written.
 * @param {Record<string, any> | null} playback null when Orpheus could not be reached
 * @param {Record<string, any> | null} synth null when that request failed
 * @param {string} [failure] why playback is null
 * @returns {PianoStatus}
 */
export function compactStatus(playback, synth, failure) {
    const synthEnabled = synth !== null && synth.enabled === true;
    if (playback === null) {
        return { state: "offline", title: null, output: null, progress: null, elapsed: null, synth: synthEnabled,
            error: failure || "Orpheus unreachable" };
    }
    const active = playback.is_playing === true;
    /** @type {PianoStatus["state"]} */
    let state = "idle";
    if (active) {
        state = playback.is_paused === true ? "paused"
            : ["starting", "connecting"].includes(playback.status) ? playback.status : "playing";
    }
    const lastError = playback.last_error;
    return {
        state,
        title: active && typeof playback.title === "string" ? playback.title : null,
        output: active && typeof playback.output === "string" ? playback.output : null,
        progress: active && typeof playback.progress === "number" ? Math.round(playback.progress * 100) : null,
        // Orpheus reports progress through the events, not time; elapsed_seconds is the mock's
        elapsed: active && typeof playback.elapsed_seconds === "number" ? Math.floor(playback.elapsed_seconds) : null,
        synth: synthEnabled,
        error: lastError !== null && typeof lastError === "object" && typeof lastError.message === "string"
            ? lastError.message : null
    };
}

/**
 * @param {PianoStatus | null} status
 * @returns {number} milliseconds until the next status poll
 */
export function pollDelay(status) {
    return status !== null && status.state !== "idle" && status.state !== "offline" ? ACTIVE_POLL_MS : IDLE_POLL_MS;
}

/**
 * Reconnect delays for the stream: 1 s, doubling to a 30 s cap, back to 1 s after a
 * connection that held for longer than a minute.
 */
export class Backoff {
    failures = 0;

    /**
     * @param {number} connectedMs how long the connection that just ended lasted
     * @returns {number} milliseconds to wait before reconnecting
     */
    next(connectedMs) {
        if (connectedMs > 60_000) {
            this.failures = 0;
        }
        const delay = Math.min(1000 * 2 ** this.failures, 30_000);
        this.failures++;
        return delay;
    }
}

/**
 * @param {number} seconds
 * @returns {string} m:ss
 */
export function formatDuration(seconds) {
    const whole = Math.max(0, Math.floor(seconds));
    return Math.floor(whole / 60) + ":" + String(whole % 60).padStart(2, "0");
}

/**
 * @param {PianoStatus | null} status
 * @returns {string} the floating text's content
 */
export function formatStatus(status) {
    if (status === null) {
        return "Piano\nwaiting for Orpheus";
    }
    if (status.state === "offline") {
        return "Piano\noffline: " + (status.error || "Orpheus unreachable");
    }
    if (status.state === "idle") {
        return "Piano\nidle" + (status.error ? "\nlast error: " + status.error : "");
    }
    /** @type {string[]} */
    const details = [status.state];
    if (status.elapsed !== null) {
        details.push(formatDuration(status.elapsed));
    }
    if (status.progress !== null) {
        details.push(status.progress + "%");
    }
    if (status.output === "piano") {
        details.push("on the real piano");
    }
    return (status.title || "Untitled") + "\n" + details.join("  ");
}

/**
 * The reply to `list`: one line per song as /api/songs returns them, at most `limit` lines.
 * @param {unknown} songs
 * @param {number} [limit]
 * @returns {string}
 */
export function formatSongs(songs, limit = SONG_LINES) {
    if (!Array.isArray(songs)) {
        return "Songs\n(Orpheus did not answer)";
    }
    if (songs.length === 0) {
        return "Songs\n(none yet)";
    }
    const lines = songs.slice(0, limit).map(song => {
        const title = song && typeof song.title === "string" ? song.title : "Untitled";
        const composer = song && typeof song.composer === "string" && song.composer ? " - " + song.composer : "";
        const duration = song && typeof song.duration_seconds === "number"
            ? " (" + formatDuration(song.duration_seconds) + ")" : "";
        return title + composer + duration;
    });
    if (songs.length > limit) {
        lines.push("+" + (songs.length - limit) + " more");
    }
    return "Songs (" + songs.length + ")\n" + lines.join("\n");
}

// The desk display: a Web entity on the music desk's glass, `anchor_desk_screen` in the salon
// grand's joint contract (spec section 4: a 0.30 x 0.17 m panel on a model 1.480 m wide).
export const DESK_SCREEN_SIZE = { x: 0.30, y: 0.17 };
export const PIANO_NATURAL_WIDTH = 1.480;
// 720 x 408 page pixels on the 0.30 x 0.17 m glass: a Web entity is dimensions in inches x dpi
// pixels across (RenderableWebEntityItem.cpp getWindowSize), and piano-display.html is laid out
// for that size.
export const DISPLAY_PIXELS = { x: 720, y: 408 };
export const DISPLAY_ACTIONS = ["previous", "pause", "stop", "next", "replay", "play-song"];
const METERS_TO_INCHES = 39.3701;

// As BASE_URL, but the page may carry a query or fragment.
const PAGE_URL = /^https?:\/\/[A-Za-z0-9.\-[\]:]+(\/[^\s<>]*)?$/i;

/**
 * Reads `piano.displayURL`, the desk display page. It has no default: without one the sitter
 * gets no display and the log says why.
 * @param {unknown} userDataText
 * @returns {{ displayURL: string } | { error: string }}
 */
export function parseDisplayURL(userDataText) {
    const piano = parseObject(userDataText).piano;
    const url = piano !== null && typeof piano === "object" ? piano.displayURL : undefined;
    if (typeof url !== "string" || !PAGE_URL.test(url)) {
        return { error: "piano.displayURL must be an http:// or https:// URL, got " + JSON.stringify(url) };
    }
    return { displayURL: url };
}

/**
 * The display's size on a piano that may be scaled from its natural size, and the dpi that
 * keeps the page at DISPLAY_PIXELS whatever the scale.
 * @param {{ x: number } | undefined} pianoDimensions
 * @returns {{ dimensions: { x: number, y: number, z: number }, dpi: number }}
 */
export function displayGeometry(pianoDimensions) {
    const scale = pianoDimensions && pianoDimensions.x > 0 ? pianoDimensions.x / PIANO_NATURAL_WIDTH : 1;
    const width = DESK_SCREEN_SIZE.x * scale;
    return {
        dimensions: { x: width, y: DESK_SCREEN_SIZE.y * scale, z: 0.01 },
        dpi: DISPLAY_PIXELS.x / (width * METERS_TO_INCHES)
    };
}

/**
 * A button press from piano-display.html over the EventBridge:
 * `{"type": "piano-display", "action": "next"}`, with `songId` for "play-song".
 * @param {unknown} message
 * @returns {{ action: string, songId: number | null } | null} null for anything else
 */
export function parseDisplayEvent(message) {
    const event = parseObject(message);
    if (event.type !== "piano-display" || !DISPLAY_ACTIONS.includes(event.action)) {
        return null;
    }
    if (event.action === "play-song") {
        return Number.isInteger(event.songId) && event.songId > 0 ? { action: event.action, songId: event.songId } : null;
    }
    return { action: event.action, songId: null };
}
