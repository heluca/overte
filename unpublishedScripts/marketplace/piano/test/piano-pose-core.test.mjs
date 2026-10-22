//
//  piano-pose-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The lid, stick and fallboard: their joints from the prop's manifest, the pose each status
//  asks for, the ordered tween, and the joint arrays the server script writes.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { parseJoints, poseFor, stepPose, samePose, angleAxis, jointEdit } from "../piano-pose-core.mjs";
import { MANIFEST } from "./salon-grand-manifest.mjs";

const MAP = /** @type {import("../piano-pose-core.mjs").JointMap} */ (parseJoints(MANIFEST));
const CLOSED = { lid: 0, lid_prop: 0, fallboard: 0 };
const OPEN = { lid: 40, lid_prop: 103.3876, fallboard: 0 };

test("parseJoints reads index, axis and travel from the manifest", () => {
    assert.equal(MAP.count, 109);
    assert.deepEqual(MAP.joints.lid, { index: 100, axis: [0, 0, 1], travel: 40 });
    assert.equal(MAP.joints.fallboard.index, 9);
    assert.ok(Math.abs(Math.hypot(...MAP.joints.lid_prop.axis) - 1) < 1e-12, "the axis is normalised");
});

test("parseJoints names what is missing or wrong", () => {
    assert.deepEqual(parseJoints(null), { error: "the manifest has no overte.joint_count" });
    const without = structuredClone(MANIFEST);
    delete without.joints.lid_prop;
    assert.match(/** @type {any} */ (parseJoints(without)).error, /no rotation joint "lid_prop"/);
    const outside = structuredClone(MANIFEST);
    outside.joints.lid.index = 109;
    assert.match(/** @type {any} */ (parseJoints(outside)).error, /joint lid has index 109, outside 0..108/);
    const flat = structuredClone(MANIFEST);
    flat.joints.fallboard.axis = [0, 0, 0];
    assert.match(/** @type {any} */ (parseJoints(flat)).error, /joint fallboard needs a unit axis/);
});

test("poseFor: open while playing or starting, closed when idle or paused, fallboard shut offline", () => {
    for (const state of ["playing", "starting", "connecting"]) {
        assert.deepEqual(poseFor({ state }, MAP), OPEN, state);
    }
    for (const state of ["idle", "paused"]) {
        assert.deepEqual(poseFor({ state }, MAP), CLOSED, state);
    }
    assert.deepEqual(poseFor(null, MAP), CLOSED);
    assert.deepEqual(poseFor({ state: "offline" }, MAP), { lid: 0, lid_prop: 0, fallboard: 90 });
});

/**
 * @param {import("../piano-pose-core.mjs").Pose} from
 * @param {import("../piano-pose-core.mjs").Pose} to
 * @returns {import("../piano-pose-core.mjs").Pose[]} every step at 100 ms until the target
 */
function tween(from, to) {
    const steps = [];
    let pose = from;
    while (!samePose(pose, to)) {
        pose = stepPose(pose, to, 100, MAP);
        steps.push(pose);
        assert.ok(steps.length < 100, "the tween ends");
    }
    return steps;
}

test("opening: the lid rises first, then the stick stands, in about 2.5 s", () => {
    const steps = tween(CLOSED, OPEN);
    assert.equal(steps.length, 25);
    for (const pose of steps) {
        assert.ok(pose.lid_prop === 0 || pose.lid === 40, "the stick moves only with the lid fully open");
    }
    assert.equal(steps[13].lid, 40);
    assert.equal(steps[13].lid_prop, 0);
});

test("closing: the stick folds first, then the lid comes down", () => {
    const steps = tween(OPEN, CLOSED);
    for (const pose of steps) {
        assert.ok(pose.lid === 40 || pose.lid_prop === 0, "the lid moves only with the stick folded");
    }
    assert.deepEqual(steps.at(-1), CLOSED);
});

test("a change of mind part way reverses without breaking the order", () => {
    const halfOpen = tween(CLOSED, OPEN)[6];
    assert.ok(halfOpen.lid > 0 && halfOpen.lid < 40);
    assert.ok(tween(halfOpen, CLOSED).every(pose => pose.lid_prop === 0));
    const folding = tween(OPEN, CLOSED)[0];
    assert.ok(folding.lid_prop > 0 && folding.lid === 40);
    assert.ok(tween(folding, OPEN).every(pose => pose.lid === 40));
});

test("the fallboard moves with the lid, independently", () => {
    const steps = tween(CLOSED, { lid: 0, lid_prop: 0, fallboard: 90 });
    assert.equal(steps.length, 10);
});

test("angleAxis matches Quat.angleAxis: degrees about a unit axis", () => {
    const q = angleAxis(90, [1, 0, 0]);
    assert.ok(Math.abs(q.x - Math.SQRT1_2) < 1e-12 && Math.abs(q.w - Math.SQRT1_2) < 1e-12 && q.y === 0 && q.z === 0);
    assert.deepEqual(angleAxis(0, [0, 0, 1]), { x: 0, y: 0, z: 0, w: 1 });
});

test("jointEdit: arrays of the manifest's length, this script's joints set (at rest too), others untouched", () => {
    const edit = jointEdit({ lid: 40, lid_prop: 0, fallboard: 0 }, MAP);
    assert.equal(edit.jointRotationsSet.length, 109);
    assert.equal(edit.jointRotations.length, 109);
    assert.deepEqual(edit.jointRotationsSet.flatMap((set, i) => set ? [i] : []), [9, 100, 101]);
    assert.deepEqual(edit.jointRotations[101], { x: 0, y: 0, z: 0, w: 1 }, "a folded stick is sent as identity");
    assert.deepEqual(edit.jointRotations[100], angleAxis(40, [0, 0, 1]));
    assert.deepEqual(edit.jointRotations[50], { x: 0, y: 0, z: 0, w: 1 });
});
