//
//  sun-sky-server.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The sky zone's `serverScripts`, run by the entity script server: once a minute it lights the
//  zone for where the sun stands (sun-sky.mjs), sets the ambient light and haze colour of the
//  other zones named in userData.sunSky.zoneNames, switches the lamps named in lampNames on
//  while the sun is down, and announces each change of phase on the Messages channel
//  "oceanus.daynight". It never writes userData, which holds the sky shader, nor `locked`.
//
//  The engine shares one module instance per URL between every entity that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import { lightFor, phase, sunPosition } from "./sun-sky.mjs";

export const TICK_MS = 60000;
export const CHANNEL = "oceanus.daynight";

/**
 * @param {unknown} text
 * @returns {Record<string, any>} the parsed object, or an empty one for anything else
 */
function parseObject(text) {
    try {
        const value = JSON.parse(typeof text === "string" ? text : "");
        return value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
    } catch (e) {
        return {};
    }
}

/**
 * @param {unknown} value
 * @returns {value is string[]}
 */
function isNameList(value) {
    return Array.isArray(value) && value.every(name => typeof name === "string" && name !== "");
}

/**
 * Reads userData.sunSky. Nothing has a default but the two name lists, which default to none.
 * @param {Record<string, any>} userData
 * @returns {{ config: { noonUtc: number, zoneNames: string[], lampNames: string[], searchRadius: number } | null,
 *     error: string | null }}
 */
export function readConfig(userData) {
    const sunSky = userData.sunSky;
    if (sunSky === null || typeof sunSky !== "object") {
        return { config: null, error: "userData.sunSky is missing" };
    }
    const { noonUtc, zoneNames = [], lampNames = [], searchRadius } = sunSky;
    if (typeof noonUtc !== "number" || !(noonUtc >= 0 && noonUtc < 24)) {
        return { config: null, error: "userData.sunSky.noonUtc must be the hour of solar noon in UTC, 0 to 24; got " + JSON.stringify(noonUtc) };
    }
    if (!isNameList(zoneNames) || !isNameList(lampNames)) {
        return { config: null, error: "userData.sunSky.zoneNames and lampNames must be lists of entity names" };
    }
    if ((zoneNames.length || lampNames.length) && !(typeof searchRadius === "number" && searchRadius > 0)) {
        return { config: null, error: "userData.sunSky.searchRadius must be a positive number of metres to find zoneNames and lampNames; got " + JSON.stringify(searchRadius) };
    }
    return { config: { noonUtc, zoneNames, lampNames, searchRadius }, error: null };
}

export default class SunSkyServer {
    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {Vec3} */ position;
    /** @type {{ noonUtc: number, zoneNames: string[], lampNames: string[], searchRadius: number } | null} */ config = null;
    /** @type {number | null} */ timer = null;
    /** @type {string | null} the phase at the last tick */ shownPhase = null;
    /** @type {Map<Uuid, string>} other zone -> the ambient and haze last written to it */ zoneLight = new Map();

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["userData", "position", "locked"]);
        this.position = /** @type {Vec3} */ (properties.position);
        const userData = parseObject(properties.userData);
        const { config, error } = readConfig(userData);
        if (!config) {
            print("Sun sky " + entityID + ": " + error + ". The sky's light does not move.");
            return;
        }
        this.config = config;
        if (properties.locked === true) {
            print("Sun sky " + entityID + ": the zone is locked and takes no edit; unlock it and reload its script.");
        }
        const uniforms = userData.ProceduralEntity && userData.ProceduralEntity.uniforms;
        if (uniforms && typeof uniforms.noonUtc === "number" && uniforms.noonUtc !== config.noonUtc) {
            print("Sun sky " + entityID + ": the sky shader's noonUtc " + uniforms.noonUtc + " is not sunSky.noonUtc "
                + config.noonUtc + "; the painted sun and the light disagree.");
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

    /** Lights the zones and the lamps for this moment. */
    tick() {
        const config = /** @type {NonNullable<SunSkyServer["config"]>} */ (this.config);
        const sun = sunPosition(Date.now(), config.noonUtc);
        const light = lightFor(sun.elevation, sun.direction);
        const now = phase(sun.elevation);
        Entities.editEntity(this.entityID, {
            keyLight: light.keyLight,
            ambientLight: light.ambientLight,
            haze: light.haze
        });
        this.lightZones(light.ambientLight, light.haze);
        this.switchLamps(now !== "day");
        // A change seen since the last tick; the phase found at load is no change.
        if (this.shownPhase !== null && now !== this.shownPhase) {
            Messages.sendMessage(CHANNEL, JSON.stringify({
                type: "phase", phase: now, azimuth: sun.azimuth, elevation: sun.elevation
            }));
        }
        this.shownPhase = now;
    }

    /**
     * @param {string} name
     * @returns {Uuid[]}
     */
    find(name) {
        const config = /** @type {NonNullable<SunSkyServer["config"]>} */ (this.config);
        return Entities.findEntitiesByName(name, this.position, config.searchRadius, true)
            .filter(id => id !== this.entityID);
    }

    /**
     * The other zones take the sky zone's ambient light and haze colour, and nothing else. Each
     * is written only when that changed, and found again every tick, so one that arrives late
     * or is rebuilt is lit at the next.
     * @param {{ ambientIntensity: number, ambientColor: { red: number, green: number, blue: number } }} ambientLight
     * @param {{ hazeColor: { red: number, green: number, blue: number } }} haze
     */
    lightZones(ambientLight, haze) {
        const config = /** @type {NonNullable<SunSkyServer["config"]>} */ (this.config);
        const written = JSON.stringify([ambientLight, haze]);
        config.zoneNames.forEach(name => this.find(name).forEach(id => {
            if (this.zoneLight.get(id) !== written) {
                Entities.editEntity(id, { ambientLight, haze });
                this.zoneLight.set(id, written);
            }
        }));
    }

    /**
     * Shows the lamps while the sun is down and hides them by day, editing only a lamp whose
     * `visible` is not already right: one edit per lamp at sunset and at sunrise.
     * @param {boolean} on
     */
    switchLamps(on) {
        const config = /** @type {NonNullable<SunSkyServer["config"]>} */ (this.config);
        config.lampNames.forEach(name => this.find(name).forEach(id => {
            if (Entities.getEntityProperties(id, ["visible"]).visible !== on) {
                Entities.editEntity(id, { visible: on });
            }
        }));
    }
}
