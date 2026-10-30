//
//  sun-sky-water.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The underwater zone's serverScripts against fake Entities and Script globals: it keeps its
//  own colours and dims them with the daylight.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { SELF, clock, fakeEngine } from "./fake-engine.mjs";
import { daylight } from "../sun-sky.mjs";

const { default: SunSkyWater, TICK_MS, WATER_HAZE_FLOOR, readLook, waterLight } = await import("../sun-sky-water.mjs");

// Oceanus tools/ground.py's underwater zone
const TEAL = {
    noonUtc: 19,
    ambientLight: { ambientIntensity: 0.4, ambientColor: { red: 30, green: 90, blue: 130 } },
    haze: { hazeColor: { red: 20, green: 80, blue: 120 } }
};

function loaded(iso, sunSky = TEAL, locked = false) {
    clock.set(iso);
    const engine = fakeEngine({ name: "Underwater zone", userData: JSON.stringify({ sunSky, kept: true }), locked });
    const water = new SunSkyWater();
    water.preload(SELF);
    return { engine, water };
}

/** @param {{ red: number, green: number, blue: number }} c */
const ratios = c => [c.green / c.red, c.blue / c.red];

test("by day the zone has its own daytime look", () => {
    const { engine } = loaded("2026-10-29T19:00:00Z");
    assert.deepEqual(engine.selfEdits(), [{
        ambientLight: TEAL.ambientLight,
        haze: TEAL.haze
    }]);
    assert.deepEqual([...engine.intervals.values()], [TICK_MS]);
});

test("at night it dims but keeps its teal, and the haze stays above the floor", () => {
    const { engine } = loaded("2026-10-30T03:00:00Z");
    const [edit] = engine.selfEdits();
    assert.equal(daylight(-90), 0.2);
    assert.equal(edit.ambientLight.ambientIntensity, 0.08);
    assert.deepEqual(edit.ambientLight.ambientColor, TEAL.ambientLight.ambientColor);
    const haze = edit.haze.hazeColor;
    assert.deepEqual(haze, { red: 7, green: 28, blue: 42 });
    assert.ok(WATER_HAZE_FLOOR > daylight(-90), "the floor holds at night");
    ratios(haze).forEach((r, i) => assert.ok(Math.abs(r - ratios(TEAL.haze.hazeColor)[i]) < 0.15, "the hue holds"));
    assert.ok(haze.blue > haze.red && haze.green > haze.red, "still teal, not grey");
});

test("it edits only when the light changed, only itself, and never userData or locked", () => {
    const { engine } = loaded("2026-10-29T17:00:00Z");
    const step = (/** @type {string} */ iso) => { clock.set(iso); engine.runTimers(); };
    step("2026-10-29T17:01:00Z");
    step("2026-10-29T21:00:00Z");
    assert.equal(engine.edits.length, 1, "midday holds, so nothing new is written");
    step("2026-10-30T00:50:00Z");
    step("2026-10-30T00:51:00Z");
    assert.equal(engine.edits.length, 3, "around sunset it changes every minute");
    step("2026-10-30T04:00:00Z");
    step("2026-10-30T05:00:00Z");
    assert.equal(engine.edits.length, 4, "the night holds");
    for (const [id, edit] of engine.edits) {
        assert.equal(id, SELF);
        assert.deepEqual(Object.keys(edit).sort(), ["ambientLight", "haze"]);
    }
    assert.equal(JSON.parse(engine.self().userData).kept, true);
});

test("waterLight scales intensity by daylight and the haze by daylight above the floor", () => {
    const look = readLook(TEAL).look;
    assert.ok(look);
    const half = waterLight(look, 0.5);
    assert.equal(half.ambientLight.ambientIntensity, 0.2);
    assert.deepEqual(half.haze.hazeColor, { red: 10, green: 40, blue: 60 });
    assert.deepEqual(waterLight(look, 0).haze.hazeColor, waterLight(look, WATER_HAZE_FLOOR).haze.hazeColor);
    assert.equal(waterLight(look, 0).ambientLight.ambientIntensity, 0);
});

test("daylight is 1 from 30 degrees up and falls with the ambient curve", () => {
    assert.equal(daylight(30), 1);
    assert.equal(daylight(90), 1);
    assert.equal(daylight(15), 0.9);
    assert.equal(daylight(-0.833), 0.44);
    assert.equal(daylight(-6), 0.2);
    let previous = daylight(-30);
    for (let e = -30; e <= 90; e += 0.05) {
        const d = daylight(e);
        assert.ok(d >= previous && d - previous < 0.005, `daylight jumps at ${e}`);
        previous = d;
    }
});

test("a missing noonUtc or daytime look is logged and nothing ticks", () => {
    const broken = [
        null,
        { ...TEAL, noonUtc: undefined },
        { ...TEAL, ambientLight: undefined },
        { ...TEAL, ambientLight: { ambientIntensity: 0.4 } },
        { ...TEAL, ambientLight: { ambientIntensity: "0.4", ambientColor: { red: 1, green: 2, blue: 3 } } },
        { ...TEAL, haze: {} },
        { ...TEAL, haze: { hazeColor: { red: 20, green: 80 } } }
    ];
    for (const sunSky of broken) {
        const { engine } = loaded("2026-10-29T19:00:00Z", sunSky);
        assert.equal(engine.timers.size, 0, JSON.stringify(sunSky));
        assert.equal(engine.edits.length, 0);
        assert.equal(engine.printed.length, 1);
        assert.ok(engine.printed[0].includes(SELF));
        assert.match(engine.printed[0], /userData.sunSky/);
    }
    assert.match(loaded("2026-10-29T19:00:00Z", { ...TEAL, noonUtc: undefined }).engine.printed[0], /noonUtc/);
});

test("unload clears the interval; a locked zone is logged", () => {
    const { engine, water } = loaded("2026-10-29T19:00:00Z");
    water.unload();
    assert.equal(engine.timers.size, 0);
    assert.match(loaded("2026-10-29T19:00:00Z", TEAL, true).engine.printed.join("\n"), /locked/);
});
