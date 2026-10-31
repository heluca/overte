//
//  seat-server.js
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The seat's serverScripts, run by the entity script server: it decides who sits where. One
//  sitter per seat, each on a lease the sitter's client script renews while seated, so a sitter
//  who crashes or leaves frees the seat when the lease runs out. The caller is
//  Script.remoteCallerID, which the engine stamps from the sending session, so a client cannot
//  claim or release for someone else. The leases live in this script's memory only; it never
//  writes the entity, so the entity can stay locked.
//
//  The server has no model and so no joints: it takes the seat names a client asks for on trust
//  as far as their form goes, and the client checks them against the model's joints.
//
//  A classic entity script, not a module like the seat's client script: domains running the
//  upstream server cannot load .mjs serverScripts. It uses only what upstream has.
//

(function () {
    var NULL_UUID = "{00000000-0000-0000-0000-000000000000}";
    var DEFAULT_LEASE_MS = 15000;
    var MIN_LEASE_MS = 3000;
    var MAX_LEASE_MS = 120000;
    var MAX_SEATS = 64;
    var SEAT_NAME = /^seat_[1-9][0-9]*$/;

    /**
     * userData.seat.leaseMs, how long a seat stays taken without a renewal.
     * @param {string} entityID
     * @param {unknown} userDataText
     * @returns {number}
     */
    function leaseFrom(entityID, userDataText) {
        var seat = null;
        try {
            var userData = JSON.parse(typeof userDataText === "string" && userDataText ? userDataText : "{}");
            seat = userData !== null && typeof userData === "object" ? userData.seat : null;
        } catch (e) {
            // Unparseable userData is the same as none
        }
        var value = seat !== null && typeof seat === "object" ? seat.leaseMs : undefined;
        if (value === undefined) {
            return DEFAULT_LEASE_MS;
        }
        if (typeof value === "number" && value >= MIN_LEASE_MS && value <= MAX_LEASE_MS) {
            return value;
        }
        print("Seat " + entityID + ": seat.leaseMs must be a number from " + MIN_LEASE_MS + " to "
            + MAX_LEASE_MS + ", got " + JSON.stringify(value) + "; using " + DEFAULT_LEASE_MS);
        return DEFAULT_LEASE_MS;
    }

    /**
     * The seat names in a claim, in the caller's order, without anything that is not one.
     * @param {unknown} params
     * @returns {string[]}
     */
    function seatNamesFrom(params) {
        /** @type {string[]} */
        var names = [];
        if (!Array.isArray(params)) {
            return names;
        }
        for (var i = 0; i < params.length && i < MAX_SEATS; i++) {
            if (typeof params[i] === "string" && SEAT_NAME.test(params[i])) {
                names.push(params[i]);
            }
        }
        return names;
    }

    /**
     * @param {string} session
     * @param {string} method
     * @param {string[]} params
     */
    function reply(session, method, params) {
        Entities.callEntityClientMethod(session, seat.entityID, method, params);
    }

    /** @returns {string | null} the caller, or null when the call came from no session */
    function caller() {
        var id = Script.remoteCallerID;
        return id && id !== NULL_UUID ? id : null;
    }

    /**
     * Forgets the leases that ran out, so every seat in `holders` is held.
     * @param {number} now
     */
    function expire(now) {
        for (var name in seat.holders) {
            if (now >= seat.holders[name].expiresAt) {
                delete seat.holders[name];
            }
        }
    }

    /**
     * @param {string} session
     * @returns {string | null} the seat the session holds
     */
    function heldBy(session) {
        for (var name in seat.holders) {
            if (seat.holders[name].session === session) {
                return name;
            }
        }
        return null;
    }

    // The engine constructs the entity script from this function; returning an object makes
    // that object the script (as upstream's own sitServer.js does with its prototype).
    var seat = {
        remotelyCallable: ["claim", "release"],
        entityID: NULL_UUID,
        leaseMs: DEFAULT_LEASE_MS,
        /**
         * Seat name to its sitter's session and when the lease runs out. A session holds at most one.
         * @type {Object<string, { session: string, expiresAt: number }>}
         */
        holders: {},

        /** @param {string} entityID */
        preload: function (entityID) {
            seat.entityID = entityID;
            seat.leaseMs = leaseFrom(entityID, Entities.getEntityProperties(entityID, ["userData"]).userData);
        },

        // Remotely callable. Script.remoteCallerID is set while the method runs.

        /**
         * A caller who holds a seat has its lease renewed, whatever it asks for. Anyone else gets
         * the first seat in `params` (seat names, the caller's preference first) that is free.
         * The answer is `seatGranted` [seat, leaseMs], so the client knows how often to renew, or
         * `seatRefused` ["taken"] or ["no-seat"].
         * @param {string} _entityID
         * @param {unknown} params
         * @returns {boolean} whether the caller holds a seat
         */
        claim: function (_entityID, params) {
            var session = caller();
            if (session === null) {
                return false;
            }
            var now = Date.now();
            expire(now);
            var name = heldBy(session);
            if (name === null) {
                var names = seatNamesFrom(params);
                if (names.length === 0) {
                    reply(session, "seatRefused", ["no-seat"]);
                    return false;
                }
                for (var i = 0; i < names.length && name === null; i++) {
                    if (!seat.holders.hasOwnProperty(names[i])) {
                        name = names[i];
                    }
                }
                if (name === null) {
                    reply(session, "seatRefused", ["taken"]);
                    return false;
                }
            }
            seat.holders[name] = { session: session, expiresAt: now + seat.leaseMs };
            reply(session, "seatGranted", [name, String(seat.leaseMs)]);
            return true;
        },

        /** @returns {boolean} whether the caller held a seat */
        release: function () {
            var session = caller();
            if (session === null) {
                return false;
            }
            expire(Date.now());
            var name = heldBy(session);
            if (name === null) {
                return false;
            }
            delete seat.holders[name];
            return true;
        }
    };

    return seat;
});
