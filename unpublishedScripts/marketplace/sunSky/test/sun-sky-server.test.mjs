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

import { test, afterEach } from "node:test";
import assert from "node:assert/strict";

import { SKY, fakeEngine } from "./fake-engine.mjs";
import { lightFor, sunPosition } from "../sun-sky.mjs";

const { default: SunSkyServer, TICK_MS, CHANNEL, readConfig } = await import("../sun-sky-server.mjs");

const NOON = 19;
const realNow = Date.now;
let clock = 0;
const setClock = (/** @type {string} */ iso) => { clock = Date.parse(iso); };
Date.now = () => clock;
afterEach(() => { clock = 0; });
process.on("exit", () => { Date.now = realNow; });

const SHADER = { version: 4, shaderUrl: "https://<asset-host>/sunSky/sun-sky.fs", uniforms: { noonUtc: NOON } };

function userData(sunSky = {}) {
    return JSON.stringify({
        ProceduralEntity: SHADER,
        sunSky: { noonUtc: NOON, zoneNames: ["Underwater zone"], lampNames: ["Lamp post lights 01", "Lamp post lights 02"], searchRadius: 2000, ...sunSky }
    });
}

/** A sky zone, an underwater zone and two lamps, loaded at a moment. */
function loaded(iso, sunSky = {}, locked = false) {
    setClock(iso);
    const engine = fakeEngine(userData(sunSky), locked);
    const water = engine.add({ name: "Underwater zone", userData: "{\"kept\":true}", visible: true });
    const lamps = [engine.add({ name: "Lamp post lights 01", visible: false }), engine.add({ name: "Lamp post lights 02", visible: false })];
    const server = new SunSkyServer();
    server.preload(SKY);
    return { engine, server, water, lamps };
}

test("preload lights the sky zone at once and then once a minute", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z");
    assert.deepEqual([...engine.intervals.values()], [TICK_MS]);
    assert.equal(TICK_MS, 60000);
    const sun = sunPosition(clock, NOON);
    assert.deepEqual(engine.editsTo(SKY), [{ ...lightFor(sun.elevation, sun.direction) }]);
    setClock("2026-10-29T16:01:00Z");
    engine.runTimers();
    const edits = engine.editsTo(SKY);
    assert.equal(edits.length, 2);
    assert.ok(edits[1].keyLight.direction.y < edits[0].keyLight.direction.y, "the sun climbed, so its light points further down");
});

test("the sky zone's edit is its key light, ambient light and haze colour, and nothing else", () => {
    const { engine } = loaded("2026-10-29T21:30:00Z");
    const [edit] = engine.editsTo(SKY);
    assert.deepEqual(Object.keys(edit).sort(), ["ambientLight", "haze", "keyLight"]);
    assert.deepEqual(Object.keys(edit.keyLight).sort(), ["castShadows", "color", "direction", "intensity"]);
    assert.deepEqual(Object.keys(edit.ambientLight).sort(), ["ambientColor", "ambientIntensity"]);
    assert.deepEqual(Object.keys(edit.haze), ["hazeColor"]);
});

test("no edit touches userData or locked", () => {
    const { engine } = loaded("2026-10-29T12:00:00Z");
    for (const time of ["12:40", "13:00", "19:00", "01:10", "01:30"]) {
        setClock(`2026-10-${time < "12" ? "30" : "29"}T${time}:00Z`);
        engine.runTimers();
    }
    assert.ok(engine.edits.length > 10);
    for (const [, properties] of engine.edits) {
        assert.ok(!("userData" in properties), "userData written");
        assert.ok(!("locked" in properties), "locked written");
    }
    assert.deepEqual(JSON.parse(engine.entities.get(SKY).userData).ProceduralEntity, SHADER);
});

test("the other zones get the ambient light and haze colour only, and only when they change", () => {
    const { engine, water } = loaded("2026-10-29T16:00:00Z");
    const [first] = engine.editsTo(water);
    assert.deepEqual(Object.keys(first).sort(), ["ambientLight", "haze"]);
    assert.deepEqual(first.ambientLight, engine.editsTo(SKY)[0].ambientLight);
    assert.deepEqual(first.haze, { hazeColor: engine.editsTo(SKY)[0].haze.hazeColor });
    // Midday: the light holds, so nothing new is written to the zone
    setClock("2026-10-29T16:01:00Z");
    engine.runTimers();
    assert.equal(engine.editsTo(water).length, 1);
    // Sunset: it changes every minute
    setClock("2026-10-30T00:50:00Z");
    engine.runTimers();
    setClock("2026-10-30T00:51:00Z");
    engine.runTimers();
    assert.equal(engine.editsTo(water).length, 3);
    assert.equal(JSON.parse(engine.entities.get(water).userData).kept, true);
});

test("searches are by exact name within searchRadius of the sky zone, and never return the sky zone", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z", { zoneNames: ["Sky zone", "Underwater zone"] });
    assert.ok(engine.searches.length >= 3);
    for (const [, center, radius, caseSensitive] of engine.searches) {
        assert.deepEqual(center, { x: 0, y: 100, z: 0 });
        assert.equal(radius, 2000);
        assert.equal(caseSensitive, true);
    }
    assert.equal(engine.editsTo(SKY).length, 1, "the sky zone named in zoneNames is not edited twice");
});

test("a zone that arrives late is lit at the next tick", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z", { zoneNames: ["Room zone"] });
    const room = engine.add({ name: "Room zone" });
    assert.equal(engine.editsTo(room).length, 0);
    setClock("2026-10-29T16:01:00Z");
    engine.runTimers();
    assert.equal(engine.editsTo(room).length, 1);
});

test("the lamps are switched off by day, on from sunset, and off again at sunrise, one edit each", () => {
    const { engine, lamps } = loaded("2026-10-29T16:00:00Z");
    lamps.forEach(id => assert.equal(engine.editsTo(id).length, 0, "already off by day"));
    setClock("2026-10-30T01:03:00Z");
    engine.runTimers();
    lamps.forEach(id => assert.equal(engine.entities.get(id).visible, false));
    setClock("2026-10-30T01:04:00Z");
    engine.runTimers();
    lamps.forEach(id => assert.deepEqual(engine.editsTo(id), [{ visible: true }]));
    for (const time of ["01:30", "05:00", "12:40"]) {
        setClock(`2026-10-30T${time}:00Z`);
        engine.runTimers();
    }
    lamps.forEach(id => assert.equal(engine.editsTo(id).length, 1, "on through twilight and night"));
    setClock("2026-10-30T12:57:00Z");
    engine.runTimers();
    lamps.forEach(id => assert.deepEqual(engine.editsTo(id), [{ visible: true }, { visible: false }]));
});

test("lamps found at load are switched for the phase at load", () => {
    const { engine, lamps } = loaded("2026-10-30T03:00:00Z");
    lamps.forEach(id => assert.deepEqual(engine.editsTo(id), [{ visible: true }]));
});

test("each change of phase is announced once on oceanus.daynight; the phase at load is not", () => {
    const { engine } = loaded("2026-10-29T12:30:00Z");
    assert.equal(engine.sent.length, 0);
    const minutes = (/** @type {string} */ from, /** @type {number} */ count) => {
        for (let i = 1; i <= count; i++) {
            clock = Date.parse(from) + i * TICK_MS;
            engine.runTimers();
        }
    };
    minutes("2026-10-29T12:30:00Z", 13 * 60);
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

test("a missing noonUtc is logged and nothing ticks", () => {
    for (const sunSky of [{ noonUtc: undefined }, { noonUtc: "19" }, { noonUtc: 24 }, { noonUtc: -1 }, { noonUtc: NaN }]) {
        const { engine, server } = loaded("2026-10-29T16:00:00Z", sunSky);
        assert.equal(engine.timers.size, 0);
        assert.equal(engine.edits.length, 0);
        assert.equal(engine.printed.length, 1);
        assert.match(engine.printed[0], /noonUtc/);
        assert.match(engine.printed[0], new RegExp(SKY.replace(/[{}]/g, "\\$&")));
        server.unload();
    }
    setClock("2026-10-29T16:00:00Z");
    const engine = fakeEngine(JSON.stringify({ ProceduralEntity: SHADER }));
    new SunSkyServer().preload(SKY);
    assert.equal(engine.timers.size, 0);
    assert.match(engine.printed[0], /userData.sunSky is missing/);
});

test("names without a searchRadius are a configuration error; no names need none", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z", { searchRadius: undefined });
    assert.equal(engine.timers.size, 0);
    assert.match(engine.printed[0], /searchRadius/);
    assert.deepEqual(readConfig({ sunSky: { noonUtc: 19 } }),
        { config: { noonUtc: 19, zoneNames: [], lampNames: [], searchRadius: undefined }, error: null });
    assert.match(String(readConfig({ sunSky: { noonUtc: 19, lampNames: "Lamp" } }).error), /lists of entity names/);
    assert.match(String(readConfig({ sunSky: { noonUtc: 19, zoneNames: [""] , searchRadius: 1 } }).error), /lists of entity names/);
});

test("a locked zone and a shader noon that disagrees are logged", () => {
    const { engine } = loaded("2026-10-29T16:00:00Z", {}, true);
    assert.match(engine.printed.join("\n"), /locked/);
    setClock("2026-10-29T16:00:00Z");
    const other = fakeEngine(JSON.stringify({ ProceduralEntity: { ...SHADER, uniforms: { noonUtc: 18 } }, sunSky: { noonUtc: NOON } }));
    new SunSkyServer().preload(SKY);
    assert.match(other.printed.join("\n"), /noonUtc 18 is not sunSky.noonUtc 19/);
    assert.equal(other.timers.size, 1, "it still runs on sunSky.noonUtc");
});
