//
//  flock-server.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A flock member's serverScripts against fake Entities, Messages, Script and AvatarList globals.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { clock, fakeEngine } from "./fake-engine.mjs";

const { default: Flock, CHANNEL, ROOSTED_TICK_MS, flocks } = await import("../flock-server.mjs");

const GULL_1 = "{9a110000-0000-4000-8000-000000000001}";
const GULL_2 = "{9a110000-0000-4000-8000-000000000002}";
const HOME = { x: 0, y: 24.6, z: -50 };
const ROOST = { x: -260, y: 0.5, z: -230 };
const DAY = "2026-10-31T16:00:00Z";
const NIGHT = "2026-10-31T03:00:00Z";

/** @param {Record<string, unknown>} [flock] @param {Record<string, unknown>} [properties] */
function gull(flock = {}, properties = {}) {
    return {
        type: "Model",
        position: HOME,
        rotation: { x: 0, y: 0, z: 0, w: 1 },
        velocity: { x: 0, y: 0, z: 0 },
        visible: true,
        locked: false,
        userData: JSON.stringify({ flock: { name: "delos-gulls", home: HOME, roost: ROOST, ...flock }, sunSky: { noonUtc: 16 } }),
        ...properties
    };
}

/**
 * @param {string} iso
 * @param {Record<string, Record<string, unknown>>} entities
 */
function loaded(iso, entities) {
    flocks.clear();
    clock.set(iso);
    const engine = fakeEngine(entities);
    const members = Object.keys(entities).map(id => {
        const member = new Flock();
        member.preload(id);
        return member;
    });
    /** Moves the clock one tick on and runs every timer. */
    const step = (ms = 200) => {
        clock.advance(ms);
        engine.runTimers();
    };
    return { engine, members, step };
}

test("a member ticks at hz and edits only its own position, rotation, velocity and angularVelocity", () => {
    const { engine, step } = loaded(DAY, { [GULL_1]: gull() });
    assert.deepEqual([...engine.intervals.values()], [200]);
    assert.equal(engine.edits.length, 0, "no edit at load");
    step();
    step();
    assert.equal(engine.edits.length, 2);
    for (const [id, edit] of engine.edits) {
        assert.equal(id, GULL_1);
        assert.deepEqual(Object.keys(edit).sort(), ["angularVelocity", "position", "rotation", "velocity"]);
    }
    const [, last] = engine.edits[1];
    const speed = Math.hypot(last.velocity.x, last.velocity.y, last.velocity.z);
    assert.ok(speed >= 7 && speed <= 12, "flying at " + speed);
    assert.deepEqual(engine.subscribed, [CHANNEL]);
});

test("an edit puts the member where its last velocity carried it", () => {
    const { engine, step } = loaded(DAY, { [GULL_1]: gull() });
    step();
    step(400);
    const [first, second] = engine.editsOf(GULL_1);
    const expected = {
        x: first.position.x + first.velocity.x * 0.4,
        y: first.position.y + first.velocity.y * 0.4,
        z: first.position.z + first.velocity.z * 0.4
    };
    assert.ok(Math.hypot(second.position.x - expected.x, second.position.y - expected.y, second.position.z - expected.z) < 1e-9);
});

test("members of one flock share a registry by name; another name is another flock", () => {
    const { engine, members, step } = loaded(DAY, {
        [GULL_1]: gull(),
        [GULL_2]: gull({}, { position: { x: 3, y: 24.6, z: -50 } }),
        "{f15h0000-0000-4000-8000-000000000003}": gull({ name: "harbour-fish", roost: undefined })
    });
    step();
    assert.deepEqual([...(flocks.get("delos-gulls") || new Map()).keys()].sort(), [GULL_1, GULL_2]);
    assert.equal(flocks.get("harbour-fish")?.size, 1);
    members[0].unload();
    assert.deepEqual([...(flocks.get("delos-gulls") || new Map()).keys()], [GULL_2]);
    assert.equal(engine.timers.size, 2);
    assert.equal(engine.handlers.size, 1, "the fish never listened; the other gull still does");
    assert.deepEqual(engine.unsubscribed, [], "the subscription is the whole server's; it is not dropped");
    members[1].unload();
    members[2].unload();
    assert.equal(flocks.size, 0);
    assert.equal(engine.timers.size, 0);
});

test("without name or home, or locked, a member says so and does nothing", () => {
    for (const properties of [{ userData: "" }, { userData: JSON.stringify({ flock: { home: HOME } }) }, { locked: true }]) {
        const { engine, step } = loaded(DAY, { [GULL_1]: gull({}, properties) });
        step();
        assert.equal(engine.timers.size, 0);
        assert.equal(engine.edits.length, 0);
        assert.equal(engine.printed.length, 1);
        assert.ok(engine.printed[0].includes(GULL_1));
        assert.match(engine.printed[0], /does not fly/);
    }
});

test("an avatar close by scatters the member to speed.max; one avatar object serves every tick", () => {
    const { engine, step } = loaded(DAY, { [GULL_1]: gull() });
    engine.avatars.set("{a0000000-0000-4000-8000-000000000001}", { x: 4, y: 24.6, z: -50 });
    for (let i = 0; i < 20; i++) {
        step();
    }
    const [last] = engine.editsOf(GULL_1).slice(-1);
    assert.ok(Math.abs(Math.hypot(last.velocity.x, last.velocity.y, last.velocity.z) - 12) < 1e-9);
    assert.equal(engine.getAvatarCalls, 1);
    engine.avatars.clear();
    step();
    // Where the member will be after the next step, whose time is held to 1 s.
    const { position, velocity } = /** @type {any} */ (engine.tree.get(GULL_1));
    engine.avatars.set("{a0000000-0000-4000-8000-000000000001}", { x: position.x + velocity.x, y: position.y + velocity.y, z: position.z + velocity.z });
    clock.advance(8000);
    step();
    assert.equal(engine.getAvatarCalls, 2, "an avatar that left and came back is asked for again");
});

test("at night it flies to its roost, hides there and stops editing; at first light it takes off", () => {
    const near = { x: ROOST.x + 6, y: ROOST.y + 3, z: ROOST.z };
    const { engine, members, step } = loaded(NIGHT, { [GULL_1]: gull({}, { position: near }) });
    let ticks = 0;
    while (engine.tree.get(GULL_1)?.visible !== false && ticks < 100) {
        step();
        ticks++;
    }
    assert.ok(ticks < 100, "never arrived");
    const [arrival] = engine.editsOf(GULL_1).slice(-1);
    assert.deepEqual(arrival.velocity, { x: 0, y: 0, z: 0 });
    assert.deepEqual(arrival.angularVelocity, { x: 0, y: 0, z: 0 });
    assert.equal(arrival.visible, false);
    assert.ok(Math.hypot(arrival.position.x - ROOST.x, arrival.position.y - ROOST.y, arrival.position.z - ROOST.z) <= 2);
    assert.deepEqual([...engine.intervals.values()], [ROOSTED_TICK_MS]);
    assert.equal(flocks.size, 0, "a roosted member is not a mate");

    const edits = engine.edits.length;
    for (let i = 0; i < 5; i++) {
        step(ROOSTED_TICK_MS);
    }
    assert.equal(engine.edits.length, edits, "no edits while roosted");

    engine.send("other.channel", JSON.stringify({ type: "phase", phase: "day" }));
    engine.send(CHANNEL, "not json");
    engine.send(CHANNEL, JSON.stringify({ type: "phase", phase: "noon" }));
    step(ROOSTED_TICK_MS);
    assert.equal(engine.edits.length, edits, "only a phase on the channel counts");

    engine.send(CHANNEL, JSON.stringify({ type: "phase", phase: "twilight", azimuth: 90, elevation: -3 }));
    step(ROOSTED_TICK_MS);
    const [takeOff] = engine.editsOf(GULL_1).slice(-1);
    assert.equal(takeOff.visible, true);
    assert.ok(Math.hypot(takeOff.velocity.x, takeOff.velocity.z) >= 7 - 1e-9);
    assert.equal(members[0].state.name, "wheel");
    assert.deepEqual([...engine.intervals.values()], [200]);
    step();
    assert.equal("visible" in engine.editsOf(GULL_1).slice(-1)[0], false, "shown once");
});

test("a member without a roost ignores the night and listens to no phase", () => {
    const { engine, members, step } = loaded(NIGHT, { [GULL_1]: gull({ roost: undefined }, { visible: true }) });
    step();
    assert.deepEqual(engine.subscribed, []);
    assert.equal(members[0].state.name, "wheel");
    assert.equal(engine.editsOf(GULL_1).length, 1);
});

test("a member found hidden by day is shown with its first edit", () => {
    const { engine, step } = loaded(DAY, { [GULL_1]: gull({}, { visible: false, position: ROOST }) });
    step();
    assert.equal(engine.editsOf(GULL_1)[0].visible, true);
});
