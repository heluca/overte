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

// The desk display: a Web entity over the face of the music desk, parented to the salon grand's
// `anchor_desk_screen` joint (spec sections 3 to 5). Measured in salon-grand.glb, in the
// anchor's frame (+Z out of the desk towards the player, +Y up the desk): the desk board is
// 0.82 m across (x -0.41 to 0.41), its face is at z -0.0045, the ledge stands proud of it
// below y -0.12, the board's top is at y 0.175, and the closed score in front of it reaches
// z 0.0099. The default covers the face above the ledge, 10 mm inside its edges, 3 mm in front
// of the score.
export const DISPLAY_DEFAULTS = {
    width: 0.80,
    height: 0.275,
    offset: { x: 0, y: 0.0275, z: 0.013 },
    // The page lays itself out in rem scaled to the viewport, so this is sharpness, not size
    pagePixelsWide: 1600
};
export const PIANO_NATURAL_WIDTH = 1.480;
export const DISPLAY_ACTIONS = ["previous", "pause", "stop", "next", "replay", "play-song"];
// A Web entity is its dimensions in inches times dpi pixels across (RenderableWebEntityItem.cpp
// getWindowSize)
const METERS_TO_INCHES = 39.3701;
/** @type {Record<string, [number, number]>} */
const DISPLAY_LIMITS = {
    width: [0.05, 3],
    height: [0.05, 2],
    offset: [-1, 1],
    pagePixelsWide: [200, 4096]
};

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
 * @typedef {object} DisplayGeometry
 * @property {{ x: number, y: number, z: number }} dimensions
 * @property {{ x: number, y: number, z: number }} localPosition from `anchor_desk_screen`
 * @property {number} dpi
 */

/**
 * The display's size, place and resolution from `piano.display`, every key optional:
 * `{ width, height, offset: { x, y, z }, pagePixelsWide }`, metres in the piano's natural size.
 * A value out of range is reported and replaced by its default. The result is scaled with a
 * piano that is not at its natural size (a child's local position does not scale by itself).
 * @param {unknown} userDataText
 * @param {{ x: number } | undefined} pianoDimensions
 * @returns {{ geometry: DisplayGeometry, problems: string[], source: string }} source: the raw
 *     `piano.display` as JSON, to tell when it changed
 */
export function parseDisplayGeometry(userDataText, pianoDimensions) {
    const piano = parseObject(userDataText).piano;
    const found = piano !== null && typeof piano === "object" ? piano.display : undefined;
    const display = found !== null && typeof found === "object" && !Array.isArray(found) ? found : {};
    /** @type {string[]} */
    const problems = [];
    if (found !== undefined && display !== found) {
        problems.push("piano.display must be an object, got " + JSON.stringify(found) + "; using the defaults");
    }
    /**
     * @param {string} name
     * @param {unknown} value
     * @param {number} fallback
     * @param {[number, number]} range
     */
    const take = (name, value, fallback, [min, max]) => {
        if (value === undefined) {
            return fallback;
        }
        if (typeof value === "number" && value >= min && value <= max) {
            return value;
        }
        problems.push("piano.display." + name + " must be a number from " + min + " to " + max + ", got "
            + JSON.stringify(value) + "; using " + fallback);
        return fallback;
    };
    const width = take("width", display.width, DISPLAY_DEFAULTS.width, DISPLAY_LIMITS.width);
    const height = take("height", display.height, DISPLAY_DEFAULTS.height, DISPLAY_LIMITS.height);
    const pixels = take("pagePixelsWide", display.pagePixelsWide, DISPLAY_DEFAULTS.pagePixelsWide,
        DISPLAY_LIMITS.pagePixelsWide);
    const offsetIn = display.offset !== null && typeof display.offset === "object" ? display.offset : {};
    if (display.offset !== undefined && offsetIn !== display.offset) {
        problems.push("piano.display.offset must be an object { x, y, z }, got " + JSON.stringify(display.offset));
    }
    const offset = {
        x: take("offset.x", offsetIn.x, DISPLAY_DEFAULTS.offset.x, DISPLAY_LIMITS.offset),
        y: take("offset.y", offsetIn.y, DISPLAY_DEFAULTS.offset.y, DISPLAY_LIMITS.offset),
        z: take("offset.z", offsetIn.z, DISPLAY_DEFAULTS.offset.z, DISPLAY_LIMITS.offset)
    };
    const scale = pianoDimensions && pianoDimensions.x > 0 ? pianoDimensions.x / PIANO_NATURAL_WIDTH : 1;
    return {
        geometry: {
            dimensions: { x: width * scale, y: height * scale, z: 0.01 },
            localPosition: { x: offset.x * scale, y: offset.y * scale, z: offset.z * scale },
            dpi: pixels / (width * scale * METERS_TO_INCHES)
        },
        problems,
        source: JSON.stringify(found === undefined ? null : found)
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
