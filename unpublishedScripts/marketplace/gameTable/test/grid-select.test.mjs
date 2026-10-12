//
//  grid-select.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { MovePicker } from "../grid-select.mjs";
import * as checkers from "../checkers.mjs";
import { uciCells } from "../game-registry.mjs";

const { squareCell, moveCells, legalMoves, fromPosition, newGame } = checkers;

function picker(moves, cells = moveCells) {
    const picking = new MovePicker();
    picking.setChoices(moves.map(notation => ({ notation, cells: cells(notation) })));
    return picking;
}

/** Clicks the numbered squares in turn; returns what the last click sent. */
function clicks(picking, ...squares) {
    let sent = null;
    squares.forEach(square => {
        sent = picking.click(squareCell(square));
    });
    return sent;
}

test("click a piece, then a destination, to play the move", () => {
    const picking = picker(legalMoves(newGame()));
    assert.deepEqual(picking.view(), { selected: [], targets: [] });
    assert.equal(clicks(picking, 11), null);
    assert.deepEqual(picking.view(), { selected: [squareCell(11)], targets: [squareCell(15), squareCell(16)] });
    assert.equal(clicks(picking, 16), "11-16");
    assert.deepEqual(picking.view(), { selected: [], targets: [] }, "sending clears the selection");
});

test("another of your pieces reselects; anything else clears", () => {
    const picking = picker(legalMoves(newGame()));
    clicks(picking, 11, 9);
    assert.deepEqual(picking.view().selected, [squareCell(9)]);
    clicks(picking, 20);
    assert.deepEqual(picking.view(), { selected: [], targets: [] });
    // A piece with no move (behind the front row) cannot be selected
    clicks(picking, 1);
    assert.deepEqual(picking.view(), { selected: [], targets: [] });
    assert.equal(clicks(picking, 15), null, "a destination with nothing selected does nothing");
});

test("a multi-jump is played by its landing square", () => {
    const picking = picker(legalMoves(fromPosition([".b......", "..r.....", "........", "....r...",
        "........", "........", "........", "........"])));
    assert.equal(clicks(picking, 1), null);
    assert.deepEqual(picking.view().targets, [squareCell(19)]);
    assert.equal(clicks(picking, 19), "1x10x19");
});

test("two paths to the same square: the next square on the way is asked for", () => {
    // Black's man on 2 can reach 18 round either side
    const moves = legalMoves(fromPosition(["...b....", "..r.r...", "........", "..r.r...",
        "........", "........", "........", "........"]));
    assert.deepEqual(moves, ["2x11x18", "2x9x18"]);
    const picking = picker(moves);
    assert.equal(clicks(picking, 2, 18), null);
    assert.deepEqual(picking.view(), { selected: [squareCell(2), squareCell(18)], targets: [squareCell(11), squareCell(9)] });
    assert.equal(clicks(picking, 9), "2x9x18");

    // A click off the way starts over
    clicks(picking, 2, 18, 20);
    assert.deepEqual(picking.view(), { selected: [], targets: [] });
    assert.equal(clicks(picking, 2, 18, 2), null, "the piece itself reselects it");
    assert.deepEqual(picking.view().selected, [squareCell(2)]);
});

test("a king's round trip back to its own square is asked for the same way", () => {
    const moves = legalMoves(fromPosition(["........", "........", "...B....", "..r.r...", "........",
        "..r.r...", "........", "........"]));
    assert.deepEqual(moves, ["10x17x26x19x10", "10x19x26x17x10"]);
    const picking = picker(moves);
    assert.equal(clicks(picking, 10, 10), null);
    assert.deepEqual(picking.view().targets, [squareCell(17), squareCell(19)]);
    assert.equal(clicks(picking, 19), "10x19x26x17x10");
});

test("chess cells: UCI squares with rank 8 on row 0; a promotion by click is a queen", () => {
    assert.deepEqual(uciCells("e2e4"), [[6, 4], [4, 4]]);
    assert.deepEqual(uciCells("a8h1"), [[0, 0], [7, 7]]);
    assert.deepEqual(uciCells("e7e8q"), [[1, 4], [0, 4]]);
    for (const text of ["E2E4", "e2e9", "e7e8k", "e2-e4", ""]) {
        assert.equal(uciCells(text), null, text);
    }
    const picking = picker(["e7e8b", "e7e8n", "e7e8q", "e7e8r", "e7d8q", "e7d8r"], uciCells);
    picking.click([1, 4]);
    assert.deepEqual(picking.view().targets, [[0, 4], [0, 3]]);
    assert.equal(picking.click([0, 4]), "e7e8q");
});

test("no choices, nothing selectable", () => {
    const picking = picker([]);
    assert.equal(clicks(picking, 11, 15), null);
    assert.deepEqual(picking.view(), { selected: [], targets: [] });
});
