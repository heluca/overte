//
//  piano-bench-client.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The bench's client entity script against fake Entities, MyAvatar, Script and Settings
//  globals: the seat request, where beginSit and endSit put the avatar, the desk display calls
//  to the piano, and every way of standing up.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const PIANO = "{9a40d000-0000-4000-8000-000000000000}";
const BENCH = "{b0b0b000-0000-4000-8000-000000000000}";
const SEAT = { x: 0, y: 0.5, z: 1.6 };
const STAND = { x: 0, y: 0, z: 2.05 };
// The bench yawed 180 degrees: its -Z, the sitter's forward, is world +Z
const BENCH_ROTATION = { x: 0, y: 1, z: 0, w: 0 };
const KEYS = { TRANSLATE_X: 0, TRANSLATE_Y: 1, TRANSLATE_Z: 2, STEP_TRANSLATE_Z: 6 };

function signal() {
    const handlers = new Set();
    return { handlers, connect(handler) { handlers.add(handler); }, disconnect(handler) { handlers.delete(handler); } };
}

function fakeEngine({ userData = {}, joints = { seat: 3, stand_up: 4 }, parentID = PIANO } = {}) {
    let nextTimer = 1;
    let now = 1_000_000;
    const poses = { 3: { position: SEAT, rotation: BENCH_ROTATION }, 4: { position: STAND, rotation: BENCH_ROTATION } };
    const engine = {
        serverCalls: [],
        localCalls: [],
        printed: [],
        settings: {},
        keys: {},
        timers: new Map(),
        avatar: { position: { x: 0, y: 0.95, z: 3 }, feetPosition: { x: 0, y: 0.05, z: 3 }, seated: false, sits: [], stands: [] },
        Entities: {
            getEntityProperties(id, names) {
                assert.equal(id, BENCH);
                const bench = { parentID, userData: JSON.stringify(userData) };
                return Object.fromEntries(names.map(name => [name, bench[name]]));
            },
            getJointIndex(id, name) {
                assert.equal(id, BENCH);
                return name in joints ? joints[name] : -1;
            },
            localToWorldPosition(local, id, index) {
                assert.deepEqual(local, { x: 0, y: 0, z: 0 });
                return { ...poses[index].position };
            },
            localToWorldRotation(local, id, index) {
                assert.deepEqual(local, { x: 0, y: 0, z: 0, w: 1 });
                return { ...poses[index].rotation };
            },
            callEntityServerMethod(id, method, params) { engine.serverCalls.push({ id, method, params }); },
            callEntityMethod(id, method, params) { engine.localCalls.push({ id, method, params }); }
        },
        MyAvatar: {
            get position() { return engine.avatar.position; },
            get feetPosition() { return engine.avatar.feetPosition; },
            orientation: { x: 0, y: 0, z: 0, w: 1 },
            getHeight() { return 1.8; },
            isSeated() { return engine.avatar.seated; },
            beginSit(position, rotation) {
                engine.avatar.sits.push({ position, rotation });
                engine.avatar.seated = true;
                engine.avatar.position = position;
            },
            endSit(position, rotation) {
                engine.avatar.stands.push({ position, rotation });
                engine.avatar.seated = false;
                engine.avatar.position = position;
            },
            getRawDriveKey(key) { return engine.keys[key] || 0; },
            onLoadComplete: signal(),
            scaleChanged: signal()
        },
        Script: {
            setTimeout(callback, ms) { engine.timers.set(nextTimer, { callback, at: now + ms }); return nextTimer++; },
            clearTimeout(id) { engine.timers.delete(id); },
            setInterval(callback, ms) { engine.timers.set(nextTimer, { callback, at: now + ms, every: ms }); return nextTimer++; },
            clearInterval(id) { engine.timers.delete(id); }
        },
        Settings: { getValue(key, fallback) { return key in engine.settings ? engine.settings[key] : fallback; } },
        Vec3: { distance(a, b) { return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z); } },
        DriveKeys: KEYS,
        /** Runs the clock forward in small steps, firing due timers. */
        advance(ms) {
            const end = now + ms;
            while (now < end) {
                now = Math.min(end, now + 50);
                for (const [id, timer] of [...engine.timers]) {
                    if (engine.timers.has(id) && timer.at <= now) {
                        if (timer.every) {
                            timer.at += timer.every;
                        } else {
                            engine.timers.delete(id);
                        }
                        timer.callback();
                    }
                }
            }
        },
        calls(method) { return engine.serverCalls.filter(call => call.method === method).length; }
    };
    Object.assign(globalThis, {
        Entities: engine.Entities, MyAvatar: engine.MyAvatar, Script: engine.Script, Settings: engine.Settings,
        Vec3: engine.Vec3, DriveKeys: engine.DriveKeys, print: text => engine.printed.push(String(text))
    });
    Date.now = () => now;
    return engine;
}

const { default: PianoBenchClient } = await import("../piano-bench-client.mjs");
const CLICK = /** @type {any} */ ({ isPrimaryButton: true });

function loaded() {
    const client = new PianoBenchClient();
    client.preload(BENCH);
    return client;
}

function seated(engine) {
    const client = loaded();
    client.clickDownOnEntity(BENCH, CLICK);
    client.seatGranted(BENCH, ["15000"]);
    engine.serverCalls.length = 0;
    engine.localCalls.length = 0;
    return client;
}

function assertNear(actual, expected) {
    for (const key of Object.keys(expected)) {
        assert.ok(Math.abs(actual[key] - expected[key]) < 1e-9, key + ": " + actual[key] + " != " + expected[key]);
    }
}

/** q and -q are the same rotation */
function assertSameRotation(actual, expected) {
    const sign = actual.x * expected.x + actual.y * expected.y + actual.z * expected.z + actual.w * expected.w < 0 ? -1 : 1;
    assertNear({ x: sign * actual.x, y: sign * actual.y, z: sign * actual.z, w: sign * actual.w }, expected);
}

test("a click asks the server for the seat; nothing moves until it is granted", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(BENCH, /** @type {any} */ ({ isPrimaryButton: false }));
    assert.deepEqual(engine.serverCalls, []);
    client.clickDownOnEntity(BENCH, CLICK);
    client.clickDownOnEntity(BENCH, CLICK);
    assert.deepEqual(engine.serverCalls, [{ id: BENCH, method: "claim", params: [] }], "one request at a time");
    assert.equal(engine.avatar.sits.length, 0);
});

test("granted: hips over the seat joint facing the bench's -Z, and the piano opens its display", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(BENCH, CLICK);
    client.seatGranted(BENCH, ["15000"]);
    assert.equal(engine.avatar.sits.length, 1);
    const { position, rotation } = engine.avatar.sits[0];
    assertNear(position, { x: 0, y: 0.5 + 1.8 * 0.06, z: 1.6 });
    assertSameRotation(rotation, BENCH_ROTATION);
    assert.deepEqual(engine.localCalls, [{ id: PIANO, method: "openDisplay", params: [] }]);
    assert.equal(engine.MyAvatar.onLoadComplete.handlers.size, 1);
    // The lease the server granted is renewed a third of the way through
    engine.advance(5000);
    assert.equal(engine.calls("claim"), 2);
    client.seatGranted(BENCH, ["15000"]);
    assert.equal(engine.avatar.sits.length, 1, "a renewal does not sit again");
});

test("clicking again stands up on the stand_up joint at standing height and closes the display", () => {
    const engine = fakeEngine();
    const client = seated(engine);
    client.clickDownOnEntity(BENCH, CLICK);
    assert.equal(engine.avatar.stands.length, 1);
    const { position, rotation } = engine.avatar.stands[0];
    assertNear(position, { x: 0, y: 0.9, z: 2.05 });
    assertSameRotation(rotation, BENCH_ROTATION);
    assert.deepEqual(engine.localCalls, [{ id: PIANO, method: "closeDisplay", params: [] }]);
    assert.deepEqual(engine.serverCalls, [{ id: BENCH, method: "release", params: [] }]);
    assert.equal(engine.timers.size, 0);
    assert.equal(engine.MyAvatar.onLoadComplete.handlers.size, 0);
    assert.equal(engine.MyAvatar.scaleChanged.handlers.size, 0);
});

test("refused: no sit, and a note in the log", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(BENCH, CLICK);
    client.seatRefused();
    assert.equal(engine.avatar.sits.length, 0);
    assert.match(engine.printed.join("\n"), /someone is already sitting here/);
    client.clickDownOnEntity(BENCH, CLICK);
    assert.equal(engine.calls("claim"), 2, "can ask again");
});

test("no answer: the request times out; a late grant gives the seat back", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(BENCH, CLICK);
    engine.advance(3000);
    assert.match(engine.printed.join("\n"), /did not answer; is serverScripts set\?/);
    client.seatGranted(BENCH, ["15000"]);
    assert.equal(engine.avatar.sits.length, 0);
    assert.equal(engine.calls("release"), 1);
});

test("a movement key held for standHoldMs stands up, once released after sitting", () => {
    const engine = fakeEngine({ userData: { pianoBench: { standHoldMs: 300 } } });
    engine.keys[KEYS.TRANSLATE_Z] = 1;
    seated(engine);
    engine.advance(2000);
    assert.equal(engine.avatar.stands.length, 0, "W still held from walking up");
    engine.keys[KEYS.TRANSLATE_Z] = 0;
    engine.advance(200);
    engine.keys[KEYS.TRANSLATE_X] = 1;
    engine.advance(1000);
    assert.equal(engine.avatar.stands.length, 0, "sideways turns the seated avatar");
    engine.keys[KEYS.TRANSLATE_Y] = 1;
    engine.advance(200);
    assert.equal(engine.avatar.stands.length, 0);
    engine.advance(300);
    assert.equal(engine.avatar.stands.length, 1);
    assertNear(engine.avatar.stands[0].position, { x: 0, y: 0.9, z: 2.05 });
});

test("moved off the bench (a teleport): stands up where the avatar is", () => {
    const engine = fakeEngine();
    seated(engine);
    engine.avatar.position = { x: 40, y: 1, z: 40 };
    engine.advance(500);
    assert.equal(engine.avatar.stands.length, 0, "not during the settle time");
    engine.advance(600);
    assert.equal(engine.avatar.stands.length, 1);
    assert.deepEqual(engine.avatar.stands[0].position, { x: 40, y: 1, z: 40 });
    assert.equal(engine.calls("release"), 1);
});

test("a lost lease, an avatar change and unload all stand up", () => {
    let engine = fakeEngine();
    let client = seated(engine);
    client.seatRefused();
    assert.equal(engine.avatar.stands.length, 1);

    engine = fakeEngine();
    seated(engine);
    [...engine.MyAvatar.onLoadComplete.handlers][0]();
    assert.equal(engine.avatar.stands.length, 1);

    engine = fakeEngine();
    client = seated(engine);
    client.unload();
    assert.equal(engine.avatar.stands.length, 1);
    assert.deepEqual(engine.localCalls, [{ id: PIANO, method: "closeDisplay", params: [] }]);
});

test("no seat joints, an avatar seated elsewhere, too far, or Create's edit mode: no request", () => {
    let engine = fakeEngine({ joints: { seat: 3 } });
    loaded().clickDownOnEntity(BENCH, CLICK);
    assert.equal(engine.serverCalls.length, 0);
    assert.match(engine.printed.join("\n"), /no seat and stand_up joints/);

    engine = fakeEngine();
    engine.avatar.seated = true;
    loaded().clickDownOnEntity(BENCH, CLICK);
    assert.equal(engine.serverCalls.length, 0);

    engine = fakeEngine();
    engine.avatar.position = { x: 10, y: 1, z: 10 };
    loaded().clickDownOnEntity(BENCH, CLICK);
    assert.equal(engine.serverCalls.length, 0);

    engine = fakeEngine();
    engine.settings["io.highfidelity.isEditing"] = true;
    loaded().clickDownOnEntity(BENCH, CLICK);
    assert.equal(engine.serverCalls.length, 0);
});

test("a bench with no piano parent says so once", () => {
    const engine = fakeEngine({ parentID: "{00000000-0000-0000-0000-000000000000}" });
    loaded();
    assert.match(engine.printed.join("\n"), /not parented to a piano/);
});

test("the renewal follows the lease the server grants", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(BENCH, CLICK);
    client.seatGranted(BENCH, ["6000"]);
    engine.advance(1950);
    assert.equal(engine.calls("claim"), 1);
    engine.advance(100);
    assert.equal(engine.calls("claim"), 2);
});
