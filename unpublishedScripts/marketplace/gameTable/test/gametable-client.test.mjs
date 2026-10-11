//
//  gametable-client.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The client entity script against fake Entities, Messages, Window, MyAvatar and Script
//  globals: what it asks the referee for, what it announces, and what it draws locally.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const BOARD = "{b0a4d000-0000-4000-8000-000000000000}";
const CHANNEL = "gametable:" + BOARD;
const ME = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";
const CAROL = "{cccccccc-0000-4000-8000-000000000003}";

function projection(fields = {}) {
    return {
        v: 1, game: "connect4", gameId: null, phase: "waiting", seats: [null, null], toMove: null, moveNo: 0,
        rows: Array(6).fill("......."), legal: [], last: null, result: null, turnDeadline: null,
        channel: CHANNEL, ...fields
    };
}

const SEATED = [{ session: ME, name: "Me" }, { session: BOB, name: "Bob" }];

function fakeEngine(gameTable = projection()) {
    let nextId = 1;
    const entities = new Map([[BOARD, {
        name: "Game Table: Connect Four",
        userData: JSON.stringify(gameTable ? { owner: "kept", gameTable } : { owner: "kept" })
    }]]);
    const engine = {
        entities,
        calls: [],
        announced: [],
        subscribed: new Set(),
        handlers: [],
        domainEdits: 0,
        Entities: {
            addEntity(properties, hostType) {
                const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
                entities.set(id, { ...properties, hostType: hostType || "domain" });
                return id;
            },
            editEntity(id, properties) {
                if (entities.get(id).hostType !== "local") {
                    engine.domainEdits++;
                }
                Object.assign(entities.get(id), properties);
                return id;
            },
            deleteEntity(id) { entities.delete(id); },
            getEntityProperties(id, names) {
                const entity = entities.get(id) || {};
                return Object.fromEntries(names.map(name => [name, entity[name]]));
            },
            getChildrenIDs(parentID) {
                return [...entities].filter(([, entity]) => entity.parentID === parentID).map(([id]) => id);
            },
            callEntityServerMethod(entityID, method, params) {
                engine.calls.push({ entityID, method, params });
            }
        },
        Messages: {
            subscribe(channel) { engine.subscribed.add(channel); },
            unsubscribe(channel) { engine.subscribed.delete(channel); },
            messageReceived: {
                connect(handler) { engine.handlers.push(handler); },
                disconnect(handler) { engine.handlers = engine.handlers.filter(h => h !== handler); }
            }
        },
        Window: { displayAnnouncement(message) { engine.announced.push(message); } },
        MyAvatar: { sessionUUID: ME, sessionDisplayName: "Me", displayName: "me-account" },
        Script: {},
        // What the referee's child sweep leaves on the board
        child(name) {
            return engine.Entities.addEntity({ name, parentID: BOARD, dimensions: { x: 0.08, y: 0.05, z: 0.04 } });
        },
        // A message as the messages mixer delivers it to every handler in the engine
        deliver(channel, message) {
            const text = typeof message === "string" ? message : JSON.stringify(message);
            engine.handlers.forEach(handler => handler(channel, text, BOB, false));
        },
        highlights() {
            return [...entities.values()].filter(entity => (entity.name || "").startsWith("GT.Highlight "));
        }
    };
    Object.assign(globalThis, {
        Entities: engine.Entities, Messages: engine.Messages, Window: engine.Window,
        MyAvatar: engine.MyAvatar, Script: engine.Script
    });
    return engine;
}

const { default: GameTableClient } = await import("../gametable-client.mjs");

function load(id) {
    const client = new GameTableClient();
    client.preload(id);
    return client;
}

function columns(engine) {
    return Array.from({ length: 7 }, (_, col) => engine.child("GT.Col" + col));
}

const PRIMARY = { isPrimaryButton: true };

test("a column target asks the board's referee to move into its column, as a string", () => {
    const engine = fakeEngine();
    const targets = columns(engine);
    const third = load(targets[3]);
    third.clickDownOnEntity(targets[3], PRIMARY);
    load(targets[0]).startFarTrigger();
    load(targets[6]).startNearTrigger();
    assert.deepEqual(engine.calls, [
        { entityID: BOARD, method: "move", params: ["3"] },
        { entityID: BOARD, method: "move", params: ["0"] },
        { entityID: BOARD, method: "move", params: ["6"] }
    ]);
    third.clickDownOnEntity(targets[3], { isPrimaryButton: false });
    assert.equal(engine.calls.length, 3);
    // Children do not listen: the board's own instance does that once
    assert.equal(engine.subscribed.size, 0);
});

test("a seat button joins with the display name, and leaves when the user holds that seat", () => {
    const engine = fakeEngine(projection({ seats: [null, { session: BOB, name: "Bob" }] }));
    const red = engine.child("GT.Seat0");
    const black = engine.child("GT.Seat1");
    load(red).clickDownOnEntity(red, PRIMARY);
    load(black).clickDownOnEntity(black, PRIMARY);
    assert.deepEqual(engine.calls, [
        { entityID: BOARD, method: "join", params: ["Me"] },
        { entityID: BOARD, method: "join", params: ["Me"] }
    ]);
    engine.entities.get(BOARD).userData = JSON.stringify({ gameTable: projection({ seats: SEATED }) });
    engine.MyAvatar.sessionDisplayName = "";
    load(red).startFarTrigger();
    load(black).startFarTrigger();
    assert.deepEqual(engine.calls.slice(2), [
        { entityID: BOARD, method: "leave", params: [] },
        { entityID: BOARD, method: "join", params: ["me-account"] }
    ]);
});

test("the board subscribes to the channel named in userData and shows the state it finds", () => {
    const engine = fakeEngine(projection({ phase: "playing", seats: SEATED, toMove: 0, legal: [0, 1, 2, 3, 4, 5, 6] }));
    columns(engine);
    load(BOARD);
    assert.deepEqual([...engine.subscribed], [CHANNEL]);
    assert.deepEqual(engine.announced, ["Your turn (Red)"]);
    assert.equal(engine.highlights().length, 7);
    // userData had the state, so the referee is not asked for it
    assert.equal(engine.calls.length, 0);
});

test("a state message announces the turn and highlights the legal columns on local entities", () => {
    const engine = fakeEngine();
    const targets = columns(engine);
    load(BOARD);
    assert.deepEqual(engine.announced, []);
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "waiting", seats: [SEATED[0], null] }) });
    assert.deepEqual(engine.announced, ["Waiting for an opponent"]);
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0, legal: [0, 2, 5] }) });
    assert.equal(engine.announced.at(-1), "Your turn (Red)");
    const shown = engine.highlights();
    assert.deepEqual(shown.map(h => h.name).sort(), ["GT.Highlight 0", "GT.Highlight 2", "GT.Highlight 5"]);
    shown.forEach(h => {
        assert.equal(h.hostType, "local");
        assert.equal(h.ignorePickIntersection, true);
        assert.equal(h.parentID, targets[Number(h.name.slice(-1))]);
        assert.ok(h.dimensions.x > 0.08);
    });
    // The same state again (after a `state` call or a repeated join) is not announced twice
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0, legal: [0, 2, 5] }) });
    assert.equal(engine.announced.filter(text => text === "Your turn (Red)").length, 1);
    assert.equal(engine.highlights().length, 3);

    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 1, legal: [0, 2, 5] }) });
    assert.equal(engine.announced.at(-1), "Waiting for Bob to move");
    assert.equal(engine.highlights().length, 0);

    engine.deliver(CHANNEL, { type: "state", ...projection({
        phase: "over", seats: SEATED, result: { winner: 1, reason: "four", line: [[5, 0], [5, 1], [5, 2], [5, 3]] }
    }) });
    assert.equal(engine.announced.at(-1), "You lose (four in a row)");
    // The client never edits a domain entity, so it never writes the board's userData
    assert.equal(engine.domainEdits, 0);
});

test("refusals addressed to this user are announced, others' are not", () => {
    const engine = fakeEngine();
    load(BOARD);
    engine.deliver(CHANNEL, { type: "rejected", to: ME.toUpperCase(), method: "move", reason: "not your turn" });
    engine.deliver(CHANNEL, { type: "rejected", to: BOB, method: "move", reason: "column full" });
    engine.deliver(CHANNEL, { type: "rejected", to: ME, method: "join", reason: "table full" });
    assert.deepEqual(engine.announced, ["Move refused: not your turn", "Seat refused: table full"]);
});

test("spectators are not told about every move; other channels and bad JSON are ignored", () => {
    const engine = fakeEngine();
    columns(engine);
    load(BOARD);
    const others = [{ session: CAROL, name: "Carol" }, { session: BOB, name: "Bob" }];
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: others, toMove: 0, legal: [3] }) });
    engine.deliver(CHANNEL, "{not json");
    engine.deliver("gametable:{another-board}", { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0 }) });
    engine.deliver("gametable:{another-board}", { type: "rejected", to: ME, method: "move", reason: "not your turn" });
    assert.deepEqual(engine.announced, []);
    assert.equal(engine.highlights().length, 0);
});

test("losing the seat is announced once, and so is the referee going offline", () => {
    const engine = fakeEngine();
    load(BOARD);
    engine.deliver(CHANNEL, { type: "state", ...projection({ seats: [SEATED[0], null] }) });
    engine.deliver(CHANNEL, { type: "state", ...projection() });
    engine.deliver(CHANNEL, { type: "state", ...projection() });
    assert.deepEqual(engine.announced, ["Waiting for an opponent", "You are not seated at the game table"]);
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 1 }) });
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "offline" }) });
    assert.equal(engine.announced.at(-1), "Game table: the referee is not running");
});

test("a board without the state in userData listens on its own channel and asks for the state", () => {
    const engine = fakeEngine(null);
    load(BOARD);
    assert.deepEqual([...engine.subscribed], [CHANNEL]);
    assert.deepEqual(engine.calls, [{ entityID: BOARD, method: "state", params: [] }]);
});

test("unload unsubscribes, disconnects and removes the highlights", () => {
    const engine = fakeEngine();
    columns(engine);
    const board = load(BOARD);
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0, legal: [1, 4] }) });
    assert.equal(engine.highlights().length, 2);
    board.unload();
    assert.equal(engine.subscribed.size, 0);
    assert.equal(engine.handlers.length, 0);
    assert.equal(engine.highlights().length, 0);
    // A column target's unload has nothing to undo
    load(engine.child("GT.Col2")).unload();
    assert.equal(engine.handlers.length, 0);
});
