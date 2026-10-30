//
//  sun-sky-water.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The `serverScripts` of a zone with a look of its own, the underwater zone: it keeps its own
//  colours and dims them with the daylight (sun-sky.mjs daylight), never taking the sky's.
//  userData.sunSky gives the noon and the zone's daytime ambient light and haze colour; once a
//  minute it sets ambientIntensity to the daytime value times the daylight, and the haze colour
//  to the daytime colour times the daylight, never below WATER_HAZE_FLOOR. It edits only its
//  own zone, only when the values change, and never userData or `locked`.
//
//  The engine shares one module instance per URL between every entity that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import { daylight, readSunSky, sunPosition } from "./sun-sky.mjs";

export const TICK_MS = 60000;
// The haze never dims below this share of its daytime colour, so the water never goes black.
export const WATER_HAZE_FLOOR = 0.35;

/**
 * @typedef {{ red: number, green: number, blue: number }} RGB
 * @typedef {{ ambientIntensity: number, ambientColor: RGB, hazeColor: RGB }} WaterLook
 */

/**
 * @param {unknown} value
 * @returns {value is RGB}
 */
function isColor(value) {
    const color = /** @type {Record<string, unknown>} */ (value);
    return value !== null && typeof value === "object"
        && ["red", "green", "blue"].every(c => typeof color[c] === "number" && /** @type {number} */ (color[c]) >= 0 && /** @type {number} */ (color[c]) <= 255);
}

/**
 * The zone's daytime look from userData.sunSky.
 * @param {Record<string, any>} sunSky
 * @returns {{ look: WaterLook, error: null } | { look: null, error: string }}
 */
export function readLook(sunSky) {
    const ambientLight = sunSky.ambientLight || {};
    const haze = sunSky.haze || {};
    if (!(typeof ambientLight.ambientIntensity === "number" && ambientLight.ambientIntensity >= 0) || !isColor(ambientLight.ambientColor)) {
        return { look: null, error: "userData.sunSky.ambientLight must give the zone's daytime ambientIntensity and ambientColor" };
    }
    if (!isColor(haze.hazeColor)) {
        return { look: null, error: "userData.sunSky.haze must give the zone's daytime hazeColor" };
    }
    return { look: { ambientIntensity: ambientLight.ambientIntensity, ambientColor: ambientLight.ambientColor, hazeColor: haze.hazeColor }, error: null };
}

/**
 * The zone's light for an amount of daylight: its own colours, dimmed.
 * @param {WaterLook} look the daytime look
 * @param {number} light daylight, 0 to 1
 * @returns {{ ambientLight: { ambientIntensity: number, ambientColor: RGB }, haze: { hazeColor: RGB } }}
 */
export function waterLight(look, light) {
    const hazeScale = Math.max(light, WATER_HAZE_FLOOR);
    const { red, green, blue } = look.hazeColor;
    return {
        ambientLight: {
            ambientIntensity: Math.round(look.ambientIntensity * light * 1000) / 1000,
            ambientColor: { red: look.ambientColor.red, green: look.ambientColor.green, blue: look.ambientColor.blue }
        },
        haze: { hazeColor: { red: Math.round(red * hazeScale), green: Math.round(green * hazeScale), blue: Math.round(blue * hazeScale) } }
    };
}

export default class SunSkyWater {
    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {number} */ noonUtc;
    /** @type {WaterLook} */ look;
    /** @type {number | null} */ timer = null;
    /** @type {string | null} the light last written */ written = null;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["userData", "locked"]);
        const { sunSky, noonUtc, error } = readSunSky(properties.userData);
        const { look, error: lookError } = sunSky ? readLook(sunSky) : { look: null, error };
        if (noonUtc === null || look === null) {
            print("Sun sky water " + entityID + ": " + lookError + ". The zone's light does not change.");
            return;
        }
        this.noonUtc = noonUtc;
        this.look = look;
        if (properties.locked === true) {
            print("Sun sky water " + entityID + ": the zone is locked and takes no edit; unlock it and reload its script.");
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

    /** Dims the zone for this moment, if that changed anything. */
    tick() {
        const light = waterLight(this.look, daylight(sunPosition(Date.now(), this.noonUtc).elevation));
        const written = JSON.stringify(light);
        if (written !== this.written) {
            Entities.editEntity(this.entityID, light);
            this.written = written;
        }
    }
}
