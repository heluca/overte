//
//  gametable-referee.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The serverScripts entry against fake Entities, Messages and Script globals: what it writes
//  to the world, not the rules (referee-core.test.mjs covers those).
//

import { test } from "node:test";
import assert from "node:assert/strict";

const BOARD = "{b0a4d000-0000-4000-8000-000000000000}";
const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";

function fakeEngine(boardUserData = "") {
    let nextId = 1;
    const entities = new Map([[BOARD, { name: "Board", dimensions: { x: 0.7, y: 0.6, z: 0.04 }, userData: boardUserData }]]);
    const engine = {
        entities,
        sent: [],
        printed: [],
        timers: new Map(),
        Entities: {
            addEntity(properties) {
                const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
                entities.set(id, { ...properties });
                return id;
            },
            editEntity(id, properties) {
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
            }
        },
        Messages: { sendMessage(channel, message) { engine.sent.push({ channel, message: JSON.parse(message) }); } },
        Script: {
            remoteCallerID: undefined,
            setInterval(callback) { engine.timers.set(nextId, callback); return nextId++; },
            setTimeout(callback) { engine.timers.set(nextId, callback); return nextId++; },
            clearInterval(id) { engine.timers.delete(id); },
            clearTimeout(id) { engine.timers.delete(id); }
        },
        print(text) { engine.printed.push(text); },
        // As the entity script server does: the caller id is set only while the method runs
        call(referee, caller, method, params = []) {
            engine.Script.remoteCallerID = caller;
            referee[method](BOARD, params);
            engine.Script.remoteCallerID = undefined;
        },
        named(name) {
            return [...entities.values()].filter(entity => entity.name === name);
        },
        discs() {
            return [...entities.values()].filter(entity => (entity.name || "").startsWith("GT.Disc "));
        },
        projection() { return JSON.parse(entities.get(BOARD).userData).gameTable; }
    };
    Object.assign(globalThis, { Entities: engine.Entities, Messages: engine.Messages, Script: engine.Script, print: engine.print });
    return engine;
}

const { default: GameTableReferee } = await import("../gametable-referee.mjs");

// Loads a referee and lets its child search run out, so it creates the status display.
function loaded(engine) {
    const referee = new GameTableReferee();
    referee.preload(BOARD);
    engine.Script.clearTimeout(referee.searchTimer);
    referee.findChildren(0);
    return referee;
}

test("the remotely callable methods are exactly the referee API", () => {
    assert.deepEqual(new GameTableReferee().remotelyCallable, ["join", "move", "leave", "state", "newGame"]);
});

test("preload publishes into userData without losing other keys and creates one status Text", () => {
    const engine = fakeEngine(JSON.stringify({ owner: "kept", gameTableConfig: { turnTimeoutS: 30 } }));
    const referee = loaded(engine);
    const userData = JSON.parse(engine.entities.get(BOARD).userData);
    assert.equal(userData.owner, "kept");
    assert.equal(userData.gameTable.phase, "waiting");
    assert.equal(userData.gameTable.channel, "gametable:" + BOARD);
    assert.equal(referee.core.turnTimeoutMs, 30_000);
    const status = engine.named("GT.Status");
    assert.equal(status.length, 1);
    assert.equal(status[0].parentID, BOARD);
    assert.equal(status[0].font, "Inconsolata");
    assert.match(status[0].text, /Waiting for players/);
    assert.deepEqual(engine.sent[0], { channel: "gametable:" + BOARD, message: { type: "state", ...userData.gameTable } });
});

test("seats come from Script.remoteCallerID and moves drop disc models on the board", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    engine.call(referee, ALICE, "move", ["3"]);
    engine.call(referee, BOB, "move", ["3"]);
    assert.equal(engine.projection().phase, "playing");
    assert.deepEqual(engine.projection().rows.slice(4), ["...B...", "...R..."]);
    const discs = engine.discs();
    assert.deepEqual(discs.map(disc => disc.name).sort(), ["GT.Disc r4c3", "GT.Disc r5c3"]);
    const red = discs.find(disc => disc.name === "GT.Disc r5c3");
    assert.match(red.modelURL, /\/assets\/checkers\/red_checker\.fbx$/);
    assert.equal(red.parentID, BOARD);
    assert.equal(red.collisionless, true);
    assert.equal(red.localPosition.x, 0);
    assert.ok(red.localPosition.y < 0);
    assert.match(engine.named("GT.Status")[0].text, /Red to move \(move 3,/);
});

test("a call without a caller id is refused and announced on the channel", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, "{00000000-0000-0000-0000-000000000000}", "join", ["Ghost"]);
    assert.deepEqual(engine.sent[engine.sent.length - 1].message,
        { type: "rejected", to: "{00000000-0000-0000-0000-000000000000}", method: "join", reason: "no caller id" });
    assert.match(engine.printed[engine.printed.length - 1], /"rejected"/);
    assert.deepEqual(engine.projection().seats, [null, null]);
});

test("the winning line is drawn larger, and a rematch clears the discs", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    [0, 1, 0, 1, 0, 1, 0].forEach((col, i) => engine.call(referee, i % 2 ? BOB : ALICE, "move", [String(col)]));
    const discs = engine.discs();
    assert.equal(discs.length, 7);
    const winning = discs.filter(disc => disc.name.endsWith("c0"));
    const other = discs.find(disc => disc.name.endsWith("c1"));
    winning.forEach(disc => assert.ok(disc.dimensions.x > other.dimensions.x));
    engine.call(referee, BOB, "newGame");
    assert.equal(engine.discs().length, 0);
    assert.equal(engine.projection().toMove, 1);
});

test("a reload finds its status Text, deletes stale discs and reports the abandoned game", () => {
    const engine = fakeEngine();
    const first = loaded(engine);
    engine.call(first, ALICE, "join", ["Alice"]);
    engine.call(first, BOB, "join", ["Bob"]);
    engine.call(first, ALICE, "move", ["2"]);
    // The old instance dies without unload: its discs and projection are left behind
    assert.equal(engine.discs().length, 1);
    const second = new GameTableReferee();
    second.preload(BOARD);
    assert.equal(engine.named("GT.Status").length, 1);
    assert.equal(engine.discs().length, 0);
    assert.equal(engine.projection().phase, "waiting");
    assert.ok(engine.sent.some(sent => sent.message.type === "over" && sent.message.result.reason === "abandoned"));
    assert.equal(second.ready, true);
});

test("unload stops the clock, removes the discs and marks the display offline", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    engine.call(referee, ALICE, "move", ["4"]);
    assert.ok(engine.timers.has(referee.timer));
    referee.unload();
    assert.equal(engine.timers.size, 0);
    assert.equal(engine.discs().length, 0);
    assert.match(engine.named("GT.Status")[0].text, /Referee not running/);
});
