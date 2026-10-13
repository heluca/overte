//
//  game-registry.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { GAMES, gameEntry, DEFAULT_GAME, CHESS_PIECES, uciCells } from "../game-registry.mjs";
import * as chess from "../chess.mjs";

const CONTRACT = ["GAME", "SEAT_NAMES", "REMATCH", "MOVE_HINT", "newGame", "sideToMove", "legalMoves", "play",
    "outcome", "rows", "render", "extra"];

test("every registered game exports the rules module contract under its own name", () => {
    for (const [name, entry] of Object.entries(GAMES)) {
        CONTRACT.forEach(key => assert.ok(key in entry.rules, name + " lacks " + key));
        assert.equal(entry.rules.GAME, name);
        assert.equal(entry.rules.SEAT_NAMES.length, 2);
        assert.ok(["alternate", "swap"].includes(entry.rules.REMATCH));
    }
});

test("the configured game is looked up; absent means Connect Four, unknown falls back with a flag", () => {
    assert.equal(DEFAULT_GAME, "connect4");
    for (const name of [undefined, null, ""]) {
        assert.deepEqual(gameEntry(name), { name: "connect4", entry: GAMES.connect4, known: true });
    }
    for (const name of ["go", "chess-960", "toString", "__proto__", 4, {}]) {
        const found = gameEntry(name);
        assert.equal(found.name, "connect4");
        assert.equal(found.known, false, String(name));
    }
    assert.equal(gameEntry("connect4").known, true);
});

test("chess is an 8x8 game with rank 8 on row 0, UCI cells and a model for every FEN letter", () => {
    const { name, entry, known } = gameEntry("chess");
    assert.equal(name, "chess");
    assert.equal(known, true);
    assert.ok(entry.layout === "grid");
    assert.equal(entry.rules, chess);
    assert.equal(entry.rowZeroSide, 1, "row 0 is rank 8, Black's (seat 1) home row");
    assert.equal(entry.cells, uciCells);
    assert.equal(entry.pieces, CHESS_PIECES);
    assert.deepEqual(Object.keys(CHESS_PIECES).sort(), [..."BKNPQRbknpqr"]);
    for (const [letter, look] of Object.entries(CHESS_PIECES)) {
        const white = letter < "a";
        assert.match(look.model, white ? /_lt\.fbx$/ : /_dk\.fbx$/, letter);
        assert.ok(existsSync(new URL("../" + look.model, import.meta.url)), look.model);
        assert.deepEqual(look.size, CHESS_PIECES[letter.toLowerCase()].size, "both colours the same size");
        assert.ok(look.size.x < 1 && look.size.x === look.size.z, letter + " fits its square");
        // White plays from the +z edge and turns to face Black; the models face their +z
        assert.deepEqual(look.rotation, white ? { x: 0, y: 1, z: 0, w: 0 } : undefined, letter);
    }
    const height = (/** @type {string} */ letter) => CHESS_PIECES[letter].size.y;
    assert.deepEqual([..."kqbnrp"].sort((a, b) => height(b) - height(a)), [..."kqbnrp"], "tallest to shortest");
    assert.ok(Math.abs(CHESS_PIECES.K.size.x - 0.0715 / (1.045 / 8)) < 1e-9, "descriptor metres over the old mat's square");
});
