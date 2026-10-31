//
//  seat-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The seat's client-side pure logic: configuration, the joint names, the order seats are asked
//  for, the sit and stand poses from the joints, and the stand-up gesture.
//

import { test } from "node:test";
import assert from "node:assert/strict";
import {
    parseObject, parseSeatConfig, seatJoint, standJointOf, nearestFirst, rotate, yawOnly, sitPose, standPose,
    StandGesture, DEFAULT_HIPS_ABOVE_SEAT, DEFAULT_STAND_HOLD_MS
} from "../seat-core.mjs";

const IDENTITY = { x: 0, y: 0, z: 0, w: 1 };
// A half turn about +Y, as the Delos benches' joints carry: -Z to +Z
const HALF_TURN = { x: 0, y: 1, z: 0, w: 0 };

/** @param {number} degrees @param {{x: number, y: number, z: number}} axis unit */
function angleAxis(degrees, axis) {
    const half = degrees * Math.PI / 360;
    return { x: axis.x * Math.sin(half), y: axis.y * Math.sin(half), z: axis.z * Math.sin(half), w: Math.cos(half) };
}

function assertClose(actual, expected, message) {
    for (const key of Object.keys(expected)) {
        assert.ok(Math.abs(actual[key] - expected[key]) < 1e-9,
            (message || "") + " " + key + ": " + actual[key] + " != " + expected[key]);
    }
}

/** Same rotation, allowing q and -q */
function assertSameRotation(actual, expected) {
    const sign = actual.w * expected.w + actual.x * expected.x + actual.y * expected.y + actual.z * expected.z < 0 ? -1 : 1;
    assertClose({ x: sign * actual.x, y: sign * actual.y, z: sign * actual.z, w: sign * actual.w }, expected);
}

/** a * b: b applied first, then a */
function multiply(a, b) {
    return {
        w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
    };
}

test("parseObject: an object, or {} for anything else", () => {
    assert.deepEqual(parseObject(JSON.stringify({ a: 1 })), { a: 1 });
    for (const text of [undefined, "", "not json", "[]", "null", "3"]) {
        assert.deepEqual(parseObject(text), {}, String(text));
    }
});

test("parseSeatConfig: defaults without a seat object", () => {
    const expected = { hipsAboveSeat: DEFAULT_HIPS_ABOVE_SEAT, standHoldMs: DEFAULT_STAND_HOLD_MS };
    for (const userData of ["", "not json", "[]", JSON.stringify({ piano: {} }), JSON.stringify({ seat: null })]) {
        assert.deepEqual(parseSeatConfig(userData), { config: expected, problems: [] }, userData);
    }
});

test("parseSeatConfig: takes values in range and reports the rest", () => {
    const { config, problems } = parseSeatConfig(JSON.stringify({
        seat: { hipsAboveSeat: 0.31, standHoldMs: 0, leaseMs: 1000 }
    }));
    assert.deepEqual(config, { hipsAboveSeat: DEFAULT_HIPS_ABOVE_SEAT, standHoldMs: 0 }, "leaseMs is the server's");
    assert.deepEqual(problems, ["seat.hipsAboveSeat must be a number from 0 to 0.3, got 0.31; using 0.06"]);
    assert.match(parseSeatConfig(JSON.stringify({ seat: { standHoldMs: "fast" } })).problems[0],
        /^seat\.standHoldMs must be a number from 0 to 5000, got "fast"; using 500$/);
});

test("the joint names: seat_N and its stand_N", () => {
    assert.equal(seatJoint(1), "seat_1");
    assert.equal(standJointOf("seat_1"), "stand_1");
    assert.equal(standJointOf(seatJoint(12)), "stand_12");
});

test("nearestFirst: nearest seat first, ties in their given order, far seats last", () => {
    const seats = [
        { name: "seat_1", position: { x: -0.52, y: 0.44, z: 0 } },
        { name: "seat_2", position: { x: 0, y: 0.44, z: 0 } },
        { name: "seat_3", position: { x: 0.52, y: 0.44, z: 0 } },
        { name: "seat_4", position: { x: 30, y: 0.44, z: 0 } }
    ];
    const names = (/** @type {{ name: string }[]} */ list) => list.map(seat => seat.name);
    assert.deepEqual(names(nearestFirst(seats, { x: 0.6, y: 1, z: 0.7 })), ["seat_3", "seat_2", "seat_1", "seat_4"]);
    assert.deepEqual(names(nearestFirst(seats, { x: 0, y: 1, z: 0.7 })), ["seat_2", "seat_1", "seat_3", "seat_4"],
        "seat_1 and seat_3 are equally near: the given order");
    assert.deepEqual(names(seats), ["seat_1", "seat_2", "seat_3", "seat_4"], "the input is left alone");
    assert.deepEqual(nearestFirst([], { x: 0, y: 0, z: 0 }), []);
});

test("rotate: a quarter turn about +Y takes -Z (forward) to -X (left)", () => {
    assertClose(rotate(angleAxis(90, { x: 0, y: 1, z: 0 }), { x: 0, y: 0, z: -1 }), { x: -1, y: 0, z: 0 });
    assertClose(rotate(IDENTITY, { x: 1, y: 2, z: 3 }), { x: 1, y: 2, z: 3 });
});

test("yawOnly keeps the heading and drops pitch and roll", () => {
    const yaw = angleAxis(130, { x: 0, y: 1, z: 0 });
    assertSameRotation(yawOnly(yaw), yaw);
    // A seat tipped 5 degrees forward and 3 sideways still faces the same way
    const tipped = [angleAxis(-5, { x: 1, y: 0, z: 0 }), angleAxis(3, { x: 0, y: 0, z: 1 })].reduce(multiply, yaw);
    assertSameRotation(yawOnly(tipped), yaw);
    assertSameRotation(yawOnly(IDENTITY), IDENTITY);
});

test("yawOnly: the Delos benches' half turn faces the sitter along +Z", () => {
    const rotation = yawOnly(HALF_TURN);
    assertSameRotation(rotation, HALF_TURN);
    assertClose(rotate(rotation, { x: 0, y: 0, z: -1 }), { x: 0, y: 0, z: 1 });
});

test("sitPose: hips above the seat by a fraction of the avatar's height, facing the joint's -Z", () => {
    // A seat joint yawed 90 degrees: its -Z, the way the sitter faces, is world -X
    const seatYaw = angleAxis(90, { x: 0, y: 1, z: 0 });
    const pose = sitPose({ x: 2, y: 0.5, z: -1 }, seatYaw, 1.75, 0.06);
    assertClose(pose.position, { x: 2, y: 0.5 + 0.105, z: -1 });
    assertSameRotation(pose.rotation, seatYaw);
    assertClose(rotate(pose.rotation, { x: 0, y: 0, z: -1 }), { x: -1, y: 0, z: 0 });
});

test("standPose: on the stand joint raised to the avatar's position height, facing the seat's way", () => {
    const pose = standPose({ x: 0.52, y: 0, z: 0.72 }, HALF_TURN, 0.92);
    assertClose(pose.position, { x: 0.52, y: 0.92, z: 0.72 });
    assertSameRotation(pose.rotation, HALF_TURN);
});

test("StandGesture: a key held long enough, once armed by a release", () => {
    const gesture = new StandGesture(500);
    assert.equal(gesture.update(0, true), false, "still held from walking up");
    assert.equal(gesture.update(1000, true), false, "not armed yet");
    assert.equal(gesture.update(1100, false), false);
    assert.equal(gesture.update(1200, true), false);
    assert.equal(gesture.update(1600, true), false);
    assert.equal(gesture.update(1650, false), false, "let go: starts again");
    assert.equal(gesture.update(1700, true), false);
    assert.equal(gesture.update(2200, true), true);
});
