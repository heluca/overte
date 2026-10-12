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

import { BOARD, fakeEngine } from "./fake-engine.mjs";

const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";

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
    assert.deepEqual(new GameTableReferee().remotelyCallable, ["join", "move", "leave", "state", "newGame", "draw"]);
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

test("seats come from Script.remoteCallerID and moves drop discs on the board", () => {
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
    assert.equal(red.shape, "Cylinder");
    assert.ok(red.color.red > 150 && red.color.green < 80);
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

test("the winning line gets a ring behind each disc, and a rematch clears discs and rings", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    [0, 1, 0, 1, 0, 1, 0].forEach((col, i) => engine.call(referee, i % 2 ? BOB : ALICE, "move", [String(col)]));
    assert.equal(engine.discs().length, 7);
    const rings = () => [...engine.entities.values()].filter(entity => (entity.name || "").startsWith("GT.Win "));
    assert.deepEqual(rings().map(ring => ring.name).sort(), ["GT.Win r2c0", "GT.Win r3c0", "GT.Win r4c0", "GT.Win r5c0"]);
    rings().forEach(ring => assert.equal(ring.parentID, BOARD));
    assert.equal(referee.layout.pieceIDs().size, 11, "the sweep keeps the rings");
    engine.call(referee, BOB, "newGame");
    assert.equal(engine.discs().length, 0);
    assert.equal(rings().length, 0);
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
    assert.equal(engine.entities.has(referee.layout.discs.get("r5c4").id), true);
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

test("an unknown game in the config logs a warning and plays Connect Four", () => {
    const engine = fakeEngine(JSON.stringify({ gameTableConfig: { game: "go" } }));
    loaded(engine);
    assert.ok(engine.printed.some(line => /unknown game "go", playing connect4/.test(line)));
    assert.equal(engine.projection().game, "connect4");
    assert.equal(engine.named("GT.Col0").length, 1);
});

test("draw offers come from Script.remoteCallerID and agreement ends the game", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    engine.call(referee, BOB, "draw");
    assert.equal(engine.projection().drawOffer, 1);
    engine.call(referee, ALICE, "draw");
    assert.deepEqual(engine.projection().result, { winner: null, reason: "agreement", line: null });
});

const CHECKERS = JSON.stringify({ gameTableConfig: { game: "checkers" } });
const FLAT = { x: 0.6, y: 0.04, z: 0.6 };

function pieces(engine) {
    return [...engine.entities].filter(([, entity]) => (entity.name || "").startsWith("GT.Pc "));
}

test("a checkers board lays out 64 tiles, the seats, the status and 24 pieces that ignore picks", () => {
    const engine = fakeEngine(CHECKERS, false, FLAT);
    loaded(engine);
    const state = engine.projection();
    assert.equal(state.game, "checkers");
    assert.equal(state.legal.length, 0);
    assert.equal(engine.named("GT.Col0").length, 0, "no Connect Four targets");
    const tiles = [...engine.entities.values()].filter(entity => (entity.name || "").startsWith("GT.Sq "));
    assert.equal(tiles.length, 64);
    tiles.forEach(tile => {
        assert.equal(tile.parentID, BOARD);
        assert.equal(tile.script, undefined, "tiles run no script");
        assert.ok(tile.localPosition.y > FLAT.y / 2, "on the playing surface");
        assert.ok(Math.abs(tile.dimensions.x - 0.075) < 1e-9);
    });
    const [darkTile] = engine.named("GT.Sq r0c1");
    const [lightTile] = engine.named("GT.Sq r0c0");
    assert.notDeepEqual(darkTile.color, lightTile.color);
    // Black's home row (row 0, squares 1-4) is at seat 0's edge, +z
    assert.ok(darkTile.localPosition.z > 0.25);
    const seats = [engine.named("GT.Seat0")[0], engine.named("GT.Seat1")[0]];
    assert.equal(seats[0].text, "Play Black");
    assert.equal(seats[1].text, "Play Red");
    assert.ok(seats[0].localPosition.z > FLAT.z / 2 && seats[1].localPosition.z < -FLAT.z / 2);
    seats.forEach(seat => assert.match(seat.script, /\/gametable-client\.mjs$/));
    const status = engine.named("GT.Status")[0];
    assert.ok(status.localPosition.z < 0 && status.localPosition.y > 0.2, "above the far edge");
    assert.match(status.text, /Black: \(empty\) {3}Red: \(empty\)/);

    const placed = pieces(engine);
    assert.equal(placed.length, 24);
    placed.forEach(([, piece]) => {
        assert.equal(piece.ignorePickIntersection, true);
        assert.equal(piece.collisionless, true);
        assert.deepEqual(piece.grab, { grabbable: false });
    });
    const black = engine.named("GT.Pc r0c1")[0];
    assert.match(black.modelURL, /\/assets\/checkers\/black_checker\.fbx$/);
    assert.deepEqual([black.localPosition.x, black.localPosition.z], [darkTile.localPosition.x, darkTile.localPosition.z]);
    assert.match(engine.named("GT.Pc r7c0")[0].modelURL, /red_checker/);
});

test("checkers moves edit the piece entities: a step moves one, a capture deletes one, a king grows", () => {
    const engine = fakeEngine(CHECKERS, false, FLAT);
    const referee = loaded(engine);
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    const state = engine.projection();
    assert.deepEqual(state.legal, ["10-14", "10-15", "11-15", "11-16", "12-16", "9-13", "9-14"]);
    assert.equal(state.drawOffer, null);
    const moving = referee.layout.pieces.get("r2c5").id;
    engine.call(referee, ALICE, "move", ["11-15"]);
    assert.deepEqual(engine.projection().last, { seat: 0, move: "11-15" });
    assert.equal(engine.entities.get(moving).name, "GT.Pc r3c4", "the same entity moved");
    assert.equal(engine.entities.get(moving).localPosition.x, engine.named("GT.Sq r3c4")[0].localPosition.x);
    assert.equal(pieces(engine).length, 24);
    engine.call(referee, BOB, "move", ["11-15"]);
    assert.equal(engine.sent.at(-1).message.reason, "illegal move");
    engine.call(referee, BOB, "move", ["22-18 please"]);
    assert.equal(engine.sent.at(-1).message.reason, "move must be like 11-15 or 22x15x6");
    engine.call(referee, BOB, "move", ["22-18"]);
    engine.call(referee, ALICE, "move", ["15x22"]);
    assert.equal(pieces(engine).length, 23);
    assert.equal(engine.named("GT.Pc r4c3").length, 0);

    // Crowning: a man stepping onto the far row is the same entity, taller
    const crown = new GameTableReferee();
    const flat = fakeEngine(CHECKERS, false, FLAT);
    crown.preload(BOARD);
    flat.Script.clearTimeout(crown.searchTimer);
    crown.findChildren(0);
    crown.layout.draw({ board: [".b......", "........", "........", "........", "........", "........", "..b.....", "........"] });
    const man = crown.layout.pieces.get("r6c2");
    const height = flat.entities.get(man.id).dimensions.y;
    crown.layout.draw({ board: [".b......", "........", "........", "........", "........", "........", "........", "...B...."] });
    assert.equal(crown.layout.pieces.get("r7c3").id, man.id);
    assert.ok(flat.entities.get(man.id).dimensions.y > 2 * height);
});

test("switching a board's game removes the other game's children", () => {
    const engine = fakeEngine("", false, FLAT);
    loaded(engine);
    assert.equal(engine.named("GT.Col3").length, 1);
    assert.equal(engine.named("GT.Hole r5c6").length, 1);
    engine.entities.get(BOARD).userData = CHECKERS;
    loaded(engine);
    assert.equal(engine.named("GT.Col3").length, 0);
    const toy = [...engine.entities.values()].filter(entity => /^GT\.(Hole|Frame|Base)/.test(entity.name || ""));
    assert.deepEqual(toy, [], "no holes, rails or base on the 8x8 board");
    assert.equal(engine.named("GT.Seat0").length, 1);
    assert.equal(engine.named("GT.Seat0")[0].text, "Play Black", "an adopted seat button takes the game's look");
    assert.equal(engine.named("GT.Sq r4c4").length, 1);
    assert.deepEqual(engine.named("GT.Seat1")[0].localRotation, { x: 0, y: 1, z: 0, w: 0 }, "seat 1 faces its edge");
    // And back: Connect Four's seat buttons face front again
    engine.entities.get(BOARD).userData = "";
    loaded(engine);
    assert.equal(engine.named("GT.Sq r4c4").length, 0);
    assert.equal(engine.named("GT.Col3").length, 1);
    assert.equal(engine.named("GT.Seat1")[0].text, "Play Black");
    assert.deepEqual(engine.named("GT.Seat1")[0].localRotation, { x: 0, y: 0, z: 0, w: 1 });
});

test("a crowning capture on the board keeps the capturer's entity and deletes the captured one", () => {
    const engine = fakeEngine(CHECKERS, false, FLAT);
    const referee = loaded(engine);
    const layout = referee.layout;
    const before = ["........", "..b.....", "...r....", "........", "........", "........", "........", "..b....."];
    layout.draw({ board: before, move: null });
    const red = layout.pieces.get("r2c3");
    const black = layout.pieces.get("r1c2");
    const manHeight = engine.entities.get(red.id).dimensions.y;
    layout.draw({
        board: [".R......", "........", "........", "........", "........", "........", "........", "..b....."],
        move: { side: 1, notation: "10x1", from: [2, 3], to: [0, 1], path: [[2, 3], [0, 1]], captured: [[1, 2]], promoted: true }
    });
    assert.equal(layout.pieces.get("r0c1").id, red.id);
    assert.equal(engine.entities.has(black.id), false);
    assert.match(engine.entities.get(red.id).modelURL, /red_checker/);
    assert.ok(engine.entities.get(red.id).dimensions.y > 2 * manHeight);
    assert.equal(pieces(engine).length, 2);
});

test("a chess board: 32 pieces, White's facing Black; en passant, castling and promotion redraw them", () => {
    const engine = fakeEngine(JSON.stringify({ gameTableConfig: { game: "chess" } }), false, FLAT);
    const referee = loaded(engine);
    assert.equal(pieces(engine).length, 32);
    assert.equal(engine.named("GT.Seat0")[0].text, "Play White");
    const king = engine.named("GT.Pc r7c4")[0];
    assert.match(king.modelURL, /\/assets\/chess\/lowPoly\/king_lt\.fbx$/);
    assert.deepEqual(king.localRotation, { x: 0, y: 1, z: 0, w: 0 });
    assert.ok(king.localPosition.z > 0.25, "rank 1 at seat 0's edge");
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    let ply = 0;
    const play = (/** @type {string[]} */ moves) => moves.forEach(move => engine.call(referee, ply++ % 2 ? BOB : ALICE, "move", [move]));
    play(["e2e4", "d7d5", "e4e5", "f7f5", "e5f6"]);
    const state = engine.projection();
    assert.deepEqual(state.last, { seat: 0, move: "e5f6", san: "exf6" });
    assert.equal(state.fen, "rnbqkbnr/ppp1p1pp/5P2/3p4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3");
    assert.equal(state.check, false);
    assert.equal(engine.named("GT.Pc r3c5").length, 0, "the pawn taken en passant is gone");
    assert.equal(pieces(engine).length, 31);
    const rook = referee.layout.pieces.get("r7c7").id;
    play(["b8c6", "g1f3", "c6b8", "f1e2", "b8c6", "e1g1"]);
    assert.equal(engine.projection().last.san, "O-O");
    assert.equal(engine.entities.get(rook).name, "GT.Pc r7c5", "the rook moved with the king");
    play(["c6b8", "f6g7", "b8c6"]);
    const pawn = referee.layout.pieces.get("r1c6").id;
    const victim = referee.layout.pieces.get("r0c7").id;
    play(["g7h8q"]);
    assert.equal(engine.projection().last.san, "gxh8=Q");
    assert.equal(engine.entities.get(pawn).name, "GT.Pc r0c7", "the pawn's entity becomes the queen");
    assert.match(engine.entities.get(pawn).modelURL, /queen_lt\.fbx$/);
    assert.deepEqual(engine.entities.get(pawn).localRotation, { x: 0, y: 1, z: 0, w: 0 });
    assert.equal(engine.entities.has(victim), false, "the rook taken is deleted");
    assert.equal(pieces(engine).length, 29);
    // A redraw that reuses a White entity for a Black piece turns it back round
    const knight = referee.layout.pieces.get("r5c5").id;
    const board = engine.projection().rows.map((/** @type {string} */ line, /** @type {number} */ row) =>
        row === 5 ? line.replace("N", ".") : row === 4 ? "n" + line.slice(1) : line);
    referee.layout.draw({ board, move: null });
    assert.equal(engine.entities.get(knight).name, "GT.Pc r4c0");
    assert.match(engine.entities.get(knight).modelURL, /knight_dk\.fbx$/);
    assert.deepEqual(engine.entities.get(knight).localRotation, { x: 0, y: 0, z: 0, w: 1 });
});
