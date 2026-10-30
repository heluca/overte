//
//  bar-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import {
    BarCore, RateLimiter, LIMIT_DEFAULTS, MENU_LIMIT, DEFAULT_GLASS_DIMENSIONS, channelFor, parseConfig, parseObject,
    staffOf, isCaller, isStaff, findItem, rotate, counterOf, glassSpot, glassProperties
} from "../bar-core.mjs";

const BAR = "{ba400000-0000-4000-8000-000000000000}";
const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";
const BARTENDER = "{cccccccc-0000-4000-8000-000000000003}";
const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";
const T0 = 1_760_000_000_000;

const MENU = [
    { name: "Bordeaux", kind: "wine", glass: "wine" },
    { name: "Negroni", kind: "cocktail", glass: "rocks" },
    { name: "Ouzo", kind: "spirit", glass: "highball" }
];

function config(userData = {}) {
    const { config, problems } = parseConfig({ menu: MENU, ...userData }, BAR);
    assert.deepEqual(problems, []);
    return config;
}

function fakeWorld() {
    let glass = 0;
    return {
        t: T0,
        staffValue: BARTENDER,
        present: [ALICE, BOB, BARTENDER],
        names: { [ALICE]: ["alice", "Alice"], [BOB]: ["", ""], [BARTENDER]: ["Bartender", "Bartender"] },
        published: [],
        announced: [],
        logged: [],
        spawned: [],
        deleted: [],
        spawnFails: false,
        now() { return this.t; },
        publish(status) { this.published.push(status); },
        announce(message) { this.announced.push(message); },
        log(text) { this.logged.push(text); },
        staff() { return this.staffValue; },
        avatars() { return this.present; },
        namesOf(session) { return this.names[session] || ["", ""]; },
        spawnGlass(order) {
            if (this.spawnFails) {
                return null;
            }
            this.spawned.push(order);
            return "{91a55000-0000-4000-8000-" + String(++glass).padStart(12, "0") + "}";
        },
        deleteEntity(id) { this.deleted.push(id); },
        get status() { return this.published[this.published.length - 1]; },
        get message() { return this.announced[this.announced.length - 1]; },
        advance(seconds) { this.t += seconds * 1000; }
    };
}

function bar(userData) {
    const world = fakeWorld();
    const core = new BarCore(world, config(userData));
    core.start();
    return { world, core };
}

test("parseObject takes only a JSON object", () => {
    assert.deepEqual(parseObject('{"a":1}'), { a: 1 });
    for (const text of ["", "[]", "null", "42", "{", undefined]) {
        assert.deepEqual(parseObject(text), {}, String(text));
    }
});

test("parseConfig: the menu is cleaned, deduplicated and capped", () => {
    const { config, problems } = parseConfig({
        menu: [
            { name: "  Negroni\n", kind: "cocktail", glass: "rocks" },
            { name: "negroni" },
            { name: "" },
            "Ouzo",
            { name: "x".repeat(60), kind: 7 }
        ]
    }, BAR);
    assert.deepEqual(config.menu, [
        { name: "Negroni", kind: "cocktail", glass: "rocks" },
        { name: "x".repeat(40), kind: "", glass: "" }
    ]);
    assert.equal(problems.length, 3);
    assert.match(problems[0], /menu\[1\] repeats "negroni"/);
    const many = parseConfig({ menu: Array.from({ length: MENU_LIMIT + 5 }, (_, i) => ({ name: "Drink " + i })) }, BAR);
    assert.equal(many.config.menu.length, MENU_LIMIT);
    assert.match(many.problems[0], /only the first 50/);
});

test("parseConfig: no menu is reported and refuses every order", () => {
    const { config, problems } = parseConfig({}, BAR);
    assert.deepEqual(config.menu, []);
    assert.match(problems[0], /menu must be an array/);
});

test("parseConfig: defaults, the channel and the limits", () => {
    const { config: c } = parseConfig({ menu: [] }, BAR);
    assert.equal(c.channel, channelFor(BAR));
    assert.equal(c.channel, "bar:" + BAR);
    assert.equal(c.counter, null);
    assert.equal(c.staffByName, false);
    for (const key of Object.keys(LIMIT_DEFAULTS)) {
        assert.equal(c[key], LIMIT_DEFAULTS[key], key);
    }
    const { config: set } = parseConfig({ menu: [], channel: "delos:bar", maxOpenOrders: 6, orderTtlS: 300, staffByName: true }, BAR);
    assert.equal(set.channel, "delos:bar");
    assert.equal(set.maxOpenOrders, 6);
    assert.equal(set.orderTtlS, 300);
    assert.equal(set.staffByName, true);
    const { config: bad, problems } = parseConfig({
        menu: [], channel: "has space", maxOpenOrders: 0, orderTtlS: 1.5, callsPerWindow: "10", staffByName: "yes"
    }, BAR);
    assert.equal(bad.channel, channelFor(BAR));
    assert.equal(bad.maxOpenOrders, LIMIT_DEFAULTS.maxOpenOrders);
    assert.equal(bad.orderTtlS, LIMIT_DEFAULTS.orderTtlS);
    assert.equal(bad.callsPerWindow, LIMIT_DEFAULTS.callsPerWindow);
    assert.equal(bad.staffByName, false);
    assert.equal(problems.length, 5);
});

test("parseConfig: glasses need an asset url and a sane size", () => {
    const { config: c, problems } = parseConfig({
        menu: [],
        glasses: {
            wine: { url: "https://assets.example.test/glass/wine.glb", dimensions: { x: 0.08, y: 0.2, z: 0.08 } },
            rocks: { url: "atp:/glass/rocks.glb" },
            flute: { url: "javascript:alert(1)" },
            coupe: { url: "https://assets.example.test/coupe.glb", dimensions: { x: 0, y: 1, z: 1 } }
        }
    }, BAR);
    assert.deepEqual(Object.keys(c.glasses), ["wine", "rocks"]);
    assert.deepEqual(c.glasses.rocks.dimensions, DEFAULT_GLASS_DIMENSIONS);
    assert.equal(problems.length, 2);
});

test("parseConfig: the counter is all three numbers or the box top", () => {
    assert.deepEqual(parseConfig({ menu: [], counter: { y: 0.38, z: -0.15, halfLength: 2.9 } }, BAR).config.counter,
        { y: 0.38, z: -0.15, halfLength: 2.9 });
    const { config: c, problems } = parseConfig({ menu: [], counter: { y: 0.38 } }, BAR);
    assert.equal(c.counter, null);
    assert.match(problems[0], /counter must be/);
    assert.deepEqual(counterOf(null, { x: 6, y: 1.44, z: 1.4 }), { y: 0.72, z: 0, halfLength: 3 });
});

test("staffOf, isCaller and isStaff", () => {
    assert.equal(staffOf({}), null);
    assert.equal(staffOf({ staff: "  " }), null);
    assert.equal(staffOf({ staff: BARTENDER }), BARTENDER);
    assert.equal(isCaller(ALICE), true);
    for (const caller of [undefined, "", NULL_UUID, "Alice", 42]) {
        assert.equal(isCaller(caller), false, String(caller));
    }
    const names = session => (session === ALICE ? ["Bartender", "Alice"] : ["", ""]);
    // The engine's forms differ: braces and case do not matter
    assert.equal(isStaff(BARTENDER, BARTENDER.slice(1, -1).toUpperCase(), false, names), true);
    assert.equal(isStaff(ALICE, BARTENDER, true, names), false);
    assert.equal(isStaff(ALICE, null, true, names), false);
    // A display name is self-chosen: it counts only where staffByName is set
    assert.equal(isStaff(ALICE, "Bartender", false, names), false);
    assert.equal(isStaff(ALICE, "Bartender", true, names), true);
    assert.equal(isStaff(BOB, "Bartender", true, names), false);
});

test("findItem matches a menu name ignoring case and space", () => {
    assert.equal(findItem(MENU, " negroni ").name, "Negroni");
    assert.equal(findItem(MENU, "Mojito"), null);
    assert.equal(findItem(MENU, 42), null);
    assert.equal(findItem(MENU, "Negroni" + " ".repeat(100) + "x"), null);
});

test("start publishes the open status mirror", () => {
    const { world } = bar();
    assert.deepEqual(world.status, {
        v: 1, state: "open", channel: "bar:" + BAR, open: 0, oldestAgeS: null, staffPresent: true, lastServed: null, at: T0
    });
});

test("an order is posted with its id, the orderer's session and the drink, and counted in the mirror", () => {
    const { world, core } = bar();
    assert.equal(core.order(ALICE, "negroni"), true);
    const message = world.message;
    assert.match(message.id, /^[0-9a-z]+-1$/);
    assert.deepEqual(message, { type: "order", id: message.id, by: ALICE, name: "Alice", drink: "Negroni", kind: "cocktail", glass: "rocks" });
    world.advance(42);
    core.order(BOB, "Ouzo");
    assert.equal(world.message.name, "Guest bbbbbbbb");
    assert.equal(world.status.open, 2);
    assert.equal(world.status.oldestAgeS, 42);
});

test("an order not on the menu is refused", () => {
    const { world, core } = bar();
    assert.equal(core.order(ALICE, "Mojito"), false);
    assert.deepEqual(world.message, { type: "rejected", to: ALICE, method: "order", reason: "not on the menu" });
});

test("one open order per session", () => {
    const { world, core } = bar();
    core.order(ALICE, "Ouzo");
    const id = world.message.id;
    assert.equal(core.order(ALICE.toUpperCase(), "Negroni"), false);
    assert.deepEqual(world.message, { type: "rejected", to: ALICE.toUpperCase(), method: "order", reason: "you already have an open order", id });
    core.cancel(ALICE, id);
    assert.equal(core.order(ALICE, "Negroni"), true);
});

test("maxOpenOrders caps the book", () => {
    const { world, core } = bar({ maxOpenOrders: 1 });
    core.order(ALICE, "Ouzo");
    assert.equal(core.order(BOB, "Ouzo"), false);
    assert.equal(world.message.reason, "the bar is full: 1 orders open");
});

test("only the bartender in staff serves, and the glass is spawned for the order", () => {
    const { world, core } = bar();
    core.order(ALICE, "Bordeaux");
    const id = world.message.id;
    assert.equal(core.serve(BOB, id), false);
    assert.deepEqual(world.message, { type: "rejected", to: BOB, method: "serve", reason: "only the bartender serves" });
    world.advance(5);
    assert.equal(core.serve(BARTENDER, id), true);
    assert.equal(world.spawned.length, 1);
    assert.equal(world.spawned[0].drink, "Bordeaux");
    assert.deepEqual(world.message, {
        type: "served", id, by: ALICE, name: "Alice", drink: "Bordeaux", servedBy: BARTENDER, glass: core.glasses[0].id
    });
    assert.deepEqual(world.status.lastServed, { id, drink: "Bordeaux", by: ALICE, at: T0 + 5000 });
    assert.equal(world.status.open, 0);
    assert.equal(core.serve(BARTENDER, id), false);
    assert.deepEqual(world.message, { type: "rejected", to: BARTENDER, method: "serve", reason: "no such order", id });
});

test("the staff check reads userData.staff afresh on every call", () => {
    const { world, core } = bar();
    core.order(ALICE, "Ouzo");
    const id = world.message.id;
    world.staffValue = null;
    assert.equal(core.serve(BARTENDER, id), false);
    world.staffValue = BOB;
    assert.equal(core.serve(BOB, id), true);
});

test("a staff display name serves only with staffByName", () => {
    const strict = bar();
    strict.world.staffValue = "Bartender";
    strict.core.order(ALICE, "Ouzo");
    assert.equal(strict.core.serve(BARTENDER, strict.world.message.id), false);
    const dev = bar({ staffByName: true });
    dev.world.staffValue = "Bartender";
    dev.core.order(ALICE, "Ouzo");
    assert.equal(dev.core.serve(BARTENDER, dev.world.message.id), true);
});

test("past maxGlasses the oldest glass is deleted", () => {
    const { world, core } = bar({ maxGlasses: 2, callsPerWindow: 100 });
    const ids = [];
    for (let i = 0; i < 3; i++) {
        core.order(ALICE, "Ouzo");
        core.serve(BARTENDER, world.message.id);
        ids.push(world.message.glass);
    }
    assert.deepEqual(world.deleted, [ids[0]]);
    assert.deepEqual(core.glasses.map(glass => glass.id), ids.slice(1));
});

test("a serve with no glass entity still serves, logs and posts glass null", () => {
    const { world, core } = bar();
    world.spawnFails = true;
    core.order(ALICE, "Ouzo");
    assert.equal(core.serve(BARTENDER, world.message.id), true);
    assert.equal(world.message.glass, null);
    assert.match(world.logged[world.logged.length - 1], /no glass for order/);
});

test("the orderer or the bartender cancels; nobody else", () => {
    const { world, core } = bar();
    core.order(ALICE, "Ouzo");
    const id = world.message.id;
    assert.equal(core.cancel(BOB, id), false);
    assert.deepEqual(world.message, { type: "rejected", to: BOB, method: "cancel", reason: "not your order", id });
    assert.equal(core.cancel(ALICE, id), true);
    assert.deepEqual(world.message, { type: "cancelled", id, by: ALICE, drink: "Ouzo", cancelledBy: ALICE });
    core.order(BOB, "Ouzo");
    assert.equal(core.cancel(BARTENDER, world.message.id), true);
    assert.equal(core.orders.size, 0);
});

test("orders expire after orderTtlS, on the tick or before any call", () => {
    const { world, core } = bar({ orderTtlS: 60 });
    core.order(ALICE, "Ouzo");
    const id = world.message.id;
    world.advance(59);
    core.tick();
    assert.equal(core.orders.size, 1);
    world.advance(1);
    core.tick();
    assert.deepEqual(world.message, { type: "expired", id, by: ALICE, drink: "Ouzo" });
    assert.equal(world.status.open, 0);
    core.order(BOB, "Ouzo");
    const late = world.message.id;
    world.advance(60);
    // Between ticks an expired order cannot be served
    assert.equal(core.serve(BARTENDER, late), false);
    assert.equal(world.message.reason, "no such order");
    assert.equal(world.announced.filter(m => m.type === "expired").length, 2);
});

test("the tick publishes when the bartender comes or goes, and not otherwise", () => {
    const { world, core } = bar();
    const count = world.published.length;
    core.tick();
    assert.equal(world.published.length, count);
    world.present = [ALICE];
    core.tick();
    assert.equal(world.status.staffPresent, false);
    world.staffValue = "Bartender";
    world.present = [BARTENDER];
    core.tick();
    assert.equal(world.status.staffPresent, false, "a name is not presence without staffByName");
});

test("status and menu are posted to the caller and store nothing", () => {
    const { world, core } = bar();
    core.order(ALICE, "Negroni");
    world.advance(30);
    const published = world.published.length;
    core.status(BOB);
    assert.equal(world.published.length, published);
    const message = world.message;
    assert.equal(message.type, "status");
    assert.equal(message.to, BOB);
    assert.equal(message.open, 1);
    assert.equal(message.oldestAgeS, 30);
    assert.deepEqual(message.orders, [{ id: message.orders[0].id, by: ALICE, name: "Alice", drink: "Negroni", ageS: 30 }]);
    core.menu(BOB);
    assert.deepEqual(world.message, { type: "menu", to: BOB, items: MENU });
});

test("a call without a caller id is logged and posts nothing", () => {
    const { world, core } = bar();
    const sent = world.announced.length;
    for (const caller of [undefined, NULL_UUID, "not-a-session"]) {
        assert.equal(core.order(caller, "Ouzo"), false);
        assert.equal(core.status(caller), false);
    }
    assert.equal(world.announced.length, sent);
    assert.match(world.logged[0], /no caller id/);
});

test("every method counts against the caller's window, and a refusal is posted once per window", () => {
    const { world, core } = bar({ callsPerWindow: 3, callWindowS: 30 });
    core.menu(ALICE);
    core.status(ALICE);
    core.menu(ALICE);
    const sent = world.announced.length;
    assert.equal(core.order(ALICE, "Ouzo"), false);
    assert.deepEqual(world.message, {
        type: "rejected", to: ALICE, method: "order", reason: "too many calls: at most 3 every 30 s; try again in 30 s"
    });
    assert.equal(core.menu(ALICE), false);
    assert.equal(core.serve(ALICE, "x"), false);
    assert.equal(world.announced.length, sent + 1, "only the first refusal is posted");
    // Other sessions have their own window
    assert.equal(core.order(BOB, "Ouzo"), true);
    world.advance(30);
    assert.equal(core.order(ALICE, "Ouzo"), true);
});

test("RateLimiter: a sliding window, told once, pruned", () => {
    const limiter = new RateLimiter(2, 10_000);
    assert.equal(limiter.admit(ALICE, 0), null);
    assert.equal(limiter.admit(ALICE, 4000), null);
    const refused = limiter.admit(ALICE, 5000);
    assert.equal(refused.tell, true);
    assert.match(refused.reason, /try again in 5 s/);
    assert.equal(limiter.admit(ALICE, 6000).tell, false);
    // The first call leaves the window at 10 s
    assert.equal(limiter.admit(ALICE, 10_000), null);
    assert.equal(limiter.admit(ALICE, 11_000).tell, true);
    limiter.prune(30_000);
    assert.equal(limiter.sessions.size, 0);
});

test("stop publishes offline and closes the open orders", () => {
    const { world, core } = bar();
    core.order(ALICE, "Ouzo");
    const id = world.message.id;
    core.stop();
    assert.equal(world.status.state, "offline");
    assert.equal(world.status.open, 0);
    assert.deepEqual(world.message, { type: "closed", ids: [id] });
});

test("rotate turns a vector by a quaternion", () => {
    const halfTurnY = { x: 0, y: Math.SQRT1_2, z: 0, w: Math.SQRT1_2 };
    const v = rotate(halfTurnY, { x: 1, y: 0, z: 0 });
    assert.ok(Math.abs(v.x) < 1e-9 && Math.abs(v.y) < 1e-9 && Math.abs(v.z + 1) < 1e-9, JSON.stringify(v));
});

test("glassSpot: on the counter, in front of the orderer, clamped to its ends", () => {
    const counter = { y: 0.38, z: -0.15, halfLength: 2.9 };
    const flat = { position: { x: 10, y: 0.72, z: 20 }, rotation: { x: 0, y: 0, z: 0, w: 1 } };
    assert.deepEqual(glassSpot(flat, counter, { x: 11.5, y: 1, z: 21.2 }), { x: 11.5, y: 1.1, z: 19.85 });
    assert.deepEqual(glassSpot(flat, counter, { x: 30, y: 1, z: 21 }), { x: 12.9, y: 1.1, z: 19.85 });
    assert.deepEqual(glassSpot(flat, counter, null), { x: 10, y: 1.1, z: 19.85 });
    // Turned a quarter about y: the bar's x axis runs along world -z
    const turned = { ...flat, rotation: { x: 0, y: Math.SQRT1_2, z: 0, w: Math.SQRT1_2 } };
    const spot = glassSpot(turned, counter, { x: 11, y: 1, z: 19 });
    assert.ok(Math.abs(spot.x - 9.85) < 1e-9 && Math.abs(spot.y - 1.1) < 1e-9 && Math.abs(spot.z - 19) < 1e-9, JSON.stringify(spot));
});

test("glassProperties: a Model from the glasses map, else a Text with the drink", () => {
    const order = { id: "k-1", by: ALICE, name: "Alice", drink: "Bordeaux", kind: "wine", glass: "wine", at: 0, expiresAt: 0 };
    const glasses = { wine: { url: "https://assets.example.test/wine.glb", dimensions: { x: 0.08, y: 0.2, z: 0.08 } } };
    const spot = { x: 1, y: 1.1, z: 2 };
    const model = glassProperties(order, glasses, spot, 600, BAR);
    assert.equal(model.type, "Model");
    assert.equal(model.modelURL, "https://assets.example.test/wine.glb");
    assert.deepEqual(model.position, { x: 1, y: 1.2000000000000002, z: 2 });
    assert.equal(model.lifetime, 600);
    assert.equal(model.parentID, undefined);
    assert.deepEqual(JSON.parse(model.userData), { barGlass: { bar: BAR, order: "k-1", drink: "Bordeaux", for: ALICE } });
    const text = glassProperties({ ...order, glass: "coupe" }, glasses, spot, 60, BAR);
    assert.equal(text.type, "Text");
    assert.equal(text.text, "Bordeaux");
    assert.equal(text.name, "Bar.Glass Bordeaux");
    assert.equal(text.lifetime, 60);
    assert.equal(glassProperties({ ...order, glass: "" }, glasses, spot, 60, BAR).type, "Text");
});
