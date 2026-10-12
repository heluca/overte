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
        rows: Array(6).fill("......."), legal: [], last: null, result: null, drawOffer: null, turnDeadline: null,
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
        timers: new Map(),
        Entities: {
            addEntity(properties, hostType) {
                const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
                entities.set(id, { ...properties, hostType: hostType || "domain" });
                return id;
            },
            editEntity(id, properties) {
                // The client must never write the board or any other domain entity
                assert.equal(entities.get(id)?.hostType, "local", "editEntity on a domain entity " + id);
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
        Script: {
            setTimeout(callback) { engine.timers.set(nextId, callback); return nextId++; },
            clearTimeout(id) { engine.timers.delete(id); }
        },
        // What the referee's child sweep leaves on the board
        child(name) {
            return engine.Entities.addEntity({ name, parentID: BOARD, dimensions: { x: 0.08, y: 0.05, z: 0.04 } });
        },
        // A message as the messages mixer delivers it to every handler in the engine
        deliver(channel, message) {
            const text = typeof message === "string" ? message : JSON.stringify(message);
            engine.handlers.forEach(handler => handler(channel, text, BOB, false));
        },
        runTimers() {
            const due = [...engine.timers];
            engine.timers.clear();
            due.forEach(([, callback]) => callback());
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
        { entityID: BOARD, method: "join", params: ["Me", "0"] },
        { entityID: BOARD, method: "join", params: ["Me", "1"] }
    ]);
    engine.entities.get(BOARD).userData = JSON.stringify({ gameTable: projection({ phase: "over", seats: SEATED }) });
    engine.MyAvatar.sessionDisplayName = "";
    load(red).startFarTrigger();
    load(black).startFarTrigger();
    assert.deepEqual(engine.calls.slice(2), [
        { entityID: BOARD, method: "leave", params: [] },
        { entityID: BOARD, method: "join", params: ["me-account", "1"] }
    ]);
});

test("during a game the seat button resigns only on a second click within 5 s", t => {
    const engine = fakeEngine(projection({ phase: "playing", seats: SEATED, toMove: 1 }));
    let now = 1_760_000_000_000;
    t.mock.method(Date, "now", () => now);
    const red = engine.child("GT.Seat0");
    const seat = load(red);
    seat.clickDownOnEntity(red, PRIMARY);
    assert.deepEqual(engine.calls, []);
    assert.deepEqual(engine.announced, ["Click your seat again within 5 s to resign the game"]);
    now += 6000;
    seat.clickDownOnEntity(red, PRIMARY);
    assert.deepEqual(engine.calls, [], "too late: asks again");
    assert.equal(engine.announced.length, 2);
    now += 4000;
    seat.clickDownOnEntity(red, PRIMARY);
    assert.deepEqual(engine.calls, [{ entityID: BOARD, method: "leave", params: [] }]);
    // The confirmation is spent: the next click asks again
    seat.clickDownOnEntity(red, PRIMARY);
    assert.equal(engine.calls.length, 1);
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

test("a stale state in userData is shown, and the referee asked once for a fresh one", () => {
    const past = Date.now() - 1000;
    const future = Date.now() + 60_000;
    [
        [projection({ phase: "offline" }), 1],
        [projection({ phase: "playing", seats: SEATED, toMove: 1, turnDeadline: past }), 1],
        [projection({ phase: "playing", seats: SEATED, toMove: 1, turnDeadline: future }), 0],
        [projection(), 0]
    ].forEach(([gameTable, asks]) => {
        const engine = fakeEngine(gameTable);
        load(BOARD);
        assert.equal(engine.calls.filter(call => call.method === "state").length, asks, gameTable.phase);
    });
});

test("column targets that arrive after the state get their highlights on a retry", () => {
    const engine = fakeEngine();
    const board = load(BOARD);
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0, legal: [1, 4] }) });
    assert.equal(engine.highlights().length, 0);
    assert.equal(engine.timers.size, 1);
    engine.runTimers();
    assert.equal(engine.timers.size, 1, "still looking");
    columns(engine);
    engine.runTimers();
    assert.deepEqual(engine.highlights().map(h => h.name).sort(), ["GT.Highlight 1", "GT.Highlight 4"]);
    assert.equal(engine.timers.size, 0, "all found: no more retries");
    board.unload();
});

test("a highlight whose target was deleted as a duplicate moves to the surviving target", () => {
    const engine = fakeEngine();
    const duplicate = engine.child("GT.Col2");
    load(BOARD);
    const state = { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0, legal: [2] }) };
    engine.deliver(CHANNEL, state);
    assert.equal(engine.highlights()[0].parentID, duplicate);
    const survivor = engine.child("GT.Col2");
    engine.Entities.deleteEntity(duplicate);
    engine.deliver(CHANNEL, state);
    const shown = engine.highlights();
    assert.equal(shown.length, 1);
    assert.equal(shown[0].parentID, survivor);
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
    assert.equal(engine.timers.size, 0);
    // A column target's unload has nothing to undo
    load(engine.child("GT.Col2")).unload();
    assert.equal(engine.handlers.length, 0);
});

test("a draw offer is announced to the players, and a draw by agreement says so", () => {
    const engine = fakeEngine();
    load(BOARD);
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0, drawOffer: 1 }) });
    assert.equal(engine.announced.at(-1), "Your turn (Red); Black offers a draw");
    engine.deliver(CHANNEL, { type: "state", ...projection({ phase: "playing", seats: SEATED, toMove: 0, drawOffer: 0 }) });
    assert.equal(engine.announced.at(-1), "Your turn (Red); your draw offer stands");
    engine.deliver(CHANNEL, { type: "state", ...projection({
        phase: "over", seats: SEATED, result: { winner: null, reason: "agreement", line: null }
    }) });
    assert.equal(engine.announced.at(-1), "Draw (by agreement)");
    engine.deliver(CHANNEL, { type: "rejected", to: ME, method: "draw", reason: "game over" });
    assert.equal(engine.announced.at(-1), "Draw offer refused: game over");
});
