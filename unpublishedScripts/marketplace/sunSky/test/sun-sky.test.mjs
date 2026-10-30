//
//  sun-sky.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The sun's position, the phases and the lighting curve, against ADR 0020's numbers (Oceanus
//  docs/decisions/0020-...) and today's Delos look (Oceanus tools/ground.py).
//

import { test } from "node:test";
import assert from "node:assert/strict";

import {
    DARK_ELEVATION, DAY_HAZE_COLOR, LIGHT_KEYS, NIGHT_AMBIENT_COLOR, NIGHT_AMBIENT_INTENSITY, SUNSET_ELEVATION,
    lightFor, phase, phaseTimes, sunPosition
} from "../sun-sky.mjs";

const NOON = 19;
// Any day will do: every day is the same.
const at = (/** @type {string} */ hhmm, day = "2026-10-29") => Date.parse(`${day}T${hhmm}:00Z`);
const near = (/** @type {number} */ actual, /** @type {number} */ expected, eps = 1e-9) =>
    assert.ok(Math.abs(actual - expected) <= eps, `${actual} is not ${expected}`);
const hhmm = (/** @type {number} */ hours) => {
    const minutes = Math.round(hours * 60) % (24 * 60);
    return `${String(Math.floor(minutes / 60)).padStart(2, "0")}:${String(minutes % 60).padStart(2, "0")}`;
};

test("the sun stands overhead at 19:00Z", () => {
    const sun = sunPosition(at("19:00"), NOON);
    near(sun.hourAngle, 0);
    near(sun.elevation, 90);
    near(sun.direction.x, 0);
    near(sun.direction.y, 1);
    near(sun.direction.z, 0);
});

test("the sun is due east on the horizon at 13:00Z and due west at 01:00Z", () => {
    const morning = sunPosition(at("13:00"), NOON);
    near(morning.hourAngle, -90);
    near(morning.elevation, 0);
    assert.equal(morning.azimuth, 90);
    near(morning.direction.x, 1);
    near(morning.direction.y, 0);
    const evening = sunPosition(at("01:00", "2026-10-30"), NOON);
    near(evening.hourAngle, 90);
    near(evening.elevation, 0);
    assert.equal(evening.azimuth, 270);
    near(evening.direction.x, -1);
    near(evening.direction.y, 0);
});

test("the morning sun is in the east and up, the afternoon sun in the west", () => {
    const morning = sunPosition(at("16:00"), NOON);
    near(morning.elevation, 45);
    assert.equal(morning.azimuth, 90);
    assert.ok(morning.direction.x > 0 && morning.direction.y > 0);
    const afternoon = sunPosition(at("22:00"), NOON);
    near(afternoon.elevation, 45);
    assert.equal(afternoon.azimuth, 270);
    assert.ok(afternoon.direction.x < 0 && afternoon.direction.y > 0);
});

test("the sun is straight underfoot at 07:00Z and every day is the same", () => {
    const midnight = sunPosition(at("07:00"), NOON);
    near(midnight.hourAngle, 180);
    near(midnight.elevation, -90);
    near(midnight.direction.y, -1);
    for (const day of ["2026-01-01", "2026-03-20", "2026-06-21", "2026-12-21", "2031-07-04"]) {
        near(sunPosition(at("15:30", day), NOON).hourAngle, sunPosition(at("15:30"), NOON).hourAngle, 1e-6);
    }
});

test("the direction is a unit vector in the east-up plane, and elevation is 90 - |hour angle|", () => {
    for (let minute = 0; minute < 24 * 60; minute += 7) {
        const sun = sunPosition(at("00:00") + minute * 60000, NOON);
        const { x, y, z } = sun.direction;
        near(Math.hypot(x, y, z), 1, 1e-12);
        assert.equal(z, 0);
        assert.ok(sun.hourAngle > -180 && sun.hourAngle <= 180);
        near(sun.elevation, 90 - Math.abs(sun.hourAngle), 1e-12);
        near(Math.asin(y) * 180 / Math.PI, sun.elevation, 1e-6);
    }
});

test("noon moves with noonUtc", () => {
    near(sunPosition(at("12:00"), 12).elevation, 90);
    near(sunPosition(at("00:00"), 0).elevation, 90);
    near(sunPosition(at("23:30"), 23.5).elevation, 90);
});

test("phases by elevation, at the boundaries", () => {
    assert.equal(phase(90), "day");
    assert.equal(phase(-0.8), "day");
    assert.equal(phase(SUNSET_ELEVATION), "twilight");
    assert.equal(phase(-3), "twilight");
    assert.equal(phase(DARK_ELEVATION), "twilight");
    assert.equal(phase(-6.01), "night");
    assert.equal(phase(-90), "night");
});

test("phaseTimes gives ADR 0020's times at noon 19:00Z", () => {
    const times = phaseTimes(NOON);
    assert.deepEqual(Object.fromEntries(Object.entries(times).map(([name, hours]) => [name, hhmm(hours)])),
        { firstLight: "12:36", sunrise: "12:57", sunset: "01:03", dark: "01:24" });
    near(times.firstLight, 12.6);
    near(times.dark, 1.4);
});

test("the phase changes at ADR 0020's times, to the minute", () => {
    const phaseAt = (/** @type {string} */ time, day = "2026-10-29") => phase(sunPosition(at(time, day), NOON).elevation);
    assert.equal(phaseAt("12:35"), "night");
    assert.equal(phaseAt("12:37"), "twilight");
    assert.equal(phaseAt("12:56"), "twilight");
    assert.equal(phaseAt("12:57"), "day");
    assert.equal(phaseAt("19:00"), "day");
    assert.equal(phaseAt("01:03", "2026-10-30"), "day");
    assert.equal(phaseAt("01:04", "2026-10-30"), "twilight");
    assert.equal(phaseAt("01:23", "2026-10-30"), "twilight");
    assert.equal(phaseAt("01:25", "2026-10-30"), "night");
    assert.equal(phaseAt("07:00"), "night");
});

test("by day the light is today's Delos look at ADR 0014's sun, 50 degrees up", () => {
    const light = lightFor(50, { x: 0.5, y: 0.766, z: 0.4 });
    assert.deepEqual(light.keyLight.color, { red: 255, green: 245, blue: 225 });
    assert.equal(light.keyLight.intensity, 2.5);
    assert.equal(light.keyLight.castShadows, true);
    assert.deepEqual(light.ambientLight, { ambientIntensity: 0.5, ambientColor: { red: 255, green: 255, blue: 255 } });
    assert.deepEqual(light.haze, { hazeColor: { red: 160, green: 184, blue: 197 } });
    assert.deepEqual(lightFor(90, { x: 0, y: 1, z: 0 }).haze.hazeColor, DAY_HAZE_COLOR);
});

test("the key light points from the sun down", () => {
    const sun = sunPosition(at("16:00"), NOON);
    const { direction } = lightFor(sun.elevation, sun.direction).keyLight;
    near(direction.x, -sun.direction.x);
    near(direction.y, -sun.direction.y);
    assert.ok(direction.x < 0 && direction.y < 0, "the morning light shines west and down");
    assert.ok(!Object.is(lightFor(90, { x: 0, y: 1, z: 0 }).keyLight.direction.z, -0));
});

test("below sunset the key light is off and casts no shadow; at night the ambient floor holds", () => {
    const set = lightFor(SUNSET_ELEVATION, { x: -1, y: 0, z: 0 });
    assert.equal(set.keyLight.intensity, 0);
    assert.equal(set.keyLight.castShadows, false);
    const justUp = lightFor(SUNSET_ELEVATION + 0.01, { x: -1, y: 0, z: 0 });
    assert.equal(justUp.keyLight.castShadows, true);
    assert.ok(justUp.keyLight.intensity < 0.01);
    for (const elevation of [-6, -20, -90]) {
        const night = lightFor(elevation, { x: 0, y: -1, z: 0 });
        assert.equal(night.keyLight.intensity, 0);
        assert.equal(night.keyLight.castShadows, false);
        assert.equal(night.ambientLight.ambientIntensity, NIGHT_AMBIENT_INTENSITY);
        assert.deepEqual(night.ambientLight.ambientColor, NIGHT_AMBIENT_COLOR);
    }
    assert.ok(NIGHT_AMBIENT_INTENSITY >= 0.08 && NIGHT_AMBIENT_INTENSITY <= 0.12);
    assert.ok(NIGHT_AMBIENT_COLOR.blue > NIGHT_AMBIENT_COLOR.red, "the night is cool");
});

test("the low sun is warmer and dimmer than the high sun", () => {
    const low = lightFor(5, { x: 1, y: 0.09, z: 0 }).keyLight;
    const high = lightFor(50, { x: 1, y: 0.77, z: 0 }).keyLight;
    assert.ok(low.intensity < high.intensity);
    assert.ok(low.color.blue < high.color.blue && low.color.green < high.color.green);
});

test("the curve is continuous and never falls as the sun rises", () => {
    const toward = { x: 1, y: 0, z: 0 };
    let previous = lightFor(-30, toward);
    for (let elevation = -30; elevation <= 90; elevation += 0.01) {
        const light = lightFor(elevation, toward);
        const jump = Math.abs(light.keyLight.intensity - previous.keyLight.intensity);
        // The one step is the key light turning on at sunset, from nothing.
        assert.ok(jump <= 0.003, `key intensity jumps ${jump} at ${elevation}`);
        assert.ok(Math.abs(light.ambientLight.ambientIntensity - previous.ambientLight.ambientIntensity) <= 0.002);
        for (const group of [["keyLight", "color"], ["ambientLight", "ambientColor"], ["haze", "hazeColor"]]) {
            const [a, b] = [light, previous].map(l => /** @type {any} */ (l)[group[0]][group[1]]);
            for (const channel of ["red", "green", "blue"]) {
                assert.ok(Math.abs(a[channel] - b[channel]) <= 1, `${group.join(".")}.${channel} jumps at ${elevation}`);
            }
        }
        assert.ok(light.keyLight.intensity >= previous.keyLight.intensity);
        assert.ok(light.ambientLight.ambientIntensity >= previous.ambientLight.ambientIntensity);
        previous = light;
    }
});

test("the curve's rows rise in elevation and hold beyond the ends", () => {
    LIGHT_KEYS.slice(1).forEach((row, i) => assert.ok(row.elevation > LIGHT_KEYS[i].elevation));
    assert.deepEqual(lightFor(-45, { x: 0, y: -1, z: 0 }).haze, lightFor(DARK_ELEVATION, { x: 0, y: -1, z: 0 }).haze);
    assert.deepEqual(lightFor(30, { x: 0, y: 1, z: 0 }).ambientLight, lightFor(89, { x: 0, y: 1, z: 0 }).ambientLight);
});
