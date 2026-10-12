//
//  piano-bench-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The bench's pure logic: configuration, the sit and stand poses from the bench's joints, the
//  seat lease and the stand-up gesture.
//

import { test } from "node:test";
import assert from "node:assert/strict";
import {
    parseBenchConfig, rotate, yawOnly, sitPose, standPose, SeatLease, StandGesture,
    DEFAULT_HIPS_ABOVE_SEAT, DEFAULT_STAND_HOLD_MS, DEFAULT_LEASE_MS
} from "../piano-bench-core.mjs";

const IDENTITY = { x: 0, y: 0, z: 0, w: 1 };

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

test("parseBenchConfig: defaults without a pianoBench object", () => {
    const expected = { hipsAboveSeat: DEFAULT_HIPS_ABOVE_SEAT, standHoldMs: DEFAULT_STAND_HOLD_MS, leaseMs: DEFAULT_LEASE_MS };
    for (const userData of ["", "not json", "[]", JSON.stringify({ piano: {} }), JSON.stringify({ pianoBench: null })]) {
        assert.deepEqual(parseBenchConfig(userData), { config: expected, problems: [] }, userData);
    }
});

test("parseBenchConfig: takes values in range and reports the rest", () => {
    const { config, problems } = parseBenchConfig(JSON.stringify({
        pianoBench: { hipsAboveSeat: 0.08, standHoldMs: "fast", leaseMs: 1000 }
    }));
    assert.deepEqual(config, { hipsAboveSeat: 0.08, standHoldMs: DEFAULT_STAND_HOLD_MS, leaseMs: DEFAULT_LEASE_MS });
    assert.equal(problems.length, 2);
    assert.match(problems[0], /pianoBench\.standHoldMs must be a number from 0 to 5000, got "fast"; using 500/);
    assert.match(problems[1], /pianoBench\.leaseMs .* got 1000; using 15000/);
});

test("rotate: a quarter turn about +Y takes -Z (forward) to -X (left)", () => {
    assertClose(rotate(angleAxis(90, { x: 0, y: 1, z: 0 }), { x: 0, y: 0, z: -1 }), { x: -1, y: 0, z: 0 });
    assertClose(rotate(IDENTITY, { x: 1, y: 2, z: 3 }), { x: 1, y: 2, z: 3 });
});

test("yawOnly keeps the heading and drops pitch and roll", () => {
    const yaw = angleAxis(130, { x: 0, y: 1, z: 0 });
    assertSameRotation(yawOnly(yaw), yaw);
    // A bench tipped 5 degrees forward and 3 sideways still faces the same way
    const tipped = [angleAxis(-5, { x: 1, y: 0, z: 0 }), angleAxis(3, { x: 0, y: 0, z: 1 })].reduce(multiply, yaw);
    assertSameRotation(yawOnly(tipped), yaw);
    assertSameRotation(yawOnly(IDENTITY), IDENTITY);
});

/** a * b: b applied first, then a */
function multiply(a, b) {
    return {
        w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
    };
}

test("sitPose: hips above the seat by a fraction of the avatar's height, facing the bench's -Z", () => {
    // A bench yawed 90 degrees: its -Z, the way the sitter faces, is world -X
    const benchYaw = angleAxis(90, { x: 0, y: 1, z: 0 });
    const pose = sitPose({ x: 2, y: 0.5, z: -1 }, benchYaw, 1.75, 0.06);
    assertClose(pose.position, { x: 2, y: 0.5 + 0.105, z: -1 });
    assertSameRotation(pose.rotation, benchYaw);
    assertClose(rotate(pose.rotation, { x: 0, y: 0, z: -1 }), { x: -1, y: 0, z: 0 }, "faces the keyboard");
});

test("standPose: on the stand_up joint raised to the avatar's position height, still facing the piano", () => {
    const pose = standPose({ x: 0, y: 0, z: 1.9 }, IDENTITY, 0.92);
    assertClose(pose.position, { x: 0, y: 0.92, z: 1.9 });
    assertSameRotation(pose.rotation, IDENTITY);
});

test("SeatLease: one holder; renewals extend it; it lapses; release frees it", () => {
    const lease = new SeatLease(15_000);
    assert.equal(lease.claim("alice", 0), true);
    assert.equal(lease.claim("bob", 1000), false, "taken");
    assert.equal(lease.claim("alice", 10_000), true, "renewal");
    assert.equal(lease.claim("bob", 24_999), false, "renewed until 25 s");
    assert.equal(lease.holderAt(24_999), "alice");
    assert.equal(lease.holderAt(25_000), null);
    assert.equal(lease.claim("bob", 25_000), true, "lapsed");
    assert.equal(lease.release("alice"), false, "only the holder releases");
    assert.equal(lease.holderAt(25_001), "bob");
    assert.equal(lease.release("bob"), true);
    assert.equal(lease.claim("alice", 25_002), true);
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

test("the placed salon grand: the sitter's hips over the cushion, facing the piano's keys", () => {
    // Piano at (24, -5.032, 54) yawed +90 degrees; bench at (0, 0, 1.545) in the piano's frame,
    // identity local rotation; seat (0, 0.48995, 0) and stand_up (0, 0, 0.640) in the bench's (spec 8.3, 8.4)
    const piano = { x: 24, y: -5.032, z: 54 };
    const yaw = angleAxis(90, { x: 0, y: 1, z: 0 });
    const inPiano = local => {
        const offset = rotate(yaw, local);
        return { x: piano.x + offset.x, y: piano.y + offset.y, z: piano.z + offset.z };
    };
    const seat = inPiano({ x: 0, y: 0.48995, z: 1.545 });
    const sit = sitPose(seat, yaw, 1.75, DEFAULT_HIPS_ABOVE_SEAT);
    assertClose(sit.position, { x: 25.545, y: -5.032 + 0.48995 + 0.105, z: 54 });
    const forward = rotate(sit.rotation, { x: 0, y: 0, z: -1 });
    assertClose(forward, { x: -1, y: 0, z: 0 }, "towards the piano's centre at x = 24");
    const stand = standPose(inPiano({ x: 0, y: 0, z: 1.545 + 0.640 }), yaw, 0.95);
    assertClose(stand.position, { x: 26.185, y: -5.032 + 0.95, z: 54 });
});
