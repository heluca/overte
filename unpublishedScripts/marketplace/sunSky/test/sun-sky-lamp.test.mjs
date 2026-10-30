//
//  sun-sky-lamp.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A lamp's serverScripts against fake Entities and Script globals.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { SELF, clock, fakeEngine } from "./fake-engine.mjs";

const { default: SunSkyLamp, TICK_MS } = await import("../sun-sky-lamp.mjs");

function loaded(iso, { visible = false, sunSky = { noonUtc: 19 }, locked = false } = {}) {
    clock.set(iso);
    const engine = fakeEngine({ type: "Light", name: "Lamp post lights 01", visible, locked, userData: JSON.stringify({ sunSky }) });
    const lamp = new SunSkyLamp();
    lamp.preload(SELF);
    return { engine, lamp };
}

test("a lamp already right at load is not edited; one that is wrong is put right", () => {
    assert.equal(loaded("2026-10-29T16:00:00Z", { visible: false }).engine.edits.length, 0);
    assert.equal(loaded("2026-10-30T03:00:00Z", { visible: true }).engine.edits.length, 0);
    const { engine } = loaded("2026-10-30T03:00:00Z", { visible: false });
    assert.deepEqual(engine.selfEdits(), [{ visible: true }]);
});

test("it comes on at sunset and goes off at sunrise, one edit each, checking once a minute", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z");
    assert.deepEqual([...engine.intervals.values()], [TICK_MS]);
    const step = (/** @type {string} */ iso) => { clock.set(iso); engine.runTimers(); };
    step("2026-10-30T01:03:00Z");
    assert.equal(engine.edits.length, 0);
    step("2026-10-30T01:04:00Z");
    assert.deepEqual(engine.selfEdits(), [{ visible: true }]);
    for (const iso of ["2026-10-30T01:05:00Z", "2026-10-30T01:30:00Z", "2026-10-30T05:00:00Z", "2026-10-30T12:56:00Z"]) {
        step(iso);
    }
    assert.equal(engine.edits.length, 1, "on through twilight and night, with no edit");
    step("2026-10-30T12:57:00Z");
    assert.deepEqual(engine.selfEdits(), [{ visible: true }, { visible: false }]);
    step("2026-10-30T16:00:00Z");
    assert.equal(engine.edits.length, 2);
    assert.equal(engine.sent.length, 0, "no messages");
});

test("it edits nothing but its own visible", () => {
    const { engine } = loaded("2026-10-30T03:00:00Z");
    for (const [id, edit] of engine.edits) {
        assert.equal(id, SELF);
        assert.deepEqual(Object.keys(edit), ["visible"]);
    }
});

test("unload clears the interval", () => {
    const { engine, lamp } = loaded("2026-10-29T16:00:00Z");
    lamp.unload();
    assert.equal(engine.timers.size, 0);
});

test("a missing noonUtc is logged and nothing ticks; a locked lamp is logged", () => {
    for (const sunSky of [null, {}, { noonUtc: 25 }]) {
        const { engine } = loaded("2026-10-30T03:00:00Z", { sunSky });
        assert.equal(engine.timers.size, 0);
        assert.equal(engine.edits.length, 0);
        assert.equal(engine.printed.length, 1);
        assert.ok(engine.printed[0].includes(SELF));
        assert.match(engine.printed[0], /sunSky/);
    }
    assert.match(loaded("2026-10-29T16:00:00Z", { locked: true }).engine.printed.join("\n"), /locked/);
});
