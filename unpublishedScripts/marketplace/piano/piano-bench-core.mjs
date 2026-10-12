//
//  piano-bench-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano bench's logic without the engine: its userData configuration, where the avatar's
//  hips go on the seat and where it stands up, the one-sitter lease the bench's server script
//  holds, and the held-key stand-up gesture. No engine globals, so it runs under Node.
//

import { parseObject } from "./piano-core.mjs";

export const DEFAULT_HIPS_ABOVE_SEAT = 0.06;
export const DEFAULT_STAND_HOLD_MS = 500;
export const DEFAULT_LEASE_MS = 15_000;

/** @typedef {{ x: number, y: number, z: number }} Vector */
/** @typedef {{ x: number, y: number, z: number, w: number }} Rotation */

/**
 * @typedef {object} BenchConfig
 * @property {number} hipsAboveSeat how far the hips sit above the `seat` joint, as a fraction of the avatar's height
 * @property {number} standHoldMs how long a movement key must be held to stand up
 * @property {number} leaseMs how long a seat stays claimed without a renewal
 */

const LIMITS = {
    hipsAboveSeat: [0, 0.3, DEFAULT_HIPS_ABOVE_SEAT],
    standHoldMs: [0, 5000, DEFAULT_STAND_HOLD_MS],
    leaseMs: [3000, 120_000, DEFAULT_LEASE_MS]
};

/**
 * Reads `pianoBench` from the bench's userData. Every key is optional; a key that is present but
 * out of range is reported and replaced by its default.
 * @param {unknown} userDataText
 * @returns {{ config: BenchConfig, problems: string[] }}
 */
export function parseBenchConfig(userDataText) {
    const found = parseObject(userDataText).pianoBench;
    const bench = found !== null && typeof found === "object" ? found : {};
    /** @type {string[]} */
    const problems = [];
    /** @type {Record<string, number>} */
    const config = {};
    for (const [key, [min, max, fallback]] of Object.entries(LIMITS)) {
        const value = bench[key];
        if (value === undefined) {
            config[key] = fallback;
        } else if (typeof value === "number" && value >= min && value <= max) {
            config[key] = value;
        } else {
            problems.push("pianoBench." + key + " must be a number from " + min + " to " + max + ", got "
                + JSON.stringify(value) + "; using " + fallback);
            config[key] = fallback;
        }
    }
    return { config: /** @type {BenchConfig} */ (/** @type {unknown} */ (config)), problems };
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
 * turns it, so the avatar stays upright on a bench that is not quite level.
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
 * Where MyAvatar.beginSit pins the hips: straight above the bench's `seat` joint (the top centre
 * of the cushion), facing the way the seat faces (-Z, towards the keyboard).
 * @param {Vector} seatPosition the `seat` joint in world coordinates
 * @param {Rotation} seatRotation the `seat` joint's world rotation
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
 * feet, so the `stand_up` joint on the floor is raised by the avatar's own feet-to-position height.
 * @param {Vector} standPosition the `stand_up` joint in world coordinates
 * @param {Rotation} seatRotation the `seat` joint's world rotation: the avatar keeps facing the piano
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
 * One sitter at a time. The bench's server script holds this; a claim is granted when the seat
 * is free, its lease ran out, or the caller already holds it (a renewal).
 */
export class SeatLease {
    /** @type {string | null} */ holder = null;
    expiresAt = 0;

    /** @param {number} leaseMs */
    constructor(leaseMs) {
        this.leaseMs = leaseMs;
    }

    /**
     * @param {string} caller session ID
     * @param {number} now
     * @returns {boolean} whether the caller now holds the seat
     */
    claim(caller, now) {
        if (this.holder !== null && this.holder !== caller && now < this.expiresAt) {
            return false;
        }
        this.holder = caller;
        this.expiresAt = now + this.leaseMs;
        return true;
    }

    /**
     * @param {string} caller
     * @returns {boolean} whether the caller held the seat
     */
    release(caller) {
        if (this.holder !== caller) {
            return false;
        }
        this.holder = null;
        this.expiresAt = 0;
        return true;
    }

    /**
     * @param {number} now
     * @returns {string | null} who holds the seat
     */
    holderAt(now) {
        return this.holder !== null && now < this.expiresAt ? this.holder : null;
    }
}

/**
 * The stand-up gesture: a movement key held for `holdMs`. It arms only once every key has been
 * released after sitting down, so a key still held from walking up to the bench does not count.
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
