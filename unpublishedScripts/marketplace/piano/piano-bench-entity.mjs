//
//  piano-bench-entity.mjs
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
//  The engine shares one module instance per URL between every bench that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import { parseBenchConfig, SeatLease } from "./piano-bench-core.mjs";

const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";

export default class PianoBenchEntity {
    remotelyCallable = ["claim", "release"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {SeatLease} */ lease;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const { config, problems } = parseBenchConfig(Entities.getEntityProperties(entityID, ["userData"]).userData);
        problems.forEach(problem => print("Piano bench " + entityID + ": " + problem));
        this.lease = new SeatLease(config.leaseMs);
    }

    // Remotely callable. Script.remoteCallerID is set for the synchronous part only.

    /** @returns {boolean} whether the caller holds the seat */
    claim() {
        const caller = Script.remoteCallerID;
        if (!caller || caller === NULL_UUID) {
            return false;
        }
        const granted = this.lease.claim(caller, Date.now());
        Entities.callEntityClientMethod(caller, this.entityID, granted ? "seatGranted" : "seatRefused", []);
        return granted;
    }

    /** @returns {boolean} whether the caller held the seat */
    release() {
        const caller = Script.remoteCallerID;
        return !!caller && this.lease.release(caller);
    }
}
