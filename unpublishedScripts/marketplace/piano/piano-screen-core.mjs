//
//  piano-screen-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano's two screens without the engine: the song picker that opens from the score and
//  the desk screen for whoever sits at the keyboard. Both are piano-display.html in a local Web
//  entity; the client script owns their state and sends the page a finished view to draw, so
//  everything that decides what a visitor sees is here: the song list in pages, the lit and
//  dimmed transport buttons, the queue, and the message a result or a refusal leaves.
//  No engine globals, so it runs under Node.
//

import { formatDuration } from "./piano-core.mjs";

// page -> script: {"type": "piano-display", "action": ...}; script -> page: {"type": "piano-view", ...}
export const EVENT_TYPE = "piano-display";
export const VIEW_TYPE = "piano-view";
export const TRANSPORT_ACTIONS = ["previous", "play", "pause", "stop", "next", "replay"];
export const PICKER_PAGE_SIZE = 5;
export const DESK_PAGE_SIZE = 5;
export const NOTICE_MS = 8000;
const TABS = ["queue", "songs"];
const ACTIVE = ["playing", "starting", "connecting"];

/** @typedef {{ id: number, title: string, composer: string | null, duration: number | null }} Song */
/** @typedef {{ text: string, tone: "ok" | "refused" | "info", until: number }} Notice */
/**
 * @typedef {{ action: "ready" | "close" } | { action: "previous" | "play" | "pause" | "stop" | "next" | "replay" }
 *     | { action: "request", songId: number } | { action: "page", page: number } | { action: "tab", tab: "queue" | "songs" }} ScreenEvent
 */

/**
 * The song list a `showSongs` answer carries for a picker (its second param).
 * @param {unknown} json
 * @returns {Song[] | null} null when Orpheus did not answer or the list is malformed
 */
export function parseSongs(json) {
    let songs;
    try {
        songs = JSON.parse(typeof json === "string" ? json : "");
    } catch (e) {
        return null;
    }
    if (!Array.isArray(songs)) {
        return null;
    }
    return songs.filter(song => song && Number.isInteger(song.id) && song.id > 0 && typeof song.title === "string")
        .map(song => ({
            id: song.id,
            title: song.title,
            composer: typeof song.composer === "string" && song.composer ? song.composer : null,
            duration: typeof song.duration === "number" && song.duration >= 0 ? song.duration : null
        }));
}

/**
 * One page of the song list, the page number clamped into range.
 * @param {Song[]} songs
 * @param {number} page from 0
 * @param {number} size songs per page
 * @returns {{ items: { id: number, title: string, composer: string | null, length: string | null }[],
 *     page: number, pages: number, total: number }}
 */
export function songPage(songs, page, size) {
    const pages = Math.max(1, Math.ceil(songs.length / size));
    const at = Math.min(Math.max(0, Number.isInteger(page) ? page : 0), pages - 1);
    const items = songs.slice(at * size, at * size + size).map(song => ({
        id: song.id,
        title: song.title,
        composer: song.composer,
        length: song.duration !== null ? formatDuration(song.duration) : null
    }));
    return { items, page: at, pages, total: songs.length };
}

/**
 * A message from piano-display.html over the EventBridge.
 * @param {unknown} message
 * @returns {ScreenEvent | null} null for anything that is not one of the page's events
 */
export function parseScreenEvent(message) {
    let event;
    try {
        event = JSON.parse(typeof message === "string" ? message : "");
    } catch (e) {
        return null;
    }
    if (event === null || typeof event !== "object" || event.type !== EVENT_TYPE) {
        return null;
    }
    const action = event.action;
    if (action === "ready" || action === "close" || TRANSPORT_ACTIONS.includes(action)) {
        return { action };
    }
    if (action === "request") {
        return Number.isInteger(event.songId) && event.songId > 0 ? { action, songId: event.songId } : null;
    }
    if (action === "page") {
        return Number.isInteger(event.page) && event.page >= 0 ? { action, page: event.page } : null;
    }
    if (action === "tab") {
        return TABS.includes(event.tab) ? { action, tab: event.tab } : null;
    }
    return null;
}

const DONE = {
    play: "Playing", pause: "Paused", stop: "Stopped, the queue cleared", next: "Skipped to the next piece",
    previous: "Back to the start", replay: "Playing it again"
};

/**
 * The message a call's result leaves on a screen (piano-bridge-core.mjs CallResult).
 * @param {import("./piano-bridge-core.mjs").CallResult} result
 * @param {number} now
 * @returns {Notice}
 */
export function resultNotice(result, now) {
    const until = now + NOTICE_MS;
    const song = result.title || (result.songId !== undefined ? "Song " + result.songId : "");
    if (!result.ok) {
        const what = result.action === "request" || result.action === "select" ? song : capitalised(result.action);
        return { text: (what ? what + ": " : "") + (result.reason || "refused"), tone: "refused", until };
    }
    if (result.action === "request") {
        const when = result.position === 0 ? "playing now"
            : result.position === 1 ? "plays next" : "number " + result.position + " in the queue";
        return { text: song + ": " + when, tone: "ok", until };
    }
    if (result.action === "select") {
        return { text: "Playing now: " + song, tone: "ok", until };
    }
    return { text: DONE[/** @type {keyof typeof DONE} */ (result.action)] || capitalised(result.action), tone: "ok", until };
}

/**
 * Shown from the click until the piano answers.
 * @param {string} text
 * @param {number} now
 * @returns {Notice}
 */
export function waitingNotice(text, now) {
    return { text, tone: "info", until: now + NOTICE_MS };
}

/** @param {string} word */
function capitalised(word) {
    return word.charAt(0).toUpperCase() + word.slice(1);
}

/**
 * @param {Notice | null} notice
 * @param {number} now
 * @returns {{ text: string, tone: string } | null} the notice while it is fresh
 */
function fresh(notice, now) {
    return notice && now < notice.until ? { text: notice.text, tone: notice.tone } : null;
}

/**
 * How each transport button looks for the status: `lit` shows what the piano is doing (play
 * while a piece plays or starts, pause while paused), `dim` that the button would be refused
 * for the state (a lit one is not dimmed). A dim button still sends, so the visitor sees the
 * reason.
 * @param {import("./piano-core.mjs").PianoStatus | null} status
 * @returns {Record<string, { lit: boolean, dim: boolean }>}
 */
export function transportButtons(status) {
    const state = status ? status.state : "offline";
    const active = ACTIVE.includes(state);
    const paused = state === "paused";
    const online = state !== "offline";
    const queued = status && typeof status.queued === "number" ? status.queued : 0;
    return {
        previous: { lit: false, dim: !online },
        play: { lit: active, dim: !online },
        pause: { lit: paused, dim: !online || (!active && !paused) },
        stop: { lit: false, dim: !online || (!active && !paused && queued === 0) },
        next: { lit: false, dim: !online || queued === 0 },
        replay: { lit: false, dim: !online }
    };
}

/**
 * @param {Song[] | null | undefined} songs undefined while the list is on its way
 * @param {number} page
 * @param {number} size
 */
function songsView(songs, page, size) {
    if (songs === undefined) {
        return { state: "loading" };
    }
    if (songs === null) {
        return { state: "error", text: "Orpheus did not answer; try again in a moment" };
    }
    return { state: "ready", ...songPage(songs, page, size) };
}

/**
 * The song picker that opens from the score.
 * @param {{ songs: Song[] | null | undefined, page: number, notice: Notice | null }} picker
 * @param {number} now
 */
export function pickerView(picker, now) {
    return {
        type: VIEW_TYPE,
        kind: "picker",
        songs: songsView(picker.songs, picker.page, PICKER_PAGE_SIZE),
        notice: fresh(picker.notice, now)
    };
}

/**
 * The desk screen: what plays and how far, the transport, and what is queued or the songs.
 * @param {import("./piano-core.mjs").PianoStatus | null} status
 * @param {{ tab: "queue" | "songs", songs: Song[] | null | undefined, page: number, notice: Notice | null }} desk
 * @param {number} now
 */
export function deskView(status, desk, now) {
    const state = status ? status.state : "waiting";
    const active = ACTIVE.includes(state) || state === "paused";
    const duration = active && typeof status?.duration === "number" ? status.duration : null;
    const progress = active && typeof status?.progress === "number" ? status.progress : null;
    let elapsed = active && typeof status?.elapsed === "number" ? status.elapsed : null;
    if (elapsed === null && progress !== null && duration !== null) {
        elapsed = Math.round(progress / 100 * duration);
    }
    const queue = Array.isArray(status?.queue) ? status.queue.filter(title => typeof title === "string") : [];
    const queued = typeof status?.queued === "number" ? status.queued : queue.length;
    let title = status?.title || "Untitled";
    let detail = status?.output === "piano" ? "on the real piano" : null;
    if (state === "waiting") {
        title = "Waiting for Orpheus";
    } else if (state === "offline") {
        title = "Orpheus is offline";
        detail = status?.error || null;
    } else if (state === "idle") {
        title = "Nothing playing";
        detail = status?.error ? "Last error: " + status.error : null;
    }
    return {
        type: VIEW_TYPE,
        kind: "desk",
        state,
        title,
        detail,
        progress,
        elapsed: elapsed !== null ? formatDuration(elapsed) : null,
        duration: duration !== null ? formatDuration(duration) : null,
        buttons: transportButtons(status),
        tab: desk.tab,
        queue: { items: queue, more: Math.max(0, queued - queue.length) },
        songs: desk.tab === "songs" ? songsView(desk.songs, desk.page, DESK_PAGE_SIZE) : null,
        notice: fresh(desk.notice, now)
    };
}

/**
 * The page's URL for a view: `view=picker` added to its query, before any fragment.
 * @param {string} url
 * @param {"desk" | "picker"} view
 * @returns {string}
 */
export function withView(url, view) {
    const hash = url.indexOf("#");
    const base = hash < 0 ? url : url.slice(0, hash);
    const fragment = hash < 0 ? "" : url.slice(hash);
    return base + (base.includes("?") ? "&" : "?") + "view=" + view + fragment;
}

/**
 * Where the picker floats for a click on the score: back along the visitor's line of sight
 * from the hit, its bottom edge just above it, turned to face them upright (+Z, a Web
 * entity's face, toward them).
 * @param {{ x: number, y: number, z: number }} hit
 * @param {{ x: number, y: number, z: number }} direction the click's ray
 * @param {{ height: number, toward: number, lift: number }} place metres
 * @returns {{ position: { x: number, y: number, z: number }, rotation: { x: number, y: number, z: number, w: number } }}
 */
export function pickerPlacement(hit, direction, place) {
    const length = Math.hypot(direction.x, direction.z);
    // Straight down onto the score: no heading to face, so face along -Z
    const dx = length > 1e-6 ? direction.x / length : 0;
    const dz = length > 1e-6 ? direction.z / length : -1;
    // A yaw that turns -Z onto the line of sight, so the face (+Z) looks back at the visitor
    const yaw = Math.atan2(-dx, -dz);
    return {
        position: {
            x: hit.x - dx * place.toward,
            y: hit.y + place.lift + place.height / 2,
            z: hit.z - dz * place.toward
        },
        rotation: { x: 0, y: Math.sin(yaw / 2), z: 0, w: Math.cos(yaw / 2) }
    };
}
