//
//  sun-sky-server.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The sky zone's serverScripts against fake Entities, Messages and Script globals: what it
//  writes to the world and when, not the sun (sun-sky.test.mjs covers that).
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { SELF, clock, fakeEngine } from "./fake-engine.mjs";
import { lightFor, sunPosition } from "../sun-sky.mjs";

const { default: SunSkyServer, TICK_MS, CHANNEL } = await import("../sun-sky-server.mjs");

const NOON = 19;
const SHADER = { version: 4, shaderUrl: "https://<asset-host>/sunSky/sun-sky.fs", uniforms: { noonUtc: NOON } };

function loaded(iso, sunSky = { noonUtc: NOON }, { locked = false, shader = SHADER } = {}) {
    clock.set(iso);
    const engine = fakeEngine({ name: "Sky zone", userData: JSON.stringify({ ProceduralEntity: shader, sunSky }), locked });
    const server = new SunSkyServer();
    server.preload(SELF);
    return { engine, server };
}

test("preload lights the sky zone at once and then once a minute", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z");
    assert.deepEqual([...engine.intervals.values()], [TICK_MS]);
    assert.equal(TICK_MS, 60000);
    const sun = sunPosition(clock.now, NOON);
    assert.deepEqual(engine.selfEdits(), [{ ...lightFor(sun.elevation, sun.direction) }]);
    clock.set("2026-10-29T16:01:00Z");
    engine.runTimers();
    const edits = engine.selfEdits();
    assert.equal(edits.length, 2);
    assert.ok(edits[1].keyLight.direction.y < edits[0].keyLight.direction.y, "the sun climbed, so its light points further down");
});

test("the edit is the zone's key light, ambient light and haze colour, and nothing else", () => {
    const { engine } = loaded("2026-10-29T21:30:00Z");
    const [edit] = engine.selfEdits();
    assert.deepEqual(Object.keys(edit).sort(), ["ambientLight", "haze", "keyLight"]);
    assert.deepEqual(Object.keys(edit.keyLight).sort(), ["castShadows", "color", "direction", "intensity"]);
    assert.deepEqual(Object.keys(edit.ambientLight).sort(), ["ambientColor", "ambientIntensity"]);
    assert.deepEqual(Object.keys(edit.haze), ["hazeColor"]);
});

test("it edits only its own zone, never userData or locked", () => {
    const { engine } = loaded("2026-10-29T12:00:00Z");
    for (const iso of ["2026-10-29T12:40:00Z", "2026-10-29T13:00:00Z", "2026-10-29T19:00:00Z", "2026-10-30T01:10:00Z", "2026-10-30T01:30:00Z"]) {
        clock.set(iso);
        engine.runTimers();
    }
    assert.equal(engine.edits.length, 6);
    for (const [id, edit] of engine.edits) {
        assert.equal(id, SELF);
        assert.ok(!("userData" in edit), "userData written");
        assert.ok(!("locked" in edit), "locked written");
    }
    assert.deepEqual(JSON.parse(engine.self().userData).ProceduralEntity, SHADER);
});

test("each change of phase is announced once on oceanus.daynight; the phase at load is not", () => {
    const { engine } = loaded("2026-10-29T12:30:00Z");
    assert.equal(engine.sent.length, 0);
    const start = clock.now;
    for (let i = 1; i <= 13 * 60; i++) {
        clock.now = start + i * TICK_MS;
        engine.runTimers();
    }
    assert.equal(CHANNEL, "oceanus.daynight");
    assert.ok(engine.sent.every(sent => sent.channel === CHANNEL));
    assert.deepEqual(engine.sent.map(sent => sent.message.phase), ["twilight", "day", "twilight", "night"]);
    const [twilight, day, dusk, night] = engine.sent.map(sent => sent.message);
    assert.deepEqual(Object.keys(twilight).sort(), ["azimuth", "elevation", "phase", "type"]);
    assert.equal(twilight.type, "phase");
    assert.equal(twilight.azimuth, 90);
    assert.ok(twilight.elevation >= -6 && twilight.elevation < -5.5);
    assert.ok(day.elevation > -0.833 && day.elevation < -0.5);
    assert.equal(dusk.azimuth, 270);
    assert.ok(dusk.elevation <= -0.833 && dusk.elevation > -1.2);
    assert.ok(night.elevation < -6 && night.elevation > -6.3);
});

test("unload clears the interval", () => {
    const { engine, server } = loaded("2026-10-29T16:00:00Z");
    assert.equal(engine.timers.size, 1);
    server.unload();
    assert.equal(engine.timers.size, 0);
    assert.equal(server.timer, null);
    server.unload();
});

test("a missing or unusable noonUtc is logged and nothing ticks", () => {
    for (const sunSky of [null, {}, { noonUtc: "19" }, { noonUtc: 24 }, { noonUtc: -1 }, { noonUtc: NaN }]) {
        const { engine, server } = loaded("2026-10-29T16:00:00Z", sunSky);
        assert.equal(engine.timers.size, 0);
        assert.equal(engine.edits.length, 0);
        assert.equal(engine.printed.length, 1);
        assert.match(engine.printed[0], sunSky ? /noonUtc/ : /userData.sunSky is missing/);
        assert.ok(engine.printed[0].includes(SELF));
        server.unload();
    }
});

test("a locked zone and a shader noon that disagrees are logged", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z", { noonUtc: NOON }, { locked: true });
    assert.match(engine.printed.join("\n"), /locked/);
    const { engine: other } = loaded("2026-10-29T16:00:00Z", { noonUtc: NOON }, { shader: { ...SHADER, uniforms: { noonUtc: 18 } } });
    assert.match(other.printed.join("\n"), /noonUtc 18 is not sunSky.noonUtc 19/);
    assert.equal(other.timers.size, 1, "it still runs on sunSky.noonUtc");
});
