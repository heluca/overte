//
//  seat-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The seat's client-side logic without the engine: its userData configuration, the seat
//  joints' names, which seat to ask for first, where the avatar's hips go on a seat and where
//  it stands up, and the held-key stand-up gesture. No engine globals, so it runs under Node.
//  The seat lease is the server script's, in seat-server.js.
//

export const DEFAULT_HIPS_ABOVE_SEAT = 0.06;
export const DEFAULT_STAND_HOLD_MS = 500;
// The server script takes no longer list; a model with more seats is not expected
export const MAX_SEATS = 64;

/** @typedef {{ x: number, y: number, z: number }} Vector */
/** @typedef {{ x: number, y: number, z: number, w: number }} Rotation */

/**
 * @typedef {object} SeatConfig
 * @property {number} hipsAboveSeat how far the hips sit above a `seat_N` joint, as a fraction of the avatar's height
 * @property {number} standHoldMs how long a movement key must be held to stand up
 */

const LIMITS = {
    hipsAboveSeat: [0, 0.3, DEFAULT_HIPS_ABOVE_SEAT],
    standHoldMs: [0, 5000, DEFAULT_STAND_HOLD_MS]
};

/**
 * @param {unknown} text userData, or any other JSON text
 * @returns {Record<string, any>} the parsed object, or {} for anything that is not one
 */
export function parseObject(text) {
    try {
        const value = JSON.parse(typeof text === "string" ? text : "");
        return value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
    } catch (e) {
        return {};
    }
}

/**
 * Reads `seat` from the entity's userData. Every key is optional; a key that is present but out
 * of range is reported and replaced by its default. `seat.leaseMs` is the server script's.
 * @param {unknown} userDataText
 * @returns {{ config: SeatConfig, problems: string[] }}
 */
export function parseSeatConfig(userDataText) {
    const found = parseObject(userDataText).seat;
    const seat = found !== null && typeof found === "object" ? found : {};
    /** @type {string[]} */
    const problems = [];
    /** @type {Record<string, number>} */
    const config = {};
    for (const [key, [min, max, fallback]] of Object.entries(LIMITS)) {
        const value = seat[key];
        if (value === undefined) {
            config[key] = fallback;
        } else if (typeof value === "number" && value >= min && value <= max) {
            config[key] = value;
        } else {
            problems.push("seat." + key + " must be a number from " + min + " to " + max + ", got "
                + JSON.stringify(value) + "; using " + fallback);
            config[key] = fallback;
        }
    }
    return { config: /** @type {SeatConfig} */ (/** @type {unknown} */ (config)), problems };
}

/**
 * @param {number} index 1 to MAX_SEATS
 * @returns {string} the seat's joint, where the hips go
 */
export function seatJoint(index) {
    return "seat_" + index;
}

/**
 * @param {string} seatName a `seat_N` joint
 * @returns {string} the matching `stand_N` joint, where the avatar stands up
 */
export function standJointOf(seatName) {
    return "stand_" + seatName.slice("seat_".length);
}

/**
 * The seats in the order to ask the server for them: nearest to `position` first, ties in
 * their given order.
 * @template {{ position: Vector }} T
 * @param {T[]} seats
 * @param {Vector} position
 * @returns {T[]} a new array
 */
export function nearestFirst(seats, position) {
    const distance = (/** @type {Vector} */ p) => Math.hypot(p.x - position.x, p.y - position.y, p.z - position.z);
    return seats
        .map(seat => ({ seat, distance: distance(seat.position) }))
        .sort((a, b) => a.distance - b.distance)
        .map(entry => entry.seat);
}

/**
 * @param {Rotation} q
 * @param {Vector} v
 * @returns {Vector} v rotated by q
 */
export function rotate(q, v) {
    // v + 2w(u x v) + 2u x (u x v), u = q's vector part
    const tx = 2 * (q.y * v.z - q.z * v.y);
    const ty = 2 * (q.z * v.x - q.x * v.z);
    const tz = 2 * (q.x * v.y - q.y * v.x);
    return {
        x: v.x + q.w * tx + (q.y * tz - q.z * ty),
        y: v.y + q.w * ty + (q.z * tx - q.x * tz),
        z: v.z + q.w * tz + (q.x * ty - q.y * tx)
    };
}

/**
 * The heading of `q` alone: a rotation about +Y that turns -Z (an avatar's forward) the way q
 * turns it, so the avatar stays upright on a seat that is not quite level.
 * @param {Rotation} q
 * @returns {Rotation}
 */
export function yawOnly(q) {
    const forward = rotate(q, { x: 0, y: 0, z: -1 });
    if (Math.abs(forward.x) < 1e-9 && Math.abs(forward.z) < 1e-9) {
        return { x: 0, y: 0, z: 0, w: 1 };
    }
    const half = Math.atan2(-forward.x, -forward.z) / 2;
    return { x: 0, y: Math.sin(half), z: 0, w: Math.cos(half) };
}

/**
 * Where MyAvatar.beginSit pins the hips: straight above the `seat_N` joint (the top centre of
 * the seat), facing the way the joint's -Z points.
 * @param {Vector} seatPosition the `seat_N` joint in world coordinates
 * @param {Rotation} seatRotation the `seat_N` joint's world rotation
 * @param {number} avatarHeight MyAvatar.getHeight(), metres
 * @param {number} hipsAboveSeat fraction of the avatar's height
 * @returns {{ position: Vector, rotation: Rotation }}
 */
export function sitPose(seatPosition, seatRotation, avatarHeight, hipsAboveSeat) {
    return {
        position: { x: seatPosition.x, y: seatPosition.y + avatarHeight * hipsAboveSeat, z: seatPosition.z },
        rotation: yawOnly(seatRotation)
    };
}

/**
 * Where MyAvatar.endSit puts the avatar. endSit sets the avatar's position, which is above its
 * feet, so the `stand_N` joint on the floor is raised by the avatar's own feet-to-position height.
 * @param {Vector} standPosition the `stand_N` joint in world coordinates
 * @param {Rotation} seatRotation the `seat_N` joint's world rotation: the avatar keeps facing the seat's way
 * @param {number} feetToPosition MyAvatar.position.y - MyAvatar.feetPosition.y while standing
 * @returns {{ position: Vector, rotation: Rotation }}
 */
export function standPose(standPosition, seatRotation, feetToPosition) {
    return {
        position: { x: standPosition.x, y: standPosition.y + feetToPosition, z: standPosition.z },
        rotation: yawOnly(seatRotation)
    };
}

/**
 * The stand-up gesture: a movement key held for `holdMs`. It arms only once every key has been
 * released after sitting down, so a key still held from walking up to the seat does not count.
 */
export class StandGesture {
    armed = false;
    /** @type {number | null} */ heldSince = null;

    /** @param {number} holdMs */
    constructor(holdMs) {
        this.holdMs = holdMs;
    }

    /**
     * @param {number} now
     * @param {boolean} pressed whether any movement key is held now
     * @returns {boolean} whether to stand up
     */
    update(now, pressed) {
        if (!pressed) {
            this.armed = true;
            this.heldSince = null;
            return false;
        }
        if (!this.armed) {
            return false;
        }
        if (this.heldSince === null) {
            this.heldSince = now;
        }
        return now - this.heldSince >= this.holdMs;
    }
}
