//
//  bar-orders.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Who may do what at the bar, without the engine: the authenticated caller check, the staff
//  check against privateUserData.staff, the per-session rate limiter, and the order book with its time
//  to live and one open order per session. Sessions are compared ignoring braces and case, as
//  the engine's forms of an id differ.
//

const NULL_ID = /^\{?0{8}-0{4}-0{4}-0{4}-0{12}\}?$/;
const UUID = /^\{?[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\}?$/i;

/**
 * @typedef {{ id: string, by: string, name: string, drink: string, kind: string, glass: string, at: number,
 *     expiresAt: number }} Order
 */

/**
 * @param {unknown} caller
 * @returns {caller is string}
 */
export function isCaller(caller) {
    return typeof caller === "string" && UUID.test(caller) && !NULL_ID.test(caller);
}

/**
 * @param {string} id
 * @returns {string} a session id without braces, lower case, as the engine's forms differ
 */
export function sessionKey(id) {
    return id.replace(/[{}]/g, "").toLowerCase();
}

/**
 * @param {string} a
 * @param {string} b
 * @returns {boolean}
 */
export function sameSession(a, b) {
    return sessionKey(a) === sessionKey(b);
}

/**
 * Whether `caller` is the bartender. A session id in `staff` is matched against the
 * authenticated caller. A name is matched only with `byName`, and only against the session
 * display name, which the avatar mixer keeps unique; it still starts from a name the avatar
 * chose, so that is for dev domains.
 * @param {string} caller
 * @param {import("./bar-config.mjs").Staff} staff
 * @param {(session: string) => string} sessionNameOf
 * @returns {boolean}
 */
export function isStaff(caller, { staff, byName }, sessionNameOf) {
    if (!staff) {
        return false;
    }
    if (UUID.test(staff)) {
        return sameSession(caller, staff);
    }
    return byName && sessionNameOf(caller) === staff;
}

/**
 * @param {string} staff
 * @returns {boolean} whether `staff` is a session id rather than a display name
 */
export function isSession(staff) {
    return UUID.test(staff);
}

/**
 * @param {number} ms
 * @returns {string} whole seconds, rounded up
 */
function seconds(ms) {
    return Math.ceil(ms / 1000) + " s";
}

/**
 * At most `calls` calls per `windowMs` per session, every method counted. A refused session
 * is told once per window, so a caller that keeps calling cannot turn refusals into a stream of
 * channel posts. Everything is in memory; a reloaded script starts empty.
 */
export class RateLimiter {
    /**
     * @param {number} calls
     * @param {number} windowMs
     */
    constructor(calls, windowMs) {
        this.calls = calls;
        this.windowMs = windowMs;
        /** @type {Map<string, { times: number[], toldUntil: number }>} */
        this.sessions = new Map();
    }

    /**
     * Counts one call.
     * @param {string} session
     * @param {number} now
     * @returns {{ reason: string, tell: boolean } | null} why it is refused and whether to say so, or null
     */
    admit(session, now) {
        const key = sessionKey(session);
        const entry = this.sessions.get(key) || { times: [], toldUntil: 0 };
        entry.times = entry.times.filter(at => now - at < this.windowMs);
        this.sessions.set(key, entry);
        if (entry.times.length < this.calls) {
            entry.times.push(now);
            return null;
        }
        const freeAt = entry.times[0] + this.windowMs;
        const tell = now >= entry.toldUntil;
        if (tell) {
            entry.toldUntil = now + this.windowMs;
        }
        return {
            reason: "too many calls: at most " + this.calls + " every " + seconds(this.windowMs)
                + "; try again in " + seconds(freeAt - now),
            tell
        };
    }

    /**
     * Forgets sessions with no call in the window, so departed sessions do not pile up.
     * @param {number} now
     */
    prune(now) {
        for (const [key, entry] of this.sessions) {
            if (entry.times.every(at => now - at >= this.windowMs) && now >= entry.toldUntil) {
                this.sessions.delete(key);
            }
        }
    }
}

/**
 * The open orders, oldest first. An order is placed with its expiry; `expire` drops the ones
 * whose time ran out. Everything is in memory; a reloaded script starts empty.
 */
export class OrderBook {
    /** @type {Map<string, Order>} id -> order, oldest first */ orders = new Map();

    get size() {
        return this.orders.size;
    }

    /** @param {Order} order */
    place(order) {
        this.orders.set(order.id, order);
    }

    /**
     * @param {string} id
     * @returns {Order | undefined}
     */
    get(id) {
        return this.orders.get(id);
    }

    /**
     * Takes an order off the book.
     * @param {string} id
     */
    take(id) {
        this.orders.delete(id);
    }

    /**
     * @param {string} session
     * @returns {Order | undefined} the session's open order
     */
    ofSession(session) {
        return this.list().find(order => sameSession(order.by, session));
    }

    /** @returns {Order | undefined} */
    oldest() {
        return this.orders.values().next().value;
    }

    /** @returns {Order[]} oldest first */
    list() {
        return [...this.orders.values()];
    }

    /**
     * @param {number} now
     * @returns {Order[]} the orders whose time ran out, now off the book
     */
    expire(now) {
        const expired = this.list().filter(order => now >= order.expiresAt);
        expired.forEach(order => this.orders.delete(order.id));
        return expired;
    }

    /** @returns {string[]} the ids of every open order, now off the book */
    clear() {
        const ids = [...this.orders.keys()];
        this.orders.clear();
        return ids;
    }
}
