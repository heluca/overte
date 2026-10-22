//
//  piano-pose-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The salon grand's lid, stick and fallboard following Orpheus's status, without the engine
//  (Morpheus ADR 0021; phantasos props/salon-grand/spec.md sections 4 and 5). The joints come
//  from the prop's published manifest.json, never typed in: index, axis and travel. The entity
//  script server has no model loaded, so the pose is written as the entity's joint arrays.
//  No engine globals, so it runs under Node.
//

export const POSE_JOINTS = ["lid", "lid_prop", "fallboard"];
// Degrees per second: the lid opens in about 1.3 s, the stick stands in about 1 s.
export const POSE_RATES = { lid: 30, lid_prop: 100, fallboard: 90 };

/** @typedef {{ index: number, axis: [number, number, number], travel: number }} PoseJoint */
/** @typedef {{ count: number, joints: Record<string, PoseJoint> }} JointMap */
/** @typedef {Record<string, number>} Pose degrees from rest, per joint name */

/**
 * Reads the joints this script moves from the prop's manifest.json.
 * @param {unknown} manifest
 * @param {string[]} [names]
 * @returns {JointMap | { error: string }}
 */
export function parseJoints(manifest, names = POSE_JOINTS) {
    const m = /** @type {Record<string, any>} */ (manifest !== null && typeof manifest === "object" ? manifest : {});
    const count = m.overte && m.overte.joint_count;
    if (!Number.isInteger(count) || count <= 0) {
        return { error: "the manifest has no overte.joint_count" };
    }
    /** @type {Record<string, PoseJoint>} */
    const joints = {};
    for (const name of names) {
        const joint = m.joints && m.joints[name];
        if (!joint || joint.kind !== "rotation" || joint.unit !== "deg") {
            return { error: "the manifest has no rotation joint " + JSON.stringify(name) + " in degrees" };
        }
        const { index, axis, travel } = joint;
        if (!Number.isInteger(index) || index < 0 || index >= count) {
            return { error: "joint " + name + " has index " + JSON.stringify(index) + ", outside 0.." + (count - 1) };
        }
        const length = Array.isArray(axis) && axis.length === 3 && axis.every(Number.isFinite) ? Math.hypot(...axis) : 0;
        if (length === 0 || typeof travel !== "number" || !(travel > 0)) {
            return { error: "joint " + name + " needs a unit axis and a positive travel" };
        }
        joints[name] = { index, axis: [axis[0] / length, axis[1] / length, axis[2] / length], travel };
    }
    return { count, joints };
}

/**
 * The pose the status asks for (spec section 4): open while playing or starting, closed when
 * idle or paused, and the fallboard shut over the keys too when Orpheus is offline.
 * @param {{ state: string } | null} status
 * @param {JointMap} map
 * @returns {Pose}
 */
export function poseFor(status, map) {
    const { lid, lid_prop: stick, fallboard } = map.joints;
    const state = status ? status.state : "idle";
    const open = state === "playing" || state === "starting" || state === "connecting";
    return {
        lid: open ? lid.travel : 0,
        lid_prop: open ? stick.travel : 0,
        fallboard: state === "offline" ? fallboard.travel : 0
    };
}

/**
 * @param {number} from
 * @param {number} to
 * @param {number} most
 * @returns {number}
 */
function toward(from, to, most) {
    return Math.abs(to - from) <= most ? to : from + Math.sign(to - from) * most;
}

/**
 * One tween step of `dtMs` from `current` toward `target`. The stick rises only once the lid
 * is fully open, and the lid comes down only once the stick is folded: the stick meets the
 * lid square at full open, and its tip would cut through a lid that is still moving.
 * @param {Pose} current
 * @param {Pose} target
 * @param {number} dtMs
 * @param {JointMap} map
 * @returns {Pose}
 */
export function stepPose(current, target, dtMs, map) {
    const step = /** @param {keyof typeof POSE_RATES} name */ name => POSE_RATES[name] * dtMs / 1000;
    const next = { ...current };
    next.fallboard = toward(current.fallboard, target.fallboard, step("fallboard"));
    const lidOpen = current.lid >= map.joints.lid.travel;
    if (target.lid_prop < current.lid_prop || lidOpen) {
        next.lid_prop = toward(current.lid_prop, target.lid_prop, step("lid_prop"));
    }
    if (target.lid > current.lid || current.lid_prop <= 0) {
        next.lid = toward(current.lid, target.lid, step("lid"));
    }
    return next;
}

/**
 * @param {Pose} a
 * @param {Pose} b
 * @returns {boolean}
 */
export function samePose(a, b) {
    return POSE_JOINTS.every(name => a[name] === b[name]);
}

/**
 * The rotation of `degrees` about a unit `axis`, as Quat.angleAxis makes it.
 * @param {number} degrees
 * @param {[number, number, number]} axis
 * @returns {{ x: number, y: number, z: number, w: number }}
 */
export function angleAxis(degrees, axis) {
    if (degrees === 0) {
        return { x: 0, y: 0, z: 0, w: 1 };
    }
    const half = degrees * Math.PI / 360;
    const s = Math.sin(half);
    return { x: axis[0] * s, y: axis[1] * s, z: axis[2] * s, w: Math.cos(half) };
}

/**
 * The entity edit for a pose: the joint arrays at the manifest's length. Every joint this
 * script owns is flagged set on every edit, at rest too, because a joint flagged unset keeps
 * whatever it was last given (spec 5.3); the others stay unset and untouched.
 * @param {Pose} pose
 * @param {JointMap} map
 * @returns {{ jointRotationsSet: boolean[], jointRotations: { x: number, y: number, z: number, w: number }[] }}
 */
export function jointEdit(pose, map) {
    const jointRotationsSet = new Array(map.count).fill(false);
    const jointRotations = Array.from({ length: map.count }, () => ({ x: 0, y: 0, z: 0, w: 1 }));
    for (const [name, joint] of Object.entries(map.joints)) {
        jointRotationsSet[joint.index] = true;
        jointRotations[joint.index] = angleAxis(pose[name] || 0, joint.axis);
    }
    return { jointRotationsSet, jointRotations };
}
