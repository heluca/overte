//
//  piano-bridge-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano's side of Orpheus's Overte bridge without the engine: the per-session limits on
//  song requests and transport (Morpheus ADRs 0020 and 0022), the route each call takes, the
//  reason a refusal gives, and the text a visitor sees. No engine globals, so it runs under Node.
//
//  Orpheus holds the same kinds of limits per bridge credential (its server/bridge.py); the
//  piano keeps its own so a domain can be stricter than the service, and so a refusal costs no
//  round trip.
//

// The bridge routes this piano drives. Every one that can start a piece names output=synth:
// Orpheus forces it for a bridge token anyway, and a wrong credential must not sound a piano.
export const REQUEST_PATH = "/api/playback/request/";
export const TRANSPORT_ROUTES = {
    pause: "/api/playback/pause",
    stop: "/api/playback/stop",
    next: "/api/playback/next",
    previous: "/api/playback/previous?output=synth",
    replay: "/api/playback/replay?output=synth"
};
export const SESSION_HEADER = "X-Overte-Session";

/** @typedef {{ requestCooldownMs: number, requestQueueCap: number, transportCalls: number, transportWindowMs: number }} Limits */

/** @type {Limits} */
export const LIMIT_DEFAULTS = {
    requestCooldownMs: 10_000,
    requestQueueCap: 5,
    transportCalls: 5,
    transportWindowMs: 30_000
};
/** @type {Record<keyof Limits, [number, number]>} */
const LIMIT_RANGES = {
    requestCooldownMs: [0, 600_000],
    // 0 turns requests off, as Orpheus's own cap does
    requestQueueCap: [0, 50],
    transportCalls: [1, 100],
    transportWindowMs: [1000, 600_000]
};

/**
 * The limits from the `piano` object of userData, every key optional and an integer. A value
 * out of range is reported and replaced by its default.
 * @param {Record<string, any>} piano
 * @returns {{ limits: Limits, problems: string[] }}
 */
export function parseLimits(piano) {
    /** @type {string[]} */
    const problems = [];
    const limits = { ...LIMIT_DEFAULTS };
    for (const name of /** @type {(keyof Limits)[]} */ (Object.keys(LIMIT_DEFAULTS))) {
        const value = piano[name];
        if (value === undefined) {
            continue;
        }
        const [min, max] = LIMIT_RANGES[name];
        if (Number.isInteger(value) && value >= min && value <= max) {
            limits[name] = value;
        } else {
            problems.push("piano." + name + " must be an integer from " + min + " to " + max + ", got "
                + JSON.stringify(value) + "; using " + LIMIT_DEFAULTS[name]);
        }
    }
    return { limits, problems };
}

/**
 * @param {number} ms
 * @returns {string} whole seconds, rounded up
 */
function seconds(ms) {
    return Math.ceil(ms / 1000) + " s";
}

/**
 * Who may request and run the transport now, per visitor session (`Script.remoteCallerID`).
 * A session is not a person: a visitor who reconnects starts afresh, and the queue cap bounds
 * what that buys. Everything is in memory; a reloaded script starts empty, as Orpheus does.
 */
export class SessionLimits {
    /** @param {Limits} limits */
    constructor(limits) {
        this.limits = limits;
        /** @type {Map<string, { queueId: number | null, since: number }>} session -> its request still waiting */
        this.waiting = new Map();
        /** @type {Map<string, number>} session -> when its last request was admitted */
        this.lastRequest = new Map();
        /** @type {Map<string, number[]>} session -> times of its recent transport calls */
        this.transport = new Map();
    }

    /**
     * Admits a song request, holding the session's one waiting slot until `requested` or
     * `failed`, so a second call while the first is in flight is refused too.
     * @param {string} session
     * @param {number} now
     * @returns {string | null} why it is refused, or null when admitted
     */
    admitRequest(session, now) {
        const { requestCooldownMs, requestQueueCap } = this.limits;
        if (requestQueueCap === 0) {
            return "Song requests are turned off on this piano";
        }
        if (this.waiting.has(session)) {
            return "You already have a song waiting; it must start before you request another";
        }
        const last = this.lastRequest.get(session);
        if (last !== undefined && now - last < requestCooldownMs) {
            return "One request every " + seconds(requestCooldownMs) + "; try again in "
                + seconds(last + requestCooldownMs - now);
        }
        if (this.waiting.size >= requestQueueCap) {
            return this.waiting.size + " requests from this piano are already waiting (the cap is "
                + requestQueueCap + "); try again when one has started";
        }
        this.waiting.set(session, { queueId: null, since: now });
        this.lastRequest.set(session, now);
        return null;
    }

    /**
     * Orpheus queued the request. A song that started at once (position 0) is not waiting.
     * @param {string} session
     * @param {number} queueId
     * @param {number} position
     * @param {number} now
     */
    requested(session, queueId, position, now) {
        if (position === 0) {
            this.waiting.delete(session);
        } else {
            this.waiting.set(session, { queueId, since: now });
        }
    }

    /**
     * Orpheus did not queue it: the slot frees, the cool-down stands (it limits attempts).
     * @param {string} session
     */
    failed(session) {
        this.waiting.delete(session);
    }

    /**
     * Frees the slots of requests that are no longer in Orpheus's queue: started, removed or
     * cleared. Only requests recorded before the status was asked for, so a reply that left
     * the queue before a new request reached it does not free that request's slot.
     * @param {number[]} queueIds the ids waiting in /api/playback/status's `queue`
     * @param {number} polledAt when that status was asked for
     */
    settle(queueIds, polledAt) {
        for (const [session, { queueId, since }] of this.waiting) {
            if (queueId !== null && since < polledAt && !queueIds.includes(queueId)) {
                this.waiting.delete(session);
            }
        }
    }

    /**
     * Counts one transport call against the session's window.
     * @param {string} session
     * @param {number} now
     * @returns {string | null} why it is refused, or null when admitted
     */
    admitTransport(session, now) {
        const { transportCalls, transportWindowMs } = this.limits;
        for (const [key, times] of this.transport) {
            const recent = times.filter(at => now - at < transportWindowMs);
            if (recent.length === 0) {
                this.transport.delete(key);
            } else {
                this.transport.set(key, recent);
            }
        }
        const times = this.transport.get(session) || [];
        if (times.length >= transportCalls) {
            return "Too many controls: at most " + transportCalls + " every " + seconds(transportWindowMs)
                + "; try again in " + seconds(times[0] + transportWindowMs - now);
        }
        times.push(now);
        this.transport.set(session, times);
        return null;
    }

    /**
     * Forgets cool-downs that have run out, so departed sessions do not pile up.
     * @param {number} now
     */
    prune(now) {
        for (const [session, at] of this.lastRequest) {
            if (now - at >= this.limits.requestCooldownMs) {
                this.lastRequest.delete(session);
            }
        }
    }
}

/**
 * @param {unknown} param the first of a remote call's string params
 * @returns {number | null} a library song id
 */
export function songIdOf(param) {
    const text = typeof param === "number" ? String(param) : param;
    return typeof text === "string" && /^[1-9][0-9]{0,9}$/.test(text) ? Number(text) : null;
}

/**
 * Why Orpheus refused: FastAPI's `{"detail": "..."}`, else the status.
 * @param {number} status
 * @param {string} body
 * @returns {string}
 */
export function refusalReason(status, body) {
    try {
        const detail = JSON.parse(body).detail;
        if (typeof detail === "string" && detail) {
            return detail;
        }
    } catch (e) {
        // not JSON: fall through to the status
    }
    return "Orpheus answered HTTP " + status;
}

/**
 * @typedef {object} CallResult
 * @property {string} action request, play, pause, stop, next, previous, replay or select
 * @property {boolean} ok
 * @property {string} [reason] why it was refused or failed
 * @property {number} [songId]
 * @property {string} [title] the song's title, when the piano knows it
 * @property {number} [position] for a request: 1 plays next, 0 already started
 */

/**
 * What the visitor's floating label shows for a call's result.
 * @param {CallResult} result
 * @returns {string}
 */
export function formatResult(result) {
    const song = result.title || (result.songId !== undefined ? "song " + result.songId : "");
    if (!result.ok) {
        return "Piano: " + result.action + (song ? " " + song : "") + " refused\n" + (result.reason || "");
    }
    if (result.action === "request") {
        const when = result.position === 0 ? "playing now"
            : result.position === 1 ? "plays next" : "number " + result.position + " in the queue";
        return "Requested: " + song + "\n" + when;
    }
    if (result.action === "select") {
        return "Playing now: " + song;
    }
    return "Piano: " + result.action;
}

/**
 * /api/songs as the structured list the song picker reads: ids with what to show.
 * @param {unknown} songs
 * @returns {{ id: number, title: string, composer: string | null, duration: number | null }[] | null}
 */
export function songEntries(songs) {
    if (!Array.isArray(songs)) {
        return null;
    }
    return songs.filter(song => song && Number.isInteger(song.id)).map(song => ({
        id: song.id,
        title: typeof song.title === "string" ? song.title : "Untitled",
        composer: typeof song.composer === "string" && song.composer ? song.composer : null,
        duration: typeof song.duration_seconds === "number" ? Math.round(song.duration_seconds) : null
    }));
}
