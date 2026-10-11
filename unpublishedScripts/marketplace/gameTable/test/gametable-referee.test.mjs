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

function fakeEngine(boardUserData = "", locked = false) {
    let nextId = 1;
    const entities = new Map([[BOARD, {
        name: "Board", dimensions: { x: 0.7, y: 0.6, z: 0.04 }, userData: boardUserData, locked
    }]]);
    const engine = {
        entities,
        sent: [],
        printed: [],
        boardEdits: 0,
        timers: new Map(),
        Entities: {
            addEntity(properties) {
                const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
                entities.set(id, { ...properties });
                return id;
            },
            editEntity(id, properties) {
                if (id === BOARD) {
                    engine.boardEdits++;
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
        projection() { return JSON.parse(entities.get(BOARD).userData).gameTable; },
        // A child the entity server delivers after preload has started
        arrive(name) {
            return engine.Entities.addEntity({ name, parentID: BOARD });
        },
        runTimer(id) {
            const callback = engine.timers.get(id);
            engine.timers.delete(id);
            callback();
        }
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

test("join passes the seat parameter through to the core", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice", "1"]);
    engine.call(referee, BOB, "join", ["Bob", "1"]);
    assert.deepEqual(engine.projection().seats, [null, { session: ALICE, name: "Alice" }]);
    assert.deepEqual(engine.sent[engine.sent.length - 1].message,
        { type: "rejected", to: BOB, method: "join", reason: "seat taken" });
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
    assert.match(engine.named("GT.Status")[0].text, /Waiting for players/);
});

test("the sweep runs for the whole search window and deletes discs that arrive late", () => {
    const engine = fakeEngine();
    const referee = new GameTableReferee();
    referee.preload(BOARD);
    assert.ok(engine.timers.has(referee.searchTimer), "still sweeping");
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    engine.call(referee, ALICE, "move", ["4"]);
    const status = engine.arrive("GT.Status");
    engine.runTimer(referee.searchTimer);
    assert.equal(referee.statusID, status);
    assert.match(engine.named("GT.Status")[0].text, /Black to move/);
    // Found its status display, but keeps sweeping: stale discs and a duplicate display arrive later
    const stale = [engine.arrive("GT.Disc r5c0"), engine.arrive("GT.Disc r5c4")];
    const duplicate = engine.arrive("GT.Status");
    assert.ok(engine.timers.has(referee.searchTimer));
    engine.runTimer(referee.searchTimer);
    [...stale, duplicate].forEach(id => assert.equal(engine.entities.has(id), false));
    assert.deepEqual(engine.discs().map(disc => disc.name), ["GT.Disc r5c4"]);
    assert.equal(engine.entities.has(referee.discs.get("r5c4").id), true);
    // After the window it stops, and creates no second display
    engine.Script.clearTimeout(referee.searchTimer);
    referee.findChildren(0);
    assert.equal(engine.timers.has(referee.searchTimer), false);
    assert.equal(engine.named("GT.Status").length, 1);
});

test("state and a repeated join go to the channel without editing the board", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    const edits = engine.boardEdits;
    const sent = engine.sent.length;
    for (let i = 0; i < 5; i++) {
        engine.call(referee, BOB, "state");
        engine.call(referee, ALICE, "join", ["Alice"]);
    }
    assert.equal(engine.boardEdits, edits);
    assert.equal(engine.sent.length, sent + 10);
    assert.ok(engine.sent.slice(sent).every(entry => entry.message.type === "state"));
    assert.equal(engine.printed.filter(line => line.includes('"state"')).length, 0);
});

test("leaving mid-game edits the board once", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    const edits = engine.boardEdits;
    engine.call(referee, BOB, "leave");
    assert.equal(engine.boardEdits, edits + 1);
    assert.equal(engine.projection().phase, "waiting");
});

test("a locked board warns in the log and on the display, and is not edited", () => {
    const engine = fakeEngine(JSON.stringify({ owner: "kept" }), true);
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    assert.equal(engine.boardEdits, 0);
    assert.deepEqual(JSON.parse(engine.entities.get(BOARD).userData), { owner: "kept" });
    assert.ok(engine.printed.some(line => /BOARD LOCKED/.test(line)));
    assert.match(engine.named("GT.Status")[0].text, /^BOARD LOCKED[^\n]*\nRed: Alice/);
    assert.equal(engine.sent[engine.sent.length - 1].message.type, "state");
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
    const state = engine.projection();
    assert.equal(state.phase, "offline");
    assert.equal(state.toMove, null);
    assert.equal(state.turnDeadline, null);
    assert.ok(engine.sent.some(sent => sent.message.type === "over" && sent.message.result.reason === "abandoned"));

    // The next load finds it offline, not mid-game, and reports nothing more
    const abandoned = engine.sent.filter(sent => sent.message.type === "over").length;
    loaded(engine);
    assert.equal(engine.sent.filter(sent => sent.message.type === "over").length, abandoned);
});

test("the referee creates seven column targets and two seat buttons that run the client script", () => {
    const engine = fakeEngine();
    loaded(engine);
    const targets = Array.from({ length: 7 }, (_, col) => engine.named("GT.Col" + col));
    const seats = [engine.named("GT.Seat0"), engine.named("GT.Seat1")];
    [...targets, ...seats].forEach(found => {
        assert.equal(found.length, 1);
        const [child] = found;
        assert.equal(child.parentID, BOARD);
        assert.match(child.script, /\/gametable-client\.mjs$/);
        assert.equal(child.collisionless, true);
        assert.deepEqual(child.grab, { grabbable: false, triggerable: true });
    });
    // Left to right over the board, in a strip above it and under the status display
    const xs = targets.map(([child]) => child.localPosition.x);
    assert.deepEqual(xs, xs.slice().sort((a, b) => a - b));
    assert.ok(Math.abs(xs[3]) < 1e-9);
    const top = targets[0][0].localPosition.y + targets[0][0].dimensions.y / 2;
    const status = engine.named("GT.Status")[0];
    assert.ok(targets[0][0].localPosition.y - targets[0][0].dimensions.y / 2 > 0.3);
    assert.ok(status.localPosition.y - status.dimensions.y / 2 > top);
    // Seats beside the board: Red to the left, Black to the right
    assert.ok(seats[0][0].localPosition.x < -0.35 && seats[1][0].localPosition.x > 0.35);
    assert.equal(seats[0][0].type, "Text");
    assert.match(seats[0][0].text, /Red/);
    assert.match(seats[1][0].text, /Black/);
});

test("a reload adopts the column targets and seats, and deletes duplicates that arrive late", () => {
    const engine = fakeEngine();
    loaded(engine);
    const before = engine.entities.size;
    const kept = engine.named("GT.Col3")[0];
    const second = new GameTableReferee();
    second.preload(BOARD);
    const duplicates = [engine.arrive("GT.Col3"), engine.arrive("GT.Seat1")];
    engine.runTimer(second.searchTimer);
    duplicates.forEach(id => assert.equal(engine.entities.has(id), false));
    engine.Script.clearTimeout(second.searchTimer);
    second.findChildren(0);
    assert.equal(engine.entities.size, before);
    assert.equal(engine.named("GT.Col3")[0], kept);
    assert.equal(engine.named("GT.Seat1").length, 1);
});

test("adopted children are brought back to the current layout and client script", () => {
    const engine = fakeEngine();
    const stale = { script: "https://old-host/gameTable/gametable-client.mjs", localPosition: { x: 9, y: 9, z: 9 },
        dimensions: { x: 1, y: 1, z: 1 } };
    const column = engine.Entities.addEntity({ name: "GT.Col2", parentID: BOARD, type: "Box", ...stale });
    const seat = engine.Entities.addEntity({ name: "GT.Seat1", parentID: BOARD, type: "Text", ...stale });
    const status = engine.Entities.addEntity({ name: "GT.Status", parentID: BOARD, type: "Text",
        localPosition: stale.localPosition, dimensions: stale.dimensions });
    loaded(engine);
    const fresh = engine.named("GT.Col1")[0];
    const adoptedColumn = engine.entities.get(column);
    assert.equal(adoptedColumn.script, fresh.script);
    assert.match(adoptedColumn.script, /^file:.*\/gametable-client\.mjs$/);
    assert.deepEqual(adoptedColumn.dimensions, fresh.dimensions);
    assert.equal(adoptedColumn.localPosition.y, fresh.localPosition.y);
    assert.ok(adoptedColumn.localPosition.x > fresh.localPosition.x);
    assert.equal(engine.entities.get(seat).script, fresh.script);
    assert.ok(engine.entities.get(seat).localPosition.x > 0.35);
    assert.ok(engine.entities.get(status).localPosition.y > fresh.localPosition.y);
    assert.equal(engine.entities.get(status).script, undefined);
});
