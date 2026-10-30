//
//  sun-sky.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The sun of an equinox day on the equator, without the engine: where the sun stands at a
//  moment, the phase of the day for its elevation, and the sky zone's light for it. Every day is
//  the same: no declination, no equation of time, no daylight saving. Noon is a parameter, the
//  hour of solar noon in UTC; this module holds no default for it.
//
//  World frame: +y up, north -z, east +x. The sun turns about the north-south (z) axis: it rises
//  due east, stands overhead at noon and sets due west.
//
//  Also the scripts' shared reading of userData.sunSky. No engine globals, so it runs under
//  Node, the entity script server and Interface alike. sun-sky.fs repeats the sun's position
//  and the haze column of LIGHT_KEYS; keep the two in step.
//

export const DEGREES_PER_HOUR = 15;
// The sun's centre at sunrise and sunset: refraction and the sun's radius.
export const SUNSET_ELEVATION = -0.833;
// Civil twilight ends.
export const DARK_ELEVATION = -6;

// Today's Delos look under ADR 0014's sun, 50 degrees up (Oceanus tools/ground.py): reached at
// FULL_DAY_ELEVATION and held above it.
export const FULL_DAY_ELEVATION = 30;
export const DAY_KEY_COLOR = Object.freeze({ red: 255, green: 245, blue: 225 });
export const DAY_KEY_INTENSITY = 2.5;
export const DAY_AMBIENT_INTENSITY = 0.5;
export const DAY_AMBIENT_COLOR = Object.freeze({ red: 255, green: 255, blue: 255 });
export const DAY_HAZE_COLOR = Object.freeze({ red: 160, green: 184, blue: 197 });

// The night floor, held below DARK_ELEVATION: enough cool light to walk the island by.
export const NIGHT_AMBIENT_INTENSITY = 0.1;
export const NIGHT_AMBIENT_COLOR = Object.freeze({ red: 150, green: 165, blue: 200 });
export const NIGHT_HAZE_COLOR = Object.freeze({ red: 22, green: 30, blue: 45 });
// The key light is off at night; its colour only keeps the curve continuous.
export const NIGHT_KEY_COLOR = Object.freeze({ red: 255, green: 140, blue: 80 });

/**
 * @typedef {{ red: number, green: number, blue: number }} RGB
 * @typedef {{ x: number, y: number, z: number }} XYZ
 * @typedef {{ elevation: number, keyColor: RGB, keyIntensity: number, ambientIntensity: number,
 *     ambientColor: RGB, hazeColor: RGB }} LightKey
 */

/**
 * The lighting curve: rows by rising elevation, interpolated linearly between rows and held
 * beyond the first and the last.
 * @type {readonly LightKey[]}
 */
export const LIGHT_KEYS = Object.freeze([
    { elevation: DARK_ELEVATION, keyColor: NIGHT_KEY_COLOR, keyIntensity: 0,
        ambientIntensity: NIGHT_AMBIENT_INTENSITY, ambientColor: NIGHT_AMBIENT_COLOR, hazeColor: NIGHT_HAZE_COLOR },
    { elevation: SUNSET_ELEVATION, keyColor: { red: 255, green: 150, blue: 90 }, keyIntensity: 0,
        ambientIntensity: 0.22, ambientColor: { red: 230, green: 190, blue: 190 }, hazeColor: { red: 128, green: 116, blue: 120 } },
    { elevation: 5, keyColor: { red: 255, green: 190, blue: 140 }, keyIntensity: 1.0,
        ambientIntensity: 0.35, ambientColor: { red: 255, green: 225, blue: 205 }, hazeColor: { red: 170, green: 160, blue: 150 } },
    { elevation: 15, keyColor: { red: 255, green: 225, blue: 190 }, keyIntensity: 1.9,
        ambientIntensity: 0.45, ambientColor: { red: 255, green: 245, blue: 238 }, hazeColor: { red: 165, green: 180, blue: 190 } },
    { elevation: FULL_DAY_ELEVATION, keyColor: DAY_KEY_COLOR, keyIntensity: DAY_KEY_INTENSITY,
        ambientIntensity: DAY_AMBIENT_INTENSITY, ambientColor: DAY_AMBIENT_COLOR, hazeColor: DAY_HAZE_COLOR }
]);

const RAD = Math.PI / 180;
const MS_PER_HOUR = 3600000;

/**
 * @param {number} value
 * @param {number} period
 * @returns {number} value modulo period, in [0, period)
 */
function wrap(value, period) {
    return ((value % period) + period) % period;
}

/**
 * The sun at a moment.
 * @param {number} msSinceEpoch milliseconds since 1970-01-01T00:00Z, as Date.now()
 * @param {number} noonUtc the hour of solar noon in UTC, 0 to 24
 * @returns {{ hourAngle: number, azimuth: number, elevation: number, direction: XYZ }}
 *     hourAngle in degrees, (-180, 180], negative before noon; azimuth the compass bearing in
 *     degrees clockwise from north: 90 (east) from midnight to noon, 270 (west) after; elevation
 *     in degrees, 90 - |hourAngle|; direction the unit vector toward the sun in the world frame
 */
export function sunPosition(msSinceEpoch, noonUtc) {
    const hours = wrap(msSinceEpoch / MS_PER_HOUR, 24);
    const hourAngle = 180 - wrap(180 - DEGREES_PER_HOUR * (hours - noonUtc), 360);
    const h = hourAngle * RAD;
    return {
        hourAngle,
        // On the meridian (noon and midnight) the bearing is undefined; the morning's is kept.
        azimuth: hourAngle > 0 ? 270 : 90,
        elevation: 90 - Math.abs(hourAngle),
        direction: { x: -Math.sin(h), y: Math.cos(h), z: 0 }
    };
}

/**
 * @param {number} elevation the sun's elevation in degrees
 * @returns {"day" | "twilight" | "night"}
 */
export function phase(elevation) {
    if (elevation > SUNSET_ELEVATION) {
        return "day";
    }
    return elevation >= DARK_ELEVATION ? "twilight" : "night";
}

/**
 * The daily UTC times at which the phase changes, as hours in [0, 24).
 * @param {number} noonUtc
 * @returns {{ firstLight: number, sunrise: number, sunset: number, dark: number }}
 */
export function phaseTimes(noonUtc) {
    const fromNoon = (/** @type {number} */ elevation) => (90 - elevation) / DEGREES_PER_HOUR;
    return {
        firstLight: wrap(noonUtc - fromNoon(DARK_ELEVATION), 24),
        sunrise: wrap(noonUtc - fromNoon(SUNSET_ELEVATION), 24),
        sunset: wrap(noonUtc + fromNoon(SUNSET_ELEVATION), 24),
        dark: wrap(noonUtc + fromNoon(DARK_ELEVATION), 24)
    };
}

/**
 * @param {number} a
 * @param {number} b
 * @param {number} t
 */
function mix(a, b, t) {
    return a + (b - a) * t;
}

/**
 * @param {RGB} a
 * @param {RGB} b
 * @param {number} t
 * @returns {RGB}
 */
function mixColor(a, b, t) {
    return {
        red: Math.round(mix(a.red, b.red, t)),
        green: Math.round(mix(a.green, b.green, t)),
        blue: Math.round(mix(a.blue, b.blue, t))
    };
}

/**
 * The curve's row below an elevation, the row above, and how far between them it lies.
 * @param {number} elevation
 * @returns {[LightKey, LightKey, number]}
 */
function bracket(elevation) {
    const last = LIGHT_KEYS.length - 1;
    if (elevation <= LIGHT_KEYS[0].elevation) {
        return [LIGHT_KEYS[0], LIGHT_KEYS[0], 0];
    }
    for (let i = 1; i <= last; i++) {
        const below = LIGHT_KEYS[i - 1];
        const above = LIGHT_KEYS[i];
        if (elevation <= above.elevation) {
            return [below, above, (elevation - below.elevation) / (above.elevation - below.elevation)];
        }
    }
    return [LIGHT_KEYS[last], LIGHT_KEYS[last], 0];
}

/**
 * How much daylight there is, 0 to 1: the ambient intensity curve over its daytime value, so 1
 * from FULL_DAY_ELEVATION up and NIGHT_AMBIENT_INTENSITY / DAY_AMBIENT_INTENSITY (0.2) at night.
 * Rounded to 3 decimals. For zones with their own look, which scale it by this.
 * @param {number} elevation the sun's elevation in degrees
 * @returns {number}
 */
export function daylight(elevation) {
    const [below, above, t] = bracket(elevation);
    return Math.round(mix(below.ambientIntensity, above.ambientIntensity, t) / DAY_AMBIENT_INTENSITY * 1000) / 1000;
}

/**
 * The sky zone's light for the sun at an elevation. Intensities are rounded to 3 decimals and
 * colours to whole units, so a minute's change that shows nothing writes nothing new.
 * @param {number} elevation the sun's elevation in degrees
 * @param {XYZ} towardSun the unit vector toward the sun (sunPosition's direction)
 * @returns {{ keyLight: { direction: XYZ, color: RGB, intensity: number, castShadows: boolean },
 *     ambientLight: { ambientIntensity: number, ambientColor: RGB }, haze: { hazeColor: RGB } }}
 */
export function lightFor(elevation, towardSun) {
    const [below, above, t] = bracket(elevation);
    const up = elevation > SUNSET_ELEVATION;
    return {
        keyLight: {
            // + 0 turns -0 into 0
            direction: { x: -towardSun.x + 0, y: -towardSun.y + 0, z: -towardSun.z + 0 },
            color: mixColor(below.keyColor, above.keyColor, t),
            intensity: up ? Math.round(mix(below.keyIntensity, above.keyIntensity, t) * 1000) / 1000 : 0,
            castShadows: up
        },
        ambientLight: {
            ambientIntensity: Math.round(mix(below.ambientIntensity, above.ambientIntensity, t) * 1000) / 1000,
            ambientColor: mixColor(below.ambientColor, above.ambientColor, t)
        },
        haze: { hazeColor: mixColor(below.hazeColor, above.hazeColor, t) }
    };
}

/**
 * Reads an entity's userData for the scripts: the `sunSky` object and its noon, which has no
 * default.
 * @param {unknown} text the entity's userData
 * @returns {{ userData: Record<string, any>, sunSky: Record<string, any>, noonUtc: number, error: null }
 *     | { userData: Record<string, any>, sunSky: null, noonUtc: null, error: string }}
 *     the parsed userData (an empty object if it is not a JSON object) and the sunSky object, or
 *     why it cannot be used
 */
export function readSunSky(text) {
    /** @type {Record<string, any>} */
    let userData = {};
    try {
        const value = JSON.parse(typeof text === "string" ? text : "");
        userData = value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
    } catch (e) {
        userData = {};
    }
    const sunSky = userData.sunSky;
    if (sunSky === null || typeof sunSky !== "object" || Array.isArray(sunSky)) {
        return { userData, sunSky: null, noonUtc: null, error: "userData.sunSky is missing" };
    }
    const noonUtc = sunSky.noonUtc;
    if (typeof noonUtc !== "number" || !(noonUtc >= 0 && noonUtc < 24)) {
        return { userData, sunSky: null, noonUtc: null,
            error: "userData.sunSky.noonUtc must be the hour of solar noon in UTC, 0 to 24; got " + JSON.stringify(noonUtc) };
    }
    return { userData, sunSky, noonUtc, error: null };
}
