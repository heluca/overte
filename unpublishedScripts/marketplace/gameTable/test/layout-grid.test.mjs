//
//  layout-grid.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { planPieces, cellOffset, cellKey } from "../layout-grid.mjs";

const EMPTY_ROW = "........";

function boardWith(cells) {
    const grid = Array.from({ length: 8 }, () => EMPTY_ROW.split(""));
    Object.entries(cells).forEach(([cell, piece]) => {
        const [row, col] = cell.match(/\d+/g).map(Number);
        grid[row][col] = piece;
    });
    return grid.map(line => line.join(""));
}

test("row 0 lies at the edge of its side's seat: seat 0 is +z, and each player's near left is dark", () => {
    // Checkers: row 0 is Black's (seat 0); Black's near-left corner (row 0, col 7) is -x for a
    // player at +z facing -z
    assert.deepEqual(cellOffset(0, 7, 0), { x: -3.5, z: 3.5 });
    assert.deepEqual(cellOffset(7, 0, 0), { x: 3.5, z: -3.5 });
    // Chess: row 7 is rank 1, White's (seat 0); a1 (row 7, col 0) is White's near left
    assert.deepEqual(cellOffset(7, 0, 1), { x: -3.5, z: 3.5 });
    assert.deepEqual(cellOffset(0, 7, 1), { x: 3.5, z: -3.5 });
});

test("a step moves the piece's entity; nothing is added or removed", () => {
    const before = new Map([["r2c5", "b"], ["r5c0", "r"]]);
    assert.deepEqual(planPieces(before, boardWith({ r3c4: "b", r5c0: "r" })), [
        { op: "move", from: "r2c5", to: "r3c4", piece: "b" }
    ]);
    assert.deepEqual(planPieces(before, boardWith({ r2c5: "b", r5c0: "r" })), [], "nothing changed");
});

test("a capture moves the capturer and removes the captured", () => {
    const before = new Map([["r5c2", "r"], ["r4c3", "b"], ["r0c1", "b"]]);
    assert.deepEqual(planPieces(before, boardWith({ r3c4: "r", r0c1: "b" })), [
        { op: "move", from: "r5c2", to: "r3c4", piece: "r" },
        { op: "remove", from: "r4c3" }
    ]);
});

test("a crowning reuses the man's entity for the king; a fresh board adds what is missing", () => {
    assert.deepEqual(planPieces(new Map([["r6c3", "b"]]), boardWith({ r7c4: "B" })), [
        { op: "move", from: "r6c3", to: "r7c4", piece: "B" }
    ]);
    assert.deepEqual(planPieces(new Map([["r7c4", "b"]]), boardWith({ r7c4: "B" })), [
        { op: "move", from: "r7c4", to: "r7c4", piece: "B" }
    ], "a change in place is an edit");
    assert.deepEqual(planPieces(new Map(), boardWith({ r0c1: "b", r7c0: "r" })), [
        { op: "add", to: "r0c1", piece: "b" },
        { op: "add", to: "r7c0", piece: "r" }
    ]);
    assert.equal(cellKey(3, 4), "r3c4");
});
