//
//  bar-entity.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The serverScripts entry against fake Entities, Messages, AvatarList and Script globals: what
//  it writes to the world, not the rules (bar-core.test.mjs covers those).
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { BAR, fakeEngine } from "./fake-engine.mjs";

const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";
const BARTENDER = "{cccccccc-0000-4000-8000-000000000003}";

const { default: BarEntity } = await import("../bar-entity.mjs");

const DELOS = {
    menu: [
        { name: "Bordeaux", kind: "wine", glass: "wine" },
        { name: "Ouzo", kind: "spirit", glass: "highball" }
    ],
    channel: "delos:bar",
    counter: { y: 0.38, z: -0.15, halfLength: 2.9 },
    staff: BARTENDER,
    owner: "kept"
};

function loaded(userData = DELOS, options) {
    const engine = fakeEngine(JSON.stringify(userData), options);
    engine.avatars.set(ALICE, { position: { x: 56.1, y: 1.2, z: 10.47 }, displayName: "alice", sessionDisplayName: "Alice" });
    engine.avatars.set(BARTENDER, { position: { x: 57.6, y: 0.9, z: 9.1 }, displayName: "Bartender", sessionDisplayName: "Bartender" });
    const bar = new BarEntity();
    bar.preload(BAR);
    return { engine, bar };
}

test("the remotely callable methods are exactly the bar API", () => {
    assert.deepEqual(new BarEntity().remotelyCallable, ["order", "serve", "cancel", "status", "menu"]);
});

test("preload mirrors the status into userData without losing other keys", () => {
    const { engine } = loaded();
    const userData = engine.userData();
    assert.equal(userData.owner, "kept");
    assert.equal(userData.staff, BARTENDER);
    assert.deepEqual(userData.menu, DELOS.menu);
    assert.equal(userData.status.state, "open");
    assert.equal(userData.status.channel, "delos:bar");
    assert.equal(userData.status.staffPresent, true);
    assert.equal(engine.timers.size, 1);
});

test("configuration problems are printed under the bar's id", () => {
    const { engine } = loaded({ menu: "Ouzo" });
    assert.match(engine.printed[0], new RegExp("^Bar \\" + BAR.slice(0, -1) + "\\}: menu must be an array"));
});

test("an order comes from Script.remoteCallerID and is posted on the channel", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "order", ["ouzo"]);
    const { channel, message } = engine.last;
    assert.equal(channel, "delos:bar");
    assert.deepEqual(message, { type: "order", id: message.id, by: ALICE, name: "Alice", drink: "Ouzo", kind: "spirit", glass: "highball" });
    assert.equal(engine.userData().status.open, 1);
});

test("the bartender serves: a Text glass on the counter in front of the orderer", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "order", ["Ouzo"]);
    const id = engine.last.message.id;
    engine.call(bar, BARTENDER, "serve", [id]);
    const glasses = engine.glasses();
    assert.equal(glasses.length, 1);
    const glass = glasses[0];
    assert.equal(glass.type, "Text");
    assert.equal(glass.text, "Ouzo");
    assert.equal(glass.parentID, undefined);
    assert.equal(glass.lifetime, 600);
    assert.ok(Math.abs(glass.position.x - 56.1) < 1e-9, "in front of Alice along the counter");
    assert.ok(Math.abs(glass.position.y - 1.17) < 1e-9, "on the counter top, 1.10 m");
    assert.ok(Math.abs(glass.position.z - 9.85) < 1e-9, "0.15 m in from the customer edge");
    assert.equal(engine.last.message.type, "served");
    assert.equal(engine.last.message.servedBy, BARTENDER);
    assert.equal(engine.userData().status.lastServed.id, id);
});

test("a glass in the glasses map is a Model with its URL and size", () => {
    const { engine, bar } = loaded({
        ...DELOS, glassLifetimeS: 120,
        glasses: { wine: { url: "https://assets.example.test/glass/wine.glb", dimensions: { x: 0.08, y: 0.2, z: 0.08 } } }
    });
    engine.call(bar, ALICE, "order", ["Bordeaux"]);
    engine.call(bar, BARTENDER, "serve", [engine.last.message.id]);
    const [glass] = engine.glasses();
    assert.equal(glass.type, "Model");
    assert.equal(glass.modelURL, "https://assets.example.test/glass/wine.glb");
    assert.equal(glass.lifetime, 120);
    assert.ok(Math.abs(glass.position.y - 1.2) < 1e-9);
});

test("an orderer who has left gets the glass mid-counter", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "order", ["Ouzo"]);
    engine.avatars.delete(ALICE);
    engine.call(bar, BARTENDER, "serve", [engine.last.message.id]);
    assert.ok(Math.abs(engine.glasses()[0].position.x - 57.6) < 1e-9);
});

test("serve from anyone but userData.staff is refused on the channel, and makes nothing", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "order", ["Ouzo"]);
    engine.call(bar, BOB, "serve", [engine.last.message.id]);
    assert.deepEqual(engine.last.message, { type: "rejected", to: BOB, method: "serve", reason: "only the bartender serves" });
    assert.equal(engine.glasses().length, 0);
});

test("Morpheus moving staff to another session takes effect at once", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "order", ["Ouzo"]);
    const id = engine.last.message.id;
    engine.Entities.editEntity(BAR, { userData: JSON.stringify({ ...engine.userData(), staff: BOB }) });
    engine.call(bar, BARTENDER, "serve", [id]);
    assert.equal(engine.last.message.type, "rejected");
    engine.call(bar, BOB, "serve", [id]);
    assert.equal(engine.last.message.type, "served");
    assert.equal(engine.userData().staff, BOB, "the status write kept the new staff");
});

test("status and menu answer on the channel", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "menu");
    assert.deepEqual(engine.last.message, { type: "menu", to: ALICE, items: DELOS.menu });
    engine.call(bar, BOB, "status");
    assert.equal(engine.last.message.type, "status");
    assert.equal(engine.last.message.to, BOB);
    assert.deepEqual(engine.last.message.orders, []);
});

test("cancel by the orderer", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "order", ["Ouzo"]);
    const id = engine.last.message.id;
    engine.call(bar, ALICE, "cancel", [id]);
    assert.equal(engine.last.message.type, "cancelled");
    assert.equal(engine.userData().status.open, 0);
});

test("a locked bar takes orders but writes no userData", () => {
    const { engine, bar } = loaded(DELOS, { locked: true });
    engine.call(bar, ALICE, "order", ["Ouzo"]);
    assert.equal(engine.last.message.type, "order");
    assert.equal(engine.userData().status, undefined);
    assert.match(engine.printed[0], /locked/);
});

test("unload stops the tick and publishes offline", () => {
    const { engine, bar } = loaded();
    engine.call(bar, ALICE, "order", ["Ouzo"]);
    bar.unload();
    assert.equal(engine.timers.size, 0);
    assert.equal(engine.userData().status.state, "offline");
    assert.equal(engine.last.message.type, "closed");
});
