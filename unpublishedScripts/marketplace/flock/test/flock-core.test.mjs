//
//  flock-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The flock's pure logic: the configuration, the steering, the orientation, the state machine,
//  the roost leg and the phase of the day.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import {
    DEFAULT_RADIUS, MAX_BANK, MAX_PITCH, MAX_TURN_RATE, ROOST_ARRIVAL, add, angularVelocityBetween, dot, gatherMates, length,
    nextState, orientationFor, phase, readFlock, roostLegFor, rotate, scale, seedOf, steer, sub, sunElevation, takeOffVelocity,
    unit
} from "../flock-core.mjs";

const HOME = { x: 0, y: 24.6, z: -50 };
const ROOST = { x: -260, y: 0.5, z: -230 };
const DELOS = {
    flock: {
        name: "delos-gulls",
        home: HOME,
        radius: 40,
        band: 7.5,
        speed: { min: 7, max: 12 },
        hz: 5,
        scatter: { within: 10, forMs: 8000 },
        roost: ROOST
    },
    sunSky: { noonUtc: 16.0 }
};
const DT = 0.2;

/** @param {Record<string, unknown>} flock @param {Record<string, unknown>} [rest] */
function config(flock = {}, rest = {}) {
    const read = readFlock(JSON.stringify({ flock: { name: "test", home: HOME, ...flock }, ...rest }));
    assert.equal(read.error, null);
    return /** @type {import("../flock-core.mjs").FlockConfig} */ (read.config);
}

/** @param {import("../flock-core.mjs").Vector} a @param {import("../flock-core.mjs").Vector} b */
function angleBetween(a, b) {
    return Math.acos(Math.min(1, Math.max(-1, dot(a, b) / length(a) / length(b))));
}

/** @param {import("../flock-core.mjs").Vector} actual @param {import("../flock-core.mjs").Vector} expected */
function near(actual, expected, tolerance = 1e-6) {
    assert.ok(length(sub(actual, expected)) < tolerance, JSON.stringify(actual) + " is not " + JSON.stringify(expected));
}

// --- configuration ---------------------------------------------------------------------------

test("the Delos userData reads as written", () => {
    const { config: read, problems, error } = readFlock(JSON.stringify(DELOS));
    assert.equal(error, null);
    assert.deepEqual(problems, []);
    assert.deepEqual(read, {
        name: "delos-gulls", home: HOME, radius: 40, band: 7.5, speed: { min: 7, max: 12 }, hz: 5,
        scatter: { within: 10, forMs: 8000 }, roost: ROOST, noonUtc: 16
    });
});

test("every key but name and home has a default; no roost needs no noon", () => {
    const { config: read, problems } = readFlock(JSON.stringify({ flock: { name: "fish", home: HOME } }));
    assert.deepEqual(problems, []);
    assert.deepEqual(read, {
        name: "fish", home: HOME, radius: DEFAULT_RADIUS, band: 7.5, speed: { min: 7, max: 12 }, hz: 5,
        scatter: { within: 10, forMs: 8000 }, roost: null, noonUtc: null
    });
});

test("out of range numbers are clamped and reported; a non-number takes the default", () => {
    const { config: read, problems } = readFlock(JSON.stringify({
        flock: { name: "g", home: HOME, hz: 20, radius: "wide", speed: { min: 9, max: 4 }, scatter: { within: -1 } }
    }));
    assert.ok(read);
    assert.equal(read.hz, 10);
    assert.equal(read.radius, DEFAULT_RADIUS);
    assert.deepEqual(read.speed, { min: 9, max: 9 });
    assert.equal(read.scatter.within, 0);
    assert.equal(problems.length, 4);
    assert.match(problems.join("\n"), /flock\.hz must be from 1 to 10, got 20; using 10/);
    assert.equal(/** @type {any} */ (readFlock(JSON.stringify({ flock: { name: "g", home: HOME, hz: 0.2 } }))).config.hz, 1);
});

test("without a name, a home, or (with a roost) a noon, there is no flock", () => {
    for (const userData of ["", "not json", "[]", { other: 1 }, { flock: { home: HOME } }, { flock: { name: "", home: HOME } },
        { flock: { name: "g", home: { x: 0, y: "up", z: 0 } } }, { flock: { name: "g" } },
        { flock: { name: "g", home: HOME, roost: ROOST } }, { flock: { name: "g", home: HOME, roost: ROOST }, sunSky: { noonUtc: 24 } }]) {
        const read = readFlock(typeof userData === "string" ? userData : JSON.stringify(userData));
        assert.equal(read.config, null, JSON.stringify(userData));
        assert.equal(typeof read.error, "string");
    }
    assert.match(String(readFlock(JSON.stringify({ flock: { name: "g", home: HOME, roost: ROOST } })).error), /sunSky\.noonUtc/);
});

test("a roost that is not a position is reported and the member never roosts", () => {
    const { config: read, problems } = readFlock(JSON.stringify({ flock: { name: "g", home: HOME, roost: [1, 2, 3] } }));
    assert.equal(read && read.roost, null);
    assert.match(problems[0], /flock\.roost/);
});

test("seedOf is in [0, 1) and differs between members", () => {
    const a = seedOf("{11111111-0000-4000-8000-000000000001}");
    const b = seedOf("{11111111-0000-4000-8000-000000000002}");
    assert.ok(a >= 0 && a < 1 && b >= 0 && b < 1);
    assert.notEqual(a, b);
});

// --- steering --------------------------------------------------------------------------------

// At home, level, at cruise: no home pull, no circulation, no band pull. Only the wander and
// the mates remain, and comparing with and without a mate cancels the wander.
const AT_HOME = { position: HOME, velocity: { x: 9.5, y: 0, z: 0 }, seed: 0 };

test("separation pushes a member away from a mate that is too close", () => {
    const c = config();
    const alone = steer(AT_HOME, [], c, DT, 0);
    const crowded = steer(AT_HOME, [{ position: add(HOME, { x: 0, y: 0, z: 1 }), velocity: AT_HOME.velocity }], c, DT, 0);
    assert.ok(crowded.z < alone.z - 0.05, "turned away, toward -z");
});

test("cohesion pulls a member toward mates at a distance", () => {
    const c = config();
    const alone = steer(AT_HOME, [], c, DT, 0);
    const mates = [12, 14, 16].map(z => ({ position: add(HOME, { x: 0, y: 0, z }), velocity: AT_HOME.velocity }));
    const joined = steer(AT_HOME, mates, c, DT, 0);
    assert.ok(joined.z > alone.z + 0.01, "turned toward +z");
});

test("alignment turns a member toward its mates' heading", () => {
    const c = config();
    const alone = steer(AT_HOME, [], c, DT, 0);
    const aligned = steer(AT_HOME, [{ position: add(HOME, { x: 8, y: 0, z: 0 }), velocity: { x: 0, y: 0, z: 9.5 } }], c, DT, 0);
    assert.ok(aligned.z > alone.z + 0.05);
});

test("mates beyond sight and beyond the nearest 24 are not steered by", () => {
    const c = config();
    const alone = steer(AT_HOME, [], c, DT, 0);
    near(steer(AT_HOME, [{ position: add(HOME, { x: 0, y: 0, z: 25 }), velocity: AT_HOME.velocity }], c, DT, 0), alone);
    // 24 mates straight ahead at the same velocity, and a 25th further off to the side, which is not seen.
    const ahead = Array.from({ length: 24 }, (_, i) => ({ position: add(HOME, { x: 8 + i * 0.1, y: 0, z: 0 }), velocity: AT_HOME.velocity }));
    const side = { position: add(HOME, { x: 0, y: 0, z: 15 }), velocity: AT_HOME.velocity };
    near(steer(AT_HOME, [...ahead, side], c, DT, 0), steer(AT_HOME, ahead, c, DT, 0));
});

test("past the radius a member is pulled home, and harder further out", () => {
    const c = config();
    const tangential = { x: 0, y: 0, z: 9.5 };
    const inward = (/** @type {number} */ out) => {
        const self = { position: add(HOME, { x: out, y: 0, z: 0 }), velocity: tangential, seed: 0 };
        return -steer(self, [], c, DT, 0).x + steer(self, [], config({ radius: 1000 }), DT, 0).x;
    };
    assert.ok(Math.abs(inward(30)) < 1e-9, "inside the radius, no pull");
    assert.ok(inward(50) > 0.05);
    assert.ok(inward(70) > inward(50));
});

test("the band pulls a member back to its height and leaves it level inside", () => {
    const c = config();
    const at = (/** @type {number} */ dy) => steer({ position: add(HOME, { x: 0, y: dy, z: 0 }), velocity: AT_HOME.velocity, seed: 0 }, [], c, DT, 0);
    assert.ok(at(15).y < -0.05, "above the band, it sinks");
    assert.ok(at(-15).y > 0.05, "below the band, it climbs");
    assert.equal(at(3).y, 0, "inside the band, level flight stays level");
});

test("the speed is held between speed.min and speed.max", () => {
    const c = config();
    for (const speed of [0, 1, 30, 100]) {
        const v = steer({ position: HOME, velocity: { x: speed, y: 0, z: 0 } }, [], c, DT, 0);
        const s = length(v);
        assert.ok(s >= 7 - 1e-9 && s <= 12 + 1e-9, speed + " became " + s);
    }
});

test("a scattering member speeds up to speed.max", () => {
    const c = config();
    let self = { position: HOME, velocity: { x: 9.5, y: 0, z: 0 }, seed: 0, flee: add(HOME, { x: -5, y: 0, z: 0 }) };
    for (let i = 0; i < 10; i++) {
        self = { ...self, velocity: steer(self, [], c, DT, 0) };
    }
    assert.ok(Math.abs(length(self.velocity) - 12) < 1e-9);
});

test("the heading turns by at most MAX_TURN_RATE a second", () => {
    const c = config();
    // Fleeing a point beside it asks for a sharper turn than a gull can make.
    const v = steer({ ...AT_HOME, flee: add(HOME, { x: 0, y: 0, z: 2 }) }, [], c, DT, 0);
    const yaw = Math.atan2(-v.z, v.x);
    assert.ok(Math.abs(yaw - MAX_TURN_RATE * DT) < 1e-9, "turned " + yaw);
});

test("a member fleeing from straight ahead turns round rather than stopping", () => {
    const c = config();
    const flee = add(HOME, { x: 8, y: 0, z: 0 });
    let self = { ...AT_HOME, flee };
    for (let step = 0; step < 4 / DT; step++) {
        const v = steer(self, [], c, DT, 0);
        const turned = angleBetween({ x: v.x, y: 0, z: v.z }, { x: self.velocity.x, y: 0, z: self.velocity.z });
        assert.ok(turned <= MAX_TURN_RATE * DT + 1e-9);
        assert.ok(length(v) >= 7 - 1e-9);
        self = { ...self, velocity: v, position: add(self.position, scale(v, DT)) };
    }
    const away = sub(self.position, flee);
    assert.ok(dot(self.velocity, away) > 0.9 * length(self.velocity) * length(away), "heading away: " + JSON.stringify(self.velocity));
    assert.ok(length(away) > 25);
});

test("a climb is held within MAX_PITCH, and a steeper flight is brought back to it", () => {
    const c = config();
    const pitchOf = (/** @type {import("../flock-core.mjs").Vector} */ v) => Math.asin(v.y / length(v));
    let self = { position: add(HOME, { x: 0, y: -200, z: 0 }), velocity: { x: 9.5, y: 0, z: 0 }, seed: 0 };
    let steepest = 0;
    for (let step = 0; step < 50; step++) {
        self = { ...self, velocity: steer(self, [], c, DT, 0) };
        steepest = Math.max(steepest, pitchOf(self.velocity));
    }
    assert.ok(steepest > 10 * Math.PI / 180 && steepest <= MAX_PITCH + 1e-9, "climbs at " + steepest);
    let vertical = { position: HOME, velocity: { x: 0.1, y: 9, z: 0 }, seed: 0 };
    for (let step = 0; step < 1 / DT; step++) {
        vertical = { ...vertical, velocity: steer(vertical, [], c, DT, 0) };
    }
    assert.ok(pitchOf(vertical.velocity) <= MAX_PITCH + 1e-9);
});

test("gatherMates skips this member, the stale and the roosting, and carries the rest forward", () => {
    const v = { x: 10, y: 0, z: 0 };
    /** @type {[string, import("../flock-core.mjs").RegistryEntry][]} */
    const entries = [
        ["self", { position: HOME, velocity: v, state: "wheel", seen: 1000 }],
        ["fresh", { position: HOME, velocity: v, state: "scatter", seen: 800 }],
        ["stale", { position: HOME, velocity: v, state: "wheel", seen: 300 }],
        ["home-bound", { position: HOME, velocity: v, state: "roost", seen: 1000 }],
        ["asleep", { position: HOME, velocity: v, state: "roosted", seen: 1000 }]
    ];
    const mates = gatherMates(entries, "self", 1000, 600);
    assert.equal(mates.length, 1);
    near(mates[0].position, add(HOME, { x: 2, y: 0, z: 0 }));
});

test("a simulated flock of 12 stays near home, in its band, apart, and in its speed range", () => {
    const c = config();
    let members = Array.from({ length: 12 }, (_, i) => ({
        position: add(HOME, { x: (i % 4) * 6 - 9, y: i % 3, z: Math.floor(i / 4) * 6 }),
        velocity: { x: 8, y: 0, z: 0 },
        seed: seedOf("gull" + i)
    }));
    let worst = { out: 0, dy: 0, apart: Infinity };
    for (let step = 0; step < 300 / DT; step++) {
        const now = step * DT * 1000;
        const velocities = members.map((m, i) => steer(m, members.filter((_, j) => j !== i), c, DT, now));
        members = members.map((m, i) => ({ ...m, velocity: velocities[i], position: add(m.position, scale(velocities[i], DT)) }));
        if (step < 30 / DT) {
            continue;
        }
        for (const [i, m] of members.entries()) {
            worst.out = Math.max(worst.out, Math.hypot(m.position.x - HOME.x, m.position.z - HOME.z));
            worst.dy = Math.max(worst.dy, Math.abs(m.position.y - HOME.y));
            const s = length(m.velocity);
            assert.ok(s >= 7 - 1e-9 && s <= 12 + 1e-9);
            for (const other of members.slice(i + 1)) {
                worst.apart = Math.min(worst.apart, length(sub(m.position, other.position)));
            }
        }
    }
    assert.ok(worst.out < 40 + 20, "out to " + worst.out);
    assert.ok(worst.dy < 7.5 + 4, "off its height by " + worst.dy);
    assert.ok(worst.apart > 1, "as close as " + worst.apart);
});

// --- orientation -----------------------------------------------------------------------------

test("the nose is along the velocity, wings level when flying straight", () => {
    const identity = /** @type {import("../flock-core.mjs").Rotation} */ (orientationFor({ x: 0, y: 0, z: -9 }, { x: 0, y: 0, z: 0 }));
    near({ x: identity.x, y: identity.y, z: identity.z }, { x: 0, y: 0, z: 0 });
    assert.ok(Math.abs(identity.w - 1) < 1e-9);
    for (const velocity of [{ x: 9, y: 0, z: 0 }, { x: -3, y: 2, z: 7 }, { x: 0, y: 0, z: 9 }]) {
        const q = /** @type {import("../flock-core.mjs").Rotation} */ (orientationFor(velocity, { x: 0, y: 0, z: 0 }));
        near(rotate(q, { x: 0, y: 0, z: -1 }), /** @type {any} */ (unit(velocity)));
        assert.ok(Math.abs(rotate(q, { x: 1, y: 0, z: 0 }).y) < 1e-9, "the wings are level");
        assert.ok(Math.abs(Math.hypot(q.x, q.y, q.z, q.w) - 1) < 1e-9);
    }
    assert.equal(orientationFor({ x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 }), null);
});

test("a member banks into its turn by atan(a / g), at most MAX_BANK", () => {
    const velocity = { x: 0, y: 0, z: -9 };
    const bankFor = (/** @type {number} */ lateral) => {
        const q = /** @type {import("../flock-core.mjs").Rotation} */ (orientationFor(velocity, { x: lateral, y: 0, z: 0 }));
        near(rotate(q, { x: 0, y: 0, z: -1 }), { x: 0, y: 0, z: -1 });
        return -Math.asin(rotate(q, { x: 1, y: 0, z: 0 }).y);
    };
    // Turning right (toward +x, flying -z): the right wing goes down.
    assert.ok(Math.abs(bankFor(9.81 * Math.tan(10 * Math.PI / 180)) - 10 * Math.PI / 180) < 1e-9);
    assert.ok(bankFor(-2) < 0, "turning left, the left wing goes down");
    assert.ok(Math.abs(bankFor(100) - MAX_BANK) < 1e-9);
    assert.ok(Math.abs(bankFor(-100) + MAX_BANK) < 1e-9);
});

test("angularVelocityBetween is the world-frame rate from one rotation to the next", () => {
    const quarterYaw = { x: 0, y: Math.sin(Math.PI / 4), z: 0, w: Math.cos(Math.PI / 4) };
    near(angularVelocityBetween({ x: 0, y: 0, z: 0, w: 1 }, quarterYaw, 2), { x: 0, y: Math.PI / 4, z: 0 });
    near(angularVelocityBetween(quarterYaw, quarterYaw, 0.2), { x: 0, y: 0, z: 0 });
    // The short way round, whichever sign the quaternions carry.
    const negated = { x: -quarterYaw.x, y: -quarterYaw.y, z: -quarterYaw.z, w: -quarterYaw.w };
    near(angularVelocityBetween({ x: 0, y: 0, z: 0, w: 1 }, negated, 2), { x: 0, y: Math.PI / 4, z: 0 });
});

// --- states and the roost --------------------------------------------------------------------

const WHEEL = /** @type {import("../flock-core.mjs").State} */ ({ name: "wheel" });
/** @param {Partial<import("../flock-core.mjs").StateInputs>} inputs */
const at = inputs => ({ now: 1000, phase: /** @type {import("../flock-core.mjs").Phase} */ ("day"), roosts: true, threat: null,
    arrived: false, scatterForMs: 8000, ...inputs });

test("an avatar scatters a wheeling member for scatter.forMs, then it wheels", () => {
    const avatar = { x: 1, y: 2, z: 3 };
    const scatter = nextState(WHEEL, at({ threat: avatar }));
    assert.deepEqual(scatter, { name: "scatter", from: avatar, until: 9000 });
    assert.equal(nextState(scatter, at({ now: 8999 })), scatter);
    assert.equal(nextState(scatter, at({ now: 8999, threat: { x: 9, y: 9, z: 9 } })), scatter, "a new threat does not extend it");
    assert.deepEqual(nextState(scatter, at({ now: 9000 })), WHEEL);
    assert.deepEqual(nextState(scatter, at({ now: 9000, threat: avatar })), { name: "scatter", from: avatar, until: 17000 });
    assert.equal(nextState(WHEEL, at({})), WHEEL);
});

test("at night a member with a roost flies there, arrives, and takes off when it is light", () => {
    const roost = nextState(WHEEL, at({ phase: "night" }));
    assert.deepEqual(roost, { name: "roost" });
    assert.deepEqual(nextState({ name: "scatter", from: HOME, until: 9e9 }, at({ phase: "night" })), roost, "night outranks a scatter");
    assert.equal(nextState(roost, at({ phase: "night", threat: HOME })), roost, "no scatter on the way");
    const roosted = nextState(roost, at({ phase: "night", arrived: true }));
    assert.deepEqual(roosted, { name: "roosted" });
    assert.equal(nextState(roosted, at({ phase: "night" })), roosted);
    assert.deepEqual(nextState(roosted, at({ phase: "twilight" })), WHEEL, "takes off at first light");
    assert.deepEqual(nextState(roosted, at({ phase: "day" })), WHEEL);
    assert.deepEqual(nextState(roost, at({ phase: "twilight" })), WHEEL, "turns back if it is light before it arrives");
    assert.deepEqual(nextState(WHEEL, at({ phase: "twilight" })), WHEEL, "dusk is not yet night");
});

test("a member without a roost ignores the night", () => {
    assert.equal(nextState(WHEEL, at({ phase: "night", roosts: false })), WHEEL);
    assert.equal(nextState(WHEEL, at({ phase: "night", roosts: false, threat: HOME })).name, "scatter");
});

test("the roost leg heads for the roost at cruise and arrives within ROOST_ARRIVAL", () => {
    const c = config({ roost: ROOST }, { sunSky: { noonUtc: 16 } });
    const far = roostLegFor({ position: HOME, velocity: unit(sub(ROOST, HOME)) || HOME }, ROOST, c, DT);
    assert.equal(far.arrived, false);
    assert.ok(Math.abs(length(far.velocity) - 9.5) < 1e-9);
    assert.deepEqual(roostLegFor({ position: add(ROOST, { x: 1.5, y: 0, z: 0 }), velocity: { x: 9, y: 0, z: 0 } }, ROOST, c, DT),
        { velocity: { x: 0, y: 0, z: 0 }, arrived: true });
    for (const start of [{ position: add(HOME, { x: 30, y: 0, z: 0 }), velocity: { x: 9, y: 0, z: 3 } },
        { position: add(ROOST, { x: 5, y: 9, z: 0 }), velocity: { x: 12, y: 0, z: 0 } },
        { position: add(ROOST, { x: -1, y: 2.5, z: -1 }), velocity: { x: -10, y: 0, z: 0 } }]) {
        let self = start;
        let seconds = 0;
        for (; seconds < 120; seconds += DT) {
            const leg = roostLegFor(self, ROOST, c, DT);
            if (leg.arrived) {
                break;
            }
            self = { position: add(self.position, scale(leg.velocity, DT)), velocity: leg.velocity };
        }
        assert.ok(seconds < 120, "never arrived from " + JSON.stringify(start.position));
        assert.ok(length(sub(self.position, ROOST)) <= ROOST_ARRIVAL);
    }
});

test("a member takes off level, toward home, at speed.min", () => {
    const c = config({ roost: ROOST }, { sunSky: { noonUtc: 16 } });
    const v = takeOffVelocity(ROOST, c);
    assert.equal(v.y, 0);
    assert.ok(Math.abs(length(v) - 7) < 1e-9);
    assert.ok(dot(v, sub(HOME, ROOST)) > 0);
});

// --- the day ---------------------------------------------------------------------------------

test("the phase at a few UTC hours with noon at 16:00 UTC", () => {
    const at = (/** @type {string} */ time) => Date.parse("2026-10-31T" + time + ":00Z");
    assert.equal(sunElevation(at("16:00"), 16), 90);
    assert.equal(sunElevation(at("04:00"), 16), -90);
    for (const [time, elevation, expected] of /** @type {[string, number, string][]} */ ([
        ["16:00", 90, "day"], ["13:00", 45, "day"], ["22:00", 0, "day"], ["22:10", -2.5, "twilight"],
        ["22:30", -7.5, "night"], ["04:00", -90, "night"], ["09:50", -2.5, "twilight"], ["10:00", 0, "day"]
    ])) {
        assert.ok(Math.abs(sunElevation(at(time), 16) - elevation) < 1e-9, time);
        assert.equal(phase(sunElevation(at(time), 16)), expected, time);
    }
    assert.equal(phase(-0.833), "twilight");
    assert.equal(phase(-6), "twilight");
    assert.equal(phase(-6.01), "night");
});
