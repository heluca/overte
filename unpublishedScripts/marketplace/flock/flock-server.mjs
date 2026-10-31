//
//  flock-server.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A flock member's `serverScripts`, run by the entity script server. Every member of a flock
//  carries this script; there is no manager entity. hz times a second the member steers itself
//  by its flockmates (flock-core.mjs) and edits its own position, rotation, velocity and
//  angularVelocity, which the entity server and every client carry forward kinematically until
//  the next edit. It scatters from an avatar that comes close and, if it has a roost, flies there
//  at night, hides and stops editing until it is light. Like the sunSky lamps, it works the phase
//  out from the clock and userData.sunSky.noonUtc each tick; no messages. It edits no other entity
//  and never writes userData or `locked`.
//
//  The engine shares one module instance per URL between every entity that uses it. Each
//  member's own state lives on its class instance; module scope holds, on purpose, a registry of
//  the members of each flock (so a member reads its mates' positions there rather than asking the
//  entity tree for each of them every tick) and the avatar objects (one per session, never one
//  per call; AvatarList.getAvatar leaks what it returns, heluca/overte#83).
//

import {
    STALE_TICKS, angularVelocityBetween, gatherMates, length, nextState, orientationFor, phase, readFlock,
    roostLegFor, rotate, scale, seedOf, steer, sub, sunElevation, add, takeOffVelocity
} from "./flock-core.mjs";

// How often a roosted member looks at the clock, ms.
export const ROOSTED_TICK_MS = 1000;
const ZERO = Object.freeze({ x: 0, y: 0, z: 0 });
const IDENTITY = Object.freeze({ x: 0, y: 0, z: 0, w: 1 });

/**
 * Every live member of every flock in this entity script server, by flock name, then entity id.
 * @type {Map<string, Map<string, import("./flock-core.mjs").RegistryEntry>>}
 */
export const flocks = new Map();

/**
 * The avatar objects asked for so far, by session id, each read live for its position.
 * @type {Map<string, { position: Vec3 }>}
 */
const avatars = new Map();

export default class Flock {
    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {import("./flock-core.mjs").FlockConfig} */ config;
    /** @type {number} */ seed;
    /** @type {import("./flock-core.mjs").Vector} the position at the last edit */ position;
    /** @type {import("./flock-core.mjs").Vector} */ velocity;
    /** @type {import("./flock-core.mjs").Rotation} */ rotation;
    /** @type {number} ms since the epoch */ lastTick = 0;
    /** @type {import("./flock-core.mjs").State} */ state = { name: "wheel" };
    /** @type {boolean} whether the entity is hidden now */ hidden = false;
    /** @type {number | null} */ timer = null;
    /** @type {number} the interval the timer runs at, ms */ tickMs = 0;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["userData", "locked", "position", "rotation", "velocity", "visible"]);
        const { config, problems, error } = readFlock(properties.userData);
        for (const problem of problems) {
            print("Flock " + entityID + ": " + problem);
        }
        if (config === null) {
            print("Flock " + entityID + ": " + error + ". The member does not fly.");
            return;
        }
        if (properties.locked === true) {
            print("Flock " + entityID + ": the member is locked and takes no edit; unlock it and reload its script. The member does not fly.");
            return;
        }
        this.config = config;
        this.seed = seedOf(String(entityID));
        this.position = /** @type {Vec3} */ (properties.position);
        this.rotation = properties.rotation || IDENTITY;
        this.hidden = properties.visible === false;
        const velocity = properties.velocity || ZERO;
        if (length(velocity) >= config.speed.min / 2) {
            this.velocity = velocity;
        } else {
            // Off the way the model faces, level, at cruise speed.
            const front = rotate(this.rotation, { x: 0, y: 0, z: -1 });
            const flat = length({ x: front.x, y: 0, z: front.z }) > 1e-6 ? { x: front.x, y: 0, z: front.z } : { x: 1, y: 0, z: 0 };
            this.velocity = scale(flat, (config.speed.min + config.speed.max) / 2 / length(flat));
        }
        this.lastTick = Date.now();
        this.schedule(1000 / config.hz);
    }

    unload() {
        if (this.timer !== null) {
            Script.clearInterval(this.timer);
            this.timer = null;
        }
        if (this.config) {
            this.leaveRegistry();
        }
    }

    /** One step: the state, then the flight for it, then one edit of this member. */
    tick() {
        const config = this.config;
        const now = Date.now();
        const dt = Math.min(1, Math.max(0, (now - this.lastTick) / 1000));
        this.lastTick = now;
        // Where the engine has the member now: the last edit carried forward along its velocity.
        const position = add(this.position, scale(this.velocity, dt));
        const was = this.state.name;

        const roost = config.roost;
        let leg = roost !== null && was === "roost" ? roostLegFor({ position, velocity: this.velocity }, roost, config, dt) : null;
        const threat = was === "wheel" || was === "scatter" ? this.nearestAvatar(position) : null;
        // Only a member with a roost has a noon (readFlock), and only it minds the night.
        const today = config.noonUtc !== null ? phase(sunElevation(now, config.noonUtc)) : "day";
        /** @type {Omit<import("./flock-core.mjs").StateInputs, "arrived">} */
        const inputs = { now, phase: today, roosts: roost !== null, threat, scatterForMs: config.scatter.forMs };
        let next = nextState(this.state, { ...inputs, arrived: leg !== null && leg.arrived });
        if (roost !== null && next.name === "roost" && leg === null) {
            // Just turned for the roost: it may be there already.
            leg = roostLegFor({ position, velocity: this.velocity }, roost, config, dt);
            next = nextState(next, { ...inputs, arrived: leg.arrived });
        }
        this.state = next;

        if (next.name === "roosted") {
            if (was !== "roosted") {
                this.position = position;
                this.velocity = ZERO;
                this.hidden = true;
                Entities.editEntity(this.entityID, { position, velocity: ZERO, angularVelocity: ZERO, visible: false });
                this.leaveRegistry();
                this.schedule(ROOSTED_TICK_MS);
            }
            return;
        }
        if (was === "roosted") {
            this.velocity = takeOffVelocity(position, config);
            this.schedule(1000 / config.hz);
        }

        let velocity;
        if (next.name === "roost" && leg !== null) {
            velocity = leg.velocity;
        } else {
            const mates = gatherMates(this.registry().entries(), String(this.entityID), now, STALE_TICKS * 1000 / config.hz);
            const flee = next.name === "scatter" ? next.from : null;
            velocity = steer({ position, velocity: this.velocity, seed: this.seed, flee }, mates, config, dt, now);
        }

        const acceleration = dt > 0 ? scale(sub(velocity, this.velocity), 1 / dt) : ZERO;
        const rotation = orientationFor(velocity, acceleration) || this.rotation;
        const angularVelocity = angularVelocityBetween(this.rotation, rotation, dt);
        if (this.hidden) {
            Entities.editEntity(this.entityID, { position, rotation, velocity, angularVelocity, visible: true });
            this.hidden = false;
        } else {
            Entities.editEntity(this.entityID, { position, rotation, velocity, angularVelocity });
        }
        this.position = position;
        this.velocity = velocity;
        this.rotation = rotation;
        this.registry().set(String(this.entityID), { position, velocity, state: next.name, seen: now });
    }

    /**
     * The nearest avatar within scatter.within of `position`, if any.
     * @param {import("./flock-core.mjs").Vector} position
     * @returns {import("./flock-core.mjs").Vector | null}
     */
    nearestAvatar(position) {
        const within = this.config.scatter.within;
        if (within <= 0) {
            return null;
        }
        if (avatars.size > 0) {
            // A session that has gone: its object reads nothing now, and a new one is needed if
            // the avatar comes back.
            const present = new Set(AvatarList.getAvatarIdentifiers().map(String));
            for (const session of avatars.keys()) {
                if (!present.has(session)) {
                    avatars.delete(session);
                }
            }
        }
        const ids = AvatarList.getAvatarsInRange(position, within);
        /** @type {import("./flock-core.mjs").Vector | null} */
        let nearest = null;
        let best = Infinity;
        for (const id of ids) {
            const session = String(id);
            let avatar = avatars.get(session);
            if (!avatar) {
                avatar = AvatarList.getAvatar(id);
                avatars.set(session, avatar);
            }
            const distance = length(sub(avatar.position, position));
            if (distance < best) {
                best = distance;
                nearest = avatar.position;
            }
        }
        return nearest;
    }

    /** @returns {Map<string, import("./flock-core.mjs").RegistryEntry>} this member's flock */
    registry() {
        let members = flocks.get(this.config.name);
        if (!members) {
            members = new Map();
            flocks.set(this.config.name, members);
        }
        return members;
    }

    leaveRegistry() {
        const members = flocks.get(this.config.name);
        if (members) {
            members.delete(String(this.entityID));
            if (members.size === 0) {
                flocks.delete(this.config.name);
            }
        }
    }

    /** @param {number} ms */
    schedule(ms) {
        if (this.timer !== null && this.tickMs === ms) {
            return;
        }
        if (this.timer !== null) {
            Script.clearInterval(this.timer);
        }
        this.tickMs = ms;
        this.timer = Script.setInterval(() => this.tick(), ms);
    }
}
