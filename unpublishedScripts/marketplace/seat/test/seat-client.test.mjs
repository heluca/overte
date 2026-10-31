//
//  seat-client.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The seat's client entity script against fake Entities, MyAvatar, Script and Settings globals,
//  on a Delos bench (three seats): which seats are asked for and in what order, where beginSit
//  and endSit put the avatar, the lease, and every way of standing up.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const MODEL = "{5ea75ea7-0000-4000-8000-000000000000}";
// The bench at (10, 0, 20), unrotated; its joints carry a half turn, so its sitters face +Z
const BENCH_AT = { x: 10, y: 0, z: 20 };
const HALF_TURN = { x: 0, y: 1, z: 0, w: 0 };
const SEAT_X = [-0.52, 0, 0.52];
const KEYS = { TRANSLATE_X: 0, TRANSLATE_Y: 1, TRANSLATE_Z: 2, STEP_TRANSLATE_Z: 6 };
const DELOS_BENCH = { seat_1: 1, seat_2: 2, seat_3: 3, stand_1: 4, stand_2: 5, stand_3: 6 };
const STANDING = { x: 10.8, y: 0.95, z: 21.2 };  // in front of seat_3, nearer it than the others

function signal() {
    const handlers = new Set();
    return { handlers, connect(handler) { handlers.add(handler); }, disconnect(handler) { handlers.delete(handler); } };
}

/** The Delos bench's joints in the model's frame, placed in the world */
function delosPoses() {
    /** @type {Record<number, { position: object, rotation: object }>} */
    const poses = {};
    SEAT_X.forEach((x, i) => {
        poses[i + 1] = { position: { x: BENCH_AT.x + x, y: 0.4385, z: BENCH_AT.z + 0.0503 }, rotation: HALF_TURN };
        poses[i + 4] = { position: { x: BENCH_AT.x + x, y: 0, z: BENCH_AT.z + 0.72 }, rotation: HALF_TURN };
    });
    return poses;
}

function fakeEngine({ userData = {}, joints = DELOS_BENCH } = {}) {
    let nextTimer = 1;
    let now = 1_000_000;
    const poses = delosPoses();
    const engine = {
        serverCalls: [],
        printed: [],
        settings: {},
        keys: {},
        timers: new Map(),
        avatar: {
            position: { ...STANDING }, feetPosition: { ...STANDING, y: 0.05 }, seated: false, sits: [], stands: []
        },
        Entities: {
            getEntityProperties(id, names) {
                assert.equal(id, MODEL);
                assert.deepEqual(names, ["userData"]);
                return { userData: JSON.stringify(userData) };
            },
            getJointIndex(id, name) {
                assert.equal(id, MODEL);
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
            callEntityServerMethod(id, method, params) { engine.serverCalls.push({ id, method, params }); }
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
        calls(method) { return engine.serverCalls.filter(call => call.method === method); }
    };
    Object.assign(globalThis, {
        Entities: engine.Entities, MyAvatar: engine.MyAvatar, Script: engine.Script, Settings: engine.Settings,
        Vec3: engine.Vec3, DriveKeys: engine.DriveKeys, print: text => engine.printed.push(String(text))
    });
    Date.now = () => now;
    return engine;
}

const { default: SeatClient } = await import("../seat-client.mjs");
const CLICK = /** @type {any} */ ({ isPrimaryButton: true });
const STAND_3 = { x: 10.52, y: 0.9, z: 20.72 };

function loaded() {
    const client = new SeatClient();
    client.preload(MODEL);
    return client;
}

function seated(engine) {
    const client = loaded();
    client.clickDownOnEntity(MODEL, CLICK);
    client.seatGranted(MODEL, ["seat_3", "15000"]);
    engine.serverCalls.length = 0;
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

test("a click asks for the seats nearest first; nothing moves until one is granted", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(MODEL, /** @type {any} */ ({ isPrimaryButton: false }));
    assert.deepEqual(engine.serverCalls, []);
    client.clickDownOnEntity(MODEL, CLICK);
    client.clickDownOnEntity(MODEL, CLICK);
    assert.deepEqual(engine.serverCalls, [{ id: MODEL, method: "claim", params: ["seat_3", "seat_2", "seat_1"] }],
        "one request at a time");
    assert.equal(engine.avatar.sits.length, 0);
});

test("granted seat_3: hips over its joint, facing +Z with the joint's half turn", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(MODEL, CLICK);
    client.seatGranted(MODEL, ["seat_3", "15000"]);
    assert.equal(engine.avatar.sits.length, 1);
    const { position, rotation } = engine.avatar.sits[0];
    assertNear(position, { x: 10.52, y: 0.4385 + 1.8 * 0.06, z: 20.0503 });
    assertSameRotation(rotation, HALF_TURN);
    assert.equal(engine.MyAvatar.onLoadComplete.handlers.size, 1);
    assert.equal(engine.MyAvatar.scaleChanged.handlers.size, 1);
});

test("the lease is renewed a third of the way through, for the held seat only", () => {
    const engine = fakeEngine();
    const client = seated(engine);
    engine.advance(5000);
    assert.deepEqual(engine.calls("claim"), [{ id: MODEL, method: "claim", params: ["seat_3"] }]);
    client.seatGranted(MODEL, ["seat_3", "15000"]);
    client.seatGranted(MODEL, ["seat_1", "15000"]);
    assert.equal(engine.avatar.sits.length, 1, "a renewal does not sit again");
    assert.equal(engine.calls("release").length, 0);
});

test("the renewal follows the lease the server grants", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(MODEL, CLICK);
    client.seatGranted(MODEL, ["seat_3", "6000"]);
    engine.advance(1950);
    assert.equal(engine.calls("claim").length, 1);
    engine.advance(100);
    assert.equal(engine.calls("claim").length, 2);
});

test("clicking again stands up on stand_3 at standing height and releases the seat", () => {
    const engine = fakeEngine();
    const client = seated(engine);
    client.clickDownOnEntity(MODEL, CLICK);
    assert.equal(engine.avatar.stands.length, 1);
    const { position, rotation } = engine.avatar.stands[0];
    assertNear(position, STAND_3);
    assertSameRotation(rotation, HALF_TURN);
    assert.deepEqual(engine.serverCalls, [{ id: MODEL, method: "release", params: [] }]);
    assert.equal(engine.timers.size, 0);
    assert.equal(engine.MyAvatar.onLoadComplete.handlers.size, 0);
    assert.equal(engine.MyAvatar.scaleChanged.handlers.size, 0);
    assert.equal(client.heldSeat, null);
});

test("refused: no sit, the reason in the log, and the visitor can ask again", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(MODEL, CLICK);
    client.seatRefused(MODEL, ["taken"]);
    assert.equal(engine.avatar.sits.length, 0);
    assert.match(engine.printed.join("\n"), /not sitting: every seat is taken/);
    client.clickDownOnEntity(MODEL, CLICK);
    client.seatRefused(MODEL, ["no-seat"]);
    assert.match(engine.printed.join("\n"), /not sitting: the server took none of the seats asked for/);
    assert.equal(engine.calls("claim").length, 2);
    assert.equal(engine.timers.size, 0, "the reply timer is cleared");
});

test("no answer: the request times out; a late grant gives the seat back", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(MODEL, CLICK);
    engine.advance(3000);
    assert.match(engine.printed.join("\n"), /did not answer; is serverScripts set\?/);
    client.seatGranted(MODEL, ["seat_3", "15000"]);
    assert.equal(engine.avatar.sits.length, 0);
    assert.equal(engine.calls("release").length, 1);
});

test("a grant for a seat the model does not have gives it back", () => {
    const engine = fakeEngine();
    const client = loaded();
    client.clickDownOnEntity(MODEL, CLICK);
    client.seatGranted(MODEL, ["seat_9", "15000"]);
    assert.equal(engine.avatar.sits.length, 0);
    assert.equal(engine.calls("release").length, 1);
});

test("a movement key held for standHoldMs stands up, once released after sitting", () => {
    const engine = fakeEngine({ userData: { seat: { standHoldMs: 300 } } });
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
    assertNear(engine.avatar.stands[0].position, STAND_3);
});

test("moved off the seat (a teleport): stands up where the avatar is", () => {
    const engine = fakeEngine();
    seated(engine);
    engine.avatar.position = { x: 40, y: 1, z: 40 };
    engine.advance(500);
    assert.equal(engine.avatar.stands.length, 0, "not during the settle time");
    engine.advance(600);
    assert.equal(engine.avatar.stands.length, 1);
    assert.deepEqual(engine.avatar.stands[0].position, { x: 40, y: 1, z: 40 });
    assert.equal(engine.calls("release").length, 1);
});

test("a lost lease, an avatar change and unload all stand up", () => {
    let engine = fakeEngine();
    let client = seated(engine);
    client.seatRefused(MODEL, ["taken"]);
    assert.equal(engine.avatar.stands.length, 1);
    assert.match(engine.printed.join("\n"), /the seat lease was lost, standing up/);

    engine = fakeEngine();
    seated(engine);
    [...engine.MyAvatar.onLoadComplete.handlers][0]();
    assert.equal(engine.avatar.stands.length, 1);

    engine = fakeEngine();
    client = seated(engine);
    client.unload();
    assert.equal(engine.avatar.stands.length, 1);
    assert.equal(engine.calls("release").length, 1);
    assert.equal(engine.timers.size, 0);
});

test("a seat with no stand joint is left out and reported once", () => {
    const { stand_3: _gone, ...joints } = DELOS_BENCH;
    const engine = fakeEngine({ joints });
    const client = loaded();
    client.clickDownOnEntity(MODEL, CLICK);
    assert.deepEqual(engine.calls("claim")[0].params, ["seat_2", "seat_1"]);
    client.seatRefused(MODEL, ["taken"]);
    client.clickDownOnEntity(MODEL, CLICK);
    assert.equal(engine.printed.filter(line => /seat_3 has no stand_3 joint/.test(line)).length, 1);
});

test("seats are counted up to the first missing seat_N", () => {
    const engine = fakeEngine({ joints: { seat_1: 1, stand_1: 4, seat_3: 3, stand_3: 6 } });
    loaded().clickDownOnEntity(MODEL, CLICK);
    assert.deepEqual(engine.calls("claim")[0].params, ["seat_1"]);
});

test("no seat joints, an avatar seated elsewhere, too far, or Create's edit mode: no request", () => {
    let engine = fakeEngine({ joints: {} });
    loaded().clickDownOnEntity(MODEL, CLICK);
    assert.equal(engine.serverCalls.length, 0);
    assert.match(engine.printed.join("\n"), /the model has no seat_1\/stand_1 joints \(or is not loaded yet\); not sitting/);

    engine = fakeEngine();
    engine.avatar.seated = true;
    loaded().clickDownOnEntity(MODEL, CLICK);
    assert.equal(engine.serverCalls.length, 0);

    engine = fakeEngine();
    engine.avatar.position = { x: 16, y: 1, z: 20 };
    loaded().clickDownOnEntity(MODEL, CLICK);
    assert.equal(engine.serverCalls.length, 0, "more than 5 m from the nearest seat");

    engine = fakeEngine();
    engine.settings["io.highfidelity.isEditing"] = true;
    loaded().clickDownOnEntity(MODEL, CLICK);
    assert.equal(engine.serverCalls.length, 0);
});

test("an out-of-range setting is logged against the entity and replaced", () => {
    const engine = fakeEngine({ userData: { seat: { hipsAboveSeat: 1 } } });
    const client = loaded();
    assert.deepEqual(engine.printed, [
        "Seat " + MODEL + ": seat.hipsAboveSeat must be a number from 0 to 0.3, got 1; using 0.06"
    ]);
    assert.equal(client.config.hipsAboveSeat, 0.06);
});
