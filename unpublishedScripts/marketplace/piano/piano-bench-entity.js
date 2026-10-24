//
//  piano-bench-entity.js
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano bench's serverScripts, run by the entity script server: it decides who sits. One
//  sitter at a time, on a lease the sitter's client script renews while seated, so a sitter who
//  crashes or leaves frees the seat when the lease runs out. The caller is Script.remoteCallerID,
//  which the engine stamps from the sending session, so a client cannot claim for someone else.
//
//  A classic entity script, not a module like the bench's client script: domains running the
//  upstream server cannot load .mjs serverScripts. It uses only what upstream has.
//

(function () {
    var NULL_UUID = "{00000000-0000-0000-0000-000000000000}";
    var DEFAULT_LEASE_MS = 15000;
    var MIN_LEASE_MS = 3000;
    var MAX_LEASE_MS = 120000;

    /**
     * userData.pianoBench.leaseMs, how long a seat stays taken without a renewal.
     * @param {string} entityID
     * @param {unknown} userDataText
     * @returns {number}
     */
    function leaseFrom(entityID, userDataText) {
        var bench = null;
        try {
            var userData = JSON.parse(typeof userDataText === "string" && userDataText ? userDataText : "{}");
            bench = userData !== null && typeof userData === "object" ? userData.pianoBench : null;
        } catch (e) {
            // Unparseable userData is the same as none
        }
        var value = bench !== null && typeof bench === "object" ? bench.leaseMs : undefined;
        if (value === undefined) {
            return DEFAULT_LEASE_MS;
        }
        if (typeof value === "number" && value >= MIN_LEASE_MS && value <= MAX_LEASE_MS) {
            return value;
        }
        print("Piano bench " + entityID + ": pianoBench.leaseMs must be a number from " + MIN_LEASE_MS + " to "
            + MAX_LEASE_MS + ", got " + JSON.stringify(value) + "; using " + DEFAULT_LEASE_MS);
        return DEFAULT_LEASE_MS;
    }

    // The engine constructs the entity script from this function; returning an object makes
    // that object the script (as upstream's own sitServer.js does with its prototype).
    var bench = {
        remotelyCallable: ["claim", "release"],
        entityID: NULL_UUID,
        leaseMs: DEFAULT_LEASE_MS,
        /** @type {string | null} */
        holder: null,
        expiresAt: 0,

        /** @param {string} entityID */
        preload: function (entityID) {
            bench.entityID = entityID;
            bench.leaseMs = leaseFrom(entityID, Entities.getEntityProperties(entityID, ["userData"]).userData);
        },

        // Remotely callable. Script.remoteCallerID is set while the method runs.

        /**
         * Granted when the seat is free, its lease ran out, or the caller holds it (a renewal).
         * The answer carries the lease so the client knows how often to renew.
         * @returns {boolean} whether the caller holds the seat
         */
        claim: function () {
            var caller = Script.remoteCallerID;
            if (!caller || caller === NULL_UUID) {
                return false;
            }
            var now = Date.now();
            var granted = bench.holder === null || bench.holder === caller || now >= bench.expiresAt;
            if (granted) {
                bench.holder = caller;
                bench.expiresAt = now + bench.leaseMs;
            }
            Entities.callEntityClientMethod(caller, bench.entityID, granted ? "seatGranted" : "seatRefused",
                [String(bench.leaseMs)]);
            return granted;
        },

        /** @returns {boolean} whether the caller held the seat */
        release: function () {
            var caller = Script.remoteCallerID;
            if (!caller || caller !== bench.holder) {
                return false;
            }
            bench.holder = null;
            bench.expiresAt = 0;
            return true;
        }
    };

    return bench;
});
