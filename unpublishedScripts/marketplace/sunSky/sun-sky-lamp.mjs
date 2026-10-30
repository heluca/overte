//
//  sun-sky-lamp.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A lamp's `serverScripts`, run by the entity script server on a Light (or any entity): shown
//  while the sun is down, from sunset to sunrise, and hidden by day. It works the phase out from
//  the clock and userData.sunSky.noonUtc once a minute, and edits its own `visible` only when
//  that is wrong. No messages; it never writes userData or `locked`.
//
//  The engine shares one module instance per URL between every entity that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import { phase, readSunSky, sunPosition } from "./sun-sky.mjs";

export const TICK_MS = 60000;

export default class SunSkyLamp {
    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {number} */ noonUtc;
    /** @type {number | null} */ timer = null;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["userData", "locked"]);
        const { noonUtc, error } = readSunSky(properties.userData);
        if (noonUtc === null) {
            print("Sun sky lamp " + entityID + ": " + error + ". The lamp does not switch.");
            return;
        }
        this.noonUtc = noonUtc;
        if (properties.locked === true) {
            print("Sun sky lamp " + entityID + ": the lamp is locked and takes no edit; unlock it and reload its script.");
        }
        this.tick();
        this.timer = Script.setInterval(() => this.tick(), TICK_MS);
    }

    unload() {
        if (this.timer !== null) {
            Script.clearInterval(this.timer);
            this.timer = null;
        }
    }

    /** Shows or hides the lamp for this moment, if it is not already so. */
    tick() {
        const on = phase(sunPosition(Date.now(), this.noonUtc).elevation) !== "day";
        if (Entities.getEntityProperties(this.entityID, ["visible"]).visible !== on) {
            Entities.editEntity(this.entityID, { visible: on });
        }
    }
}
