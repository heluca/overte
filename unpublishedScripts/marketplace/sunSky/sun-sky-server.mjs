//
//  sun-sky-server.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The sky zone's `serverScripts`, run by the entity script server: once a minute it lights its
//  own zone for where the sun stands (sun-sky.mjs) and announces each change of phase on the
//  Messages channel "oceanus.daynight". It edits no other entity: the lamps and the underwater
//  zone run their own scripts (sun-sky-lamp.mjs, sun-sky-water.mjs) and work the phase out from
//  the clock as this one does. It never writes userData, which holds the sky shader, nor
//  `locked`.
//
//  The engine shares one module instance per URL between every entity that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import { lightFor, phase, readSunSky, sunPosition } from "./sun-sky.mjs";

export const TICK_MS = 60000;
export const CHANNEL = "oceanus.daynight";

export default class SunSkyServer {
    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {number} */ noonUtc;
    /** @type {number | null} */ timer = null;
    /** @type {string | null} the phase at the last tick */ shownPhase = null;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["userData", "locked"]);
        const { userData, noonUtc, error } = readSunSky(properties.userData);
        if (noonUtc === null) {
            print("Sun sky " + entityID + ": " + error + ". The sky's light does not move.");
            return;
        }
        this.noonUtc = noonUtc;
        if (properties.locked === true) {
            print("Sun sky " + entityID + ": the zone is locked and takes no edit; unlock it and reload its script.");
        }
        const uniforms = userData.ProceduralEntity && userData.ProceduralEntity.uniforms;
        if (uniforms && typeof uniforms.noonUtc === "number" && uniforms.noonUtc !== noonUtc) {
            print("Sun sky " + entityID + ": the sky shader's noonUtc " + uniforms.noonUtc + " is not sunSky.noonUtc "
                + noonUtc + "; the painted sun and the light disagree.");
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

    /** Lights the zone for this moment and announces a change of phase. */
    tick() {
        const sun = sunPosition(Date.now(), this.noonUtc);
        const light = lightFor(sun.elevation, sun.direction);
        const now = phase(sun.elevation);
        Entities.editEntity(this.entityID, {
            keyLight: light.keyLight,
            ambientLight: light.ambientLight,
            haze: light.haze
        });
        // A change seen since the last tick; the phase found at load is no change.
        if (this.shownPhase !== null && now !== this.shownPhase) {
            Messages.sendMessage(CHANNEL, JSON.stringify({ type: "phase", phase: now, azimuth: sun.azimuth, elevation: sun.elevation }));
        }
        this.shownPhase = now;
    }
}
