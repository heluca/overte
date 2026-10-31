//
//  flock-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A flock without the engine: the userData.flock configuration, one member's steering by the
//  boid rules (separation, alignment, cohesion) inside a home cylinder and an altitude band, its
//  nose along its velocity banked into the turn, the wheel / scatter / roost state machine, the
//  leg to the roost, and the phase of the day from the clock. No engine globals, so it runs under
//  Node and the entity script server alike; flock-server.mjs is the entity script.
//
//  World frame: +y up. An entity's front is its -z (Quat.getFront).
//

export const DEFAULT_RADIUS = 40;
export const DEFAULT_BAND = 7.5;
export const DEFAULT_SPEED_MIN = 7;
export const DEFAULT_SPEED_MAX = 12;
export const DEFAULT_HZ = 5;
export const DEFAULT_SCATTER_WITHIN = 10;
export const DEFAULT_SCATTER_FOR_MS = 8000;

// The nearest mates a member steers by; the rest of the flock reaches it through them.
export const MAX_MATES = 24;
// Mates further than this are not seen at all, m.
export const PERCEPTION = 20;
// Mates closer than this push apart, harder the closer, m.
export const SEPARATION = 6;
// How fast the heading may turn, rad/s: at 9.5 m/s a turn of radius 9 m, so a gull banks round.
export const MAX_TURN_RATE = 60 * Math.PI / 180;
// How fast the speed may change, m/s per s.
export const MAX_SPEED_CHANGE = 4;
// The steepest climb or dive while wheeling, rad.
export const MAX_PITCH = 30 * Math.PI / 180;
export const MAX_BANK = 35 * Math.PI / 180;
export const GRAVITY = 9.81;
// A member this close to its roost has arrived, m.
export const ROOST_ARRIVAL = 2;
// A registry entry not refreshed for this many of its member's ticks is a member gone.
export const STALE_TICKS = 3;
// One turn of a member's wander, ms.
export const WANDER_PERIOD_MS = 30000;

// The steering gains, in m/s^2 for the pushes that do not scale with a distance or a speed.
export const WEIGHTS = Object.freeze({
    // At contact, falling to 0 at SEPARATION.
    separation: 8,
    // Per m/s of difference from the mates' mean velocity.
    alignment: 0.5,
    // Per metre from the mates' centroid.
    cohesion: 0.05,
    // Per metre past `radius`, horizontally.
    home: 0.8,
    // About the home axis, so the flock circles rather than ping-pongs off the edge.
    circulation: 0.3,
    // Per metre past the band, capped at bandMax.
    band: 0.8,
    bandMax: 3,
    // Per m/s of climb or sink: gulls soar mostly level.
    level: 0.6,
    // Per m/s short of the target speed.
    cruise: 0.5,
    wander: 1.2,
    flee: 15
});

const MS_PER_HOUR = 3600000;
const EPSILON = 1e-9;

// Copied from sunSky/sun-sky.mjs (DEGREES_PER_HOUR, SUNSET_ELEVATION, DARK_ELEVATION); each
// bundle is served from its own folder, so the flock does not import across bundles.
export const DEGREES_PER_HOUR = 15;
export const SUNSET_ELEVATION = -0.833;
export const DARK_ELEVATION = -6;

/**
 * @typedef {{ x: number, y: number, z: number }} Vector
 * @typedef {{ x: number, y: number, z: number, w: number }} Rotation
 * @typedef {"day" | "twilight" | "night"} Phase
 *
 * @typedef {object} FlockConfig
 * @property {string} name the flock; members with the same name flock together
 * @property {Vector} home the centre of the home cylinder and of the altitude band
 * @property {number} radius the home cylinder's radius, m
 * @property {number} band how far above or below home.y a member may fly freely, m
 * @property {{ min: number, max: number }} speed m/s
 * @property {number} hz edits per second
 * @property {{ within: number, forMs: number }} scatter an avatar this close, m, scatters the
 *     member for this long, ms; within 0 never scatters
 * @property {Vector | null} roost where the member spends the night; null never roosts
 * @property {number | null} noonUtc the hour of solar noon in UTC, set when roost is
 *
 * @typedef {object} Member
 * @property {Vector} position
 * @property {Vector} velocity
 * @property {number} [seed] 0 to 1, the member's own wander phase (seedOf)
 * @property {Vector | null} [flee] a point to flee from, while scattering
 *
 * @typedef {{ name: "wheel" } | { name: "scatter", from: Vector, until: number } | { name: "roost" }
 *     | { name: "roosted" }} State
 *
 * @typedef {object} StateInputs
 * @property {number} now ms since the epoch
 * @property {Phase} phase
 * @property {boolean} roosts whether the member has a roost
 * @property {Vector | null} threat the nearest avatar within scatter.within, if any
 * @property {boolean} arrived whether the leg to the roost has arrived (roostLegFor)
 * @property {number} scatterForMs
 *
 * @typedef {object} RegistryEntry
 * @property {Vector} position at `seen`
 * @property {Vector} velocity
 * @property {State["name"]} state
 * @property {number} seen ms since the epoch
 */

// --- vectors ---------------------------------------------------------------------------------

/** @param {Vector} a @param {Vector} b @returns {Vector} */
export function add(a, b) {
    return { x: a.x + b.x, y: a.y + b.y, z: a.z + b.z };
}

/** @param {Vector} a @param {Vector} b @returns {Vector} */
export function sub(a, b) {
    return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
}

/** @param {Vector} a @param {number} s @returns {Vector} */
export function scale(a, s) {
    return { x: a.x * s, y: a.y * s, z: a.z * s };
}

/** @param {Vector} a @param {Vector} b */
export function dot(a, b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/** @param {Vector} a @param {Vector} b @returns {Vector} */
export function cross(a, b) {
    return { x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x };
}

/** @param {Vector} a */
export function length(a) {
    return Math.hypot(a.x, a.y, a.z);
}

/**
 * @param {Vector} a
 * @returns {Vector | null} a scaled to length 1, or null for (nearly) zero
 */
export function unit(a) {
    const l = length(a);
    return l > EPSILON ? scale(a, 1 / l) : null;
}

/** @param {number} value @param {number} min @param {number} max */
function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
}

/** @param {unknown} v @returns {v is Vector} */
function isVector(v) {
    return v !== null && typeof v === "object"
        && [/** @type {any} */ (v).x, /** @type {any} */ (v).y, /** @type {any} */ (v).z].every(Number.isFinite);
}

// --- configuration ---------------------------------------------------------------------------

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
 * Reads `flock` (and, for a member with a roost, `sunSky.noonUtc`) from the entity's userData.
 * `name` and `home` are required; every other key has a default, and a number out of range is
 * reported and clamped into it (a value that is not a number is reported and replaced by its
 * default).
 * @param {unknown} userDataText
 * @returns {{ config: FlockConfig, problems: string[], error: null }
 *     | { config: null, problems: string[], error: string }}
 */
export function readFlock(userDataText) {
    const userData = parseObject(userDataText);
    const found = userData.flock;
    /** @type {string[]} */
    const problems = [];
    if (found === null || typeof found !== "object" || Array.isArray(found)) {
        return { config: null, problems, error: "userData.flock is missing" };
    }
    if (typeof found.name !== "string" || found.name === "") {
        return { config: null, problems, error: "userData.flock.name must be the flock's name; got " + JSON.stringify(found.name) };
    }
    if (!isVector(found.home)) {
        return { config: null, problems, error: "userData.flock.home must be a position {x, y, z}; got " + JSON.stringify(found.home) };
    }

    /**
     * @param {string} key the key's path under flock, for the message
     * @param {unknown} value
     * @param {number} min
     * @param {number} max
     * @param {number} fallback
     */
    const number = (key, value, min, max, fallback) => {
        if (value === undefined) {
            return fallback;
        }
        if (typeof value !== "number" || !Number.isFinite(value)) {
            problems.push("flock." + key + " must be a number from " + min + " to " + max + ", got "
                + JSON.stringify(value) + "; using " + fallback);
            return fallback;
        }
        const clamped = clamp(value, min, max);
        if (clamped !== value) {
            problems.push("flock." + key + " must be from " + min + " to " + max + ", got " + value + "; using " + clamped);
        }
        return clamped;
    };
    /** @param {unknown} v @returns {Record<string, unknown>} */
    const object = v => v !== null && typeof v === "object" && !Array.isArray(v) ? /** @type {Record<string, unknown>} */ (v) : {};

    const speed = object(found.speed);
    const min = number("speed.min", speed.min, 0.5, 50, DEFAULT_SPEED_MIN);
    let max = number("speed.max", speed.max, 0.5, 50, DEFAULT_SPEED_MAX);
    if (max < min) {
        problems.push("flock.speed.max " + max + " is below speed.min " + min + "; using " + min);
        max = min;
    }
    const scatter = object(found.scatter);

    /** @type {Vector | null} */
    let roost = null;
    if (found.roost !== undefined && found.roost !== null) {
        if (isVector(found.roost)) {
            roost = { x: found.roost.x, y: found.roost.y, z: found.roost.z };
        } else {
            problems.push("flock.roost must be a position {x, y, z}, got " + JSON.stringify(found.roost) + "; the member never roosts");
        }
    }
    /** @type {number | null} */
    let noonUtc = null;
    if (roost !== null) {
        const sunSky = object(userData.sunSky);
        if (typeof sunSky.noonUtc !== "number" || !(sunSky.noonUtc >= 0 && sunSky.noonUtc < 24)) {
            return { config: null, problems, error: "userData.sunSky.noonUtc must be the hour of solar noon in UTC, 0 to 24, "
                + "for a member with a roost; got " + JSON.stringify(sunSky.noonUtc) };
        }
        noonUtc = sunSky.noonUtc;
    }

    return {
        config: {
            name: found.name,
            home: { x: found.home.x, y: found.home.y, z: found.home.z },
            radius: number("radius", found.radius, 1, 1000, DEFAULT_RADIUS),
            band: number("band", found.band, 0, 500, DEFAULT_BAND),
            speed: { min, max },
            hz: number("hz", found.hz, 1, 10, DEFAULT_HZ),
            scatter: {
                within: number("scatter.within", scatter.within, 0, 100, DEFAULT_SCATTER_WITHIN),
                forMs: number("scatter.forMs", scatter.forMs, 0, 120000, DEFAULT_SCATTER_FOR_MS)
            },
            roost,
            noonUtc
        },
        problems,
        error: null
    };
}

/**
 * A member's own number in [0, 1), from its entity id, so members do not wander in step.
 * @param {string} text
 */
export function seedOf(text) {
    let hash = 2166136261;
    for (let i = 0; i < text.length; i++) {
        hash = Math.imul(hash ^ text.charCodeAt(i), 16777619) >>> 0;
    }
    return hash / 4294967296;
}

// --- the flock -------------------------------------------------------------------------------

/**
 * The live mates in a flock's registry, as of `now`: every entry but the member's own, those
 * not refreshed within `staleMs` left out as gone, those roosting left out as not flying, and
 * each position carried forward along its velocity to `now`.
 * @param {Iterable<[string, RegistryEntry]>} entries
 * @param {string} selfId
 * @param {number} now ms since the epoch
 * @param {number} staleMs
 * @returns {{ position: Vector, velocity: Vector }[]}
 */
export function gatherMates(entries, selfId, now, staleMs) {
    const mates = [];
    for (const [id, entry] of entries) {
        if (id === selfId || now - entry.seen > staleMs || entry.state === "roost" || entry.state === "roosted") {
            continue;
        }
        const ahead = Math.max(0, now - entry.seen) / 1000;
        mates.push({ position: add(entry.position, scale(entry.velocity, ahead)), velocity: entry.velocity });
    }
    return mates;
}

/**
 * `from` turned toward `to` by at most `maxAngle`.
 * @param {Vector} from a unit vector
 * @param {Vector} to a unit vector
 * @param {number} maxAngle rad
 * @returns {Vector} a unit vector
 */
export function turnToward(from, to, maxAngle) {
    const angle = Math.acos(clamp(dot(from, to), -1, 1));
    if (angle <= maxAngle) {
        return to;
    }
    // About from x to; a reversal has no such axis and turns about from's own up, a level turn.
    let axis = unit(cross(from, to));
    if (!axis) {
        const right = unit(cross(from, { x: 0, y: 1, z: 0 })) || { x: 1, y: 0, z: 0 };
        axis = cross(right, from);
    }
    // Rodrigues, with axis perpendicular to from.
    const c = Math.cos(maxAngle);
    const s = Math.sin(maxAngle);
    return /** @type {Vector} */ (unit(add(scale(from, c), scale(cross(axis, from), s))));
}

/**
 * One member's new velocity: the boid rules over its nearest mates, the pull home past `radius`
 * and back into the band, a slow circulation about home and a wander of its own, and, while
 * scattering, the flight from `self.flee` at speed.max; then the flight a gull can make under
 * that (fly).
 * @param {Member} self
 * @param {{ position: Vector, velocity: Vector }[]} mates the flock's other members (gatherMates)
 * @param {FlockConfig} config
 * @param {number} dt seconds since the last step
 * @param {number} now ms since the epoch
 * @returns {Vector}
 */
export function steer(self, mates, config, dt, now) {
    const p = self.position;
    const v = self.velocity;
    let a = { x: 0, y: 0, z: 0 };

    const near = mates
        .map(mate => ({ mate, offset: sub(mate.position, p) }))
        .map(m => ({ ...m, distance: length(m.offset) }))
        .filter(m => m.distance < PERCEPTION)
        .sort((m1, m2) => m1.distance - m2.distance)
        .slice(0, MAX_MATES);
    if (near.length > 0) {
        let centroid = { x: 0, y: 0, z: 0 };
        let heading = { x: 0, y: 0, z: 0 };
        for (const { mate, offset, distance } of near) {
            centroid = add(centroid, offset);
            heading = add(heading, mate.velocity);
            if (distance < SEPARATION) {
                // Two members at one point part sideways, each its own way.
                const away = unit(scale(offset, -1)) || { x: Math.cos(2 * Math.PI * (self.seed || 0)), y: 0, z: Math.sin(2 * Math.PI * (self.seed || 0)) };
                a = add(a, scale(away, WEIGHTS.separation * (SEPARATION - distance) / SEPARATION));
            }
        }
        a = add(a, scale(sub(scale(heading, 1 / near.length), v), WEIGHTS.alignment));
        a = add(a, scale(centroid, WEIGHTS.cohesion / near.length));
    }

    // Home is a cylinder: the band below governs the height.
    const fromHome = { x: p.x - config.home.x, y: 0, z: p.z - config.home.z };
    const out = length(fromHome);
    if (out > config.radius) {
        a = add(a, scale(fromHome, -WEIGHTS.home * (out - config.radius) / out));
    }
    if (out > 1) {
        const around = { x: -fromHome.z / out, y: 0, z: fromHome.x / out };
        a = add(a, scale(around, WEIGHTS.circulation));
    }

    const dy = p.y - config.home.y;
    if (Math.abs(dy) > config.band) {
        a.y -= Math.sign(dy) * Math.min(WEIGHTS.bandMax, WEIGHTS.band * (Math.abs(dy) - config.band));
    }
    a.y -= WEIGHTS.level * v.y;

    const turn = 2 * Math.PI * (now / WANDER_PERIOD_MS + (self.seed || 0));
    a = add(a, { x: WEIGHTS.wander * Math.cos(turn), y: 0, z: WEIGHTS.wander * Math.sin(turn) });

    if (self.flee) {
        const away = sub(p, self.flee);
        const flat = unit({ x: away.x, y: 0, z: away.z }) || unit({ x: v.x, y: 0, z: v.z }) || { x: 1, y: 0, z: 0 };
        a = add(a, scale(/** @type {Vector} */ (unit({ x: flat.x, y: 0.3, z: flat.z })), WEIGHTS.flee));
    }

    return fly(v, a, config, dt, self.flee ? config.speed.max : (config.speed.min + config.speed.max) / 2, self.seed || 0);
}

/**
 * The flight a member can make from velocity `v` under the steering acceleration `a`, as a gull
 * flies: the speed changes by at most MAX_SPEED_CHANGE per second, toward `target`, within
 * speed.min..max; the heading turns in the horizontal plane at a / v, at most MAX_TURN_RATE, and
 * a demand to slow down that the speed cannot meet turns the member instead (a gull cannot stop
 * to go back); the climb or dive follows the vertical demand within MAX_PITCH, changing at most
 * MAX_TURN_RATE.
 * @param {Vector} v
 * @param {Vector} a
 * @param {FlockConfig} config
 * @param {number} dt seconds
 * @param {number} target the speed to settle at, m/s
 * @param {number} seed which way to turn back from a demand straight behind
 * @returns {Vector}
 */
function fly(v, a, config, dt, target, seed) {
    const speedNow = length(v);
    const forward = unit(v) || unit({ x: a.x, y: 0, z: a.z }) || { x: 1, y: 0, z: 0 };
    const along = dot(a, forward) + WEIGHTS.cruise * (target - speedNow);
    const speed = clamp(speedNow + clamp(along, -MAX_SPEED_CHANGE, MAX_SPEED_CHANGE) * dt, config.speed.min, config.speed.max);
    const unmet = dt > 0 && along < 0 ? Math.max(0, (speed - speedNow) / dt - along) : 0;

    const flat = unit({ x: forward.x, y: 0, z: forward.z }) || { x: 1, y: 0, z: 0 };
    const left = { x: flat.z, y: 0, z: -flat.x };
    const side = dot(a, left);
    const towards = Math.abs(side) > 1e-6 ? Math.sign(side) : (seed < 0.5 ? 1 : -1);
    const flatSpeed = Math.max(speedNow * Math.hypot(forward.x, forward.z), config.speed.min / 2);
    const yawRate = clamp((side + towards * unmet) / flatSpeed, -MAX_TURN_RATE, MAX_TURN_RATE);
    const yaw = yawRate * dt;
    const heading = add(scale(flat, Math.cos(yaw)), scale(left, Math.sin(yaw)));

    const pitchNow = Math.asin(clamp(forward.y, -1, 1));
    const limit = Math.sin(MAX_PITCH);
    const pitchWanted = Math.asin(clamp((v.y + a.y * dt) / speed, -limit, limit));
    const pitch = pitchNow + clamp(pitchWanted - pitchNow, -MAX_TURN_RATE * dt, MAX_TURN_RATE * dt);
    return scale(add(scale(heading, Math.cos(pitch)), { x: 0, y: Math.sin(pitch), z: 0 }), speed);
}

/**
 * The leg to the roost: straight for it at cruise speed, slowing as it nears (to three quarters
 * of the distance per second, at least 1 m/s) so the heading's turn limit cannot hold it in
 * orbit round the point, until it is within ROOST_ARRIVAL.
 * @param {Member} self
 * @param {Vector} roost
 * @param {FlockConfig} config
 * @param {number} dt seconds since the last step
 * @returns {{ velocity: Vector, arrived: boolean }}
 */
export function roostLegFor(self, roost, config, dt) {
    const offset = sub(roost, self.position);
    const distance = length(offset);
    if (distance <= ROOST_ARRIVAL) {
        return { velocity: { x: 0, y: 0, z: 0 }, arrived: true };
    }
    const toward = scale(offset, 1 / distance);
    const forward = unit(self.velocity);
    const direction = forward ? turnToward(forward, toward, MAX_TURN_RATE * dt) : toward;
    const speed = Math.min((config.speed.min + config.speed.max) / 2, Math.max(1, 0.75 * distance));
    return { velocity: scale(direction, speed), arrived: false };
}

/**
 * The velocity a member leaves its roost with: level, toward home, at speed.min.
 * @param {Vector} position
 * @param {FlockConfig} config
 * @returns {Vector}
 */
export function takeOffVelocity(position, config) {
    const flat = unit({ x: config.home.x - position.x, y: 0, z: config.home.z - position.z }) || { x: 1, y: 0, z: 0 };
    return scale(flat, config.speed.min);
}

/**
 * The next state.
 * - `roost` (flying to the roost) and `roosted` (there, hidden, not steering) hold while it is
 *   night for a member with a roost; any other phase sends it back to `wheel`.
 * - `scatter` holds until its time is up, then the member wheels (and may scatter again).
 * - `wheel` scatters from a threat.
 * @param {State} state
 * @param {StateInputs} inputs
 * @returns {State} `state` itself when nothing changes
 */
export function nextState(state, inputs) {
    if (inputs.roosts && inputs.phase === "night") {
        if (state.name === "roosted") {
            return state;
        }
        if (state.name === "roost") {
            return inputs.arrived ? { name: "roosted" } : state;
        }
        return { name: "roost" };
    }
    let now = state;
    if (now.name === "roost" || now.name === "roosted" || (now.name === "scatter" && inputs.now >= now.until)) {
        now = { name: "wheel" };
    }
    if (now.name === "wheel" && inputs.threat) {
        return { name: "scatter", from: inputs.threat, until: inputs.now + inputs.scatterForMs };
    }
    return now;
}

// --- rotation --------------------------------------------------------------------------------

/**
 * @param {Rotation} a
 * @param {Rotation} b
 * @returns {Rotation} a * b, b applied first
 */
export function multiply(a, b) {
    return {
        x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
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
 * The rotation whose -z is along `velocity`, wings level to the horizon, then rolled about the
 * velocity into the turn by the coordinated-turn bank atan(lateral acceleration / g), at most
 * MAX_BANK: right wing down when turning right.
 * @param {Vector} velocity
 * @param {Vector} acceleration world frame, m/s^2
 * @returns {Rotation | null} null when the velocity is (nearly) zero: keep the rotation there is
 */
export function orientationFor(velocity, acceleration) {
    const front = unit(velocity);
    if (!front) {
        return null;
    }
    const right0 = unit(cross(front, { x: 0, y: 1, z: 0 })) || unit(cross(front, { x: 0, y: 0, z: 1 }));
    const r = /** @type {Vector} */ (right0);
    const u = cross(r, front);
    const bank = clamp(Math.atan2(dot(acceleration, r), GRAVITY), -MAX_BANK, MAX_BANK);
    const c = Math.cos(bank);
    const s = Math.sin(bank);
    const right = sub(scale(r, c), scale(u, s));
    const up = add(scale(u, c), scale(r, s));
    const back = scale(front, -1);
    return fromAxes(right, up, back);
}

/**
 * @param {Vector} x the rotated +x
 * @param {Vector} y the rotated +y
 * @param {Vector} z the rotated +z
 * @returns {Rotation} normalised, w >= 0
 */
function fromAxes(x, y, z) {
    const trace = x.x + y.y + z.z;
    /** @type {Rotation} */
    let q;
    if (trace > 0) {
        const s = 0.5 / Math.sqrt(trace + 1);
        q = { w: 0.25 / s, x: (y.z - z.y) * s, y: (z.x - x.z) * s, z: (x.y - y.x) * s };
    } else if (x.x > y.y && x.x > z.z) {
        const s = 2 * Math.sqrt(1 + x.x - y.y - z.z);
        q = { w: (y.z - z.y) / s, x: 0.25 * s, y: (y.x + x.y) / s, z: (z.x + x.z) / s };
    } else if (y.y > z.z) {
        const s = 2 * Math.sqrt(1 + y.y - x.x - z.z);
        q = { w: (z.x - x.z) / s, x: (y.x + x.y) / s, y: 0.25 * s, z: (z.y + y.z) / s };
    } else {
        const s = 2 * Math.sqrt(1 + z.z - x.x - y.y);
        q = { w: (x.y - y.x) / s, x: (z.x + x.z) / s, y: (z.y + y.z) / s, z: 0.25 * s };
    }
    const n = Math.hypot(q.x, q.y, q.z, q.w) * (q.w < 0 ? -1 : 1);
    return { x: q.x / n, y: q.y / n, z: q.z / n, w: q.w / n };
}

/**
 * The world-frame angular velocity that turns `from` into `to` in `dt` (the engine steps an
 * entity's rotation as dQ * rotation, so its angularVelocity is in the world frame): sent with
 * each edit so the rotation keeps turning between edits at the rate it just turned.
 * @param {Rotation} from
 * @param {Rotation} to
 * @param {number} dt seconds
 * @returns {Vector} rad/s
 */
export function angularVelocityBetween(from, to, dt) {
    let d = multiply(to, { x: -from.x, y: -from.y, z: -from.z, w: from.w });
    if (d.w < 0) {
        d = { x: -d.x, y: -d.y, z: -d.z, w: -d.w };
    }
    const sinHalf = Math.hypot(d.x, d.y, d.z);
    if (sinHalf < EPSILON || dt <= 0) {
        return { x: 0, y: 0, z: 0 };
    }
    const angle = 2 * Math.atan2(sinHalf, d.w);
    return scale({ x: d.x, y: d.y, z: d.z }, angle / sinHalf / dt);
}

// --- the day ---------------------------------------------------------------------------------

/**
 * @param {number} value
 * @param {number} period
 * @returns {number} value modulo period, in [0, period)
 */
function wrap(value, period) {
    return ((value % period) + period) % period;
}

/**
 * The sun's elevation at a moment, on an equinox day on the equator. The formula of
 * sunSky/sun-sky.mjs sunPosition: hour angle 15 deg an hour from noonUtc, elevation
 * 90 - |hour angle|.
 * @param {number} msSinceEpoch as Date.now()
 * @param {number} noonUtc the hour of solar noon in UTC, 0 to 24
 * @returns {number} degrees
 */
export function sunElevation(msSinceEpoch, noonUtc) {
    const hours = wrap(msSinceEpoch / MS_PER_HOUR, 24);
    const hourAngle = 180 - wrap(180 - DEGREES_PER_HOUR * (hours - noonUtc), 360);
    return 90 - Math.abs(hourAngle);
}

/**
 * As sunSky/sun-sky.mjs phase.
 * @param {number} elevation the sun's elevation in degrees
 * @returns {Phase}
 */
export function phase(elevation) {
    if (elevation > SUNSET_ELEVATION) {
        return "day";
    }
    return elevation >= DARK_ELEVATION ? "twilight" : "night";
}
