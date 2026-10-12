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
import { planPieces, cellOffset, cellKey, GridLayout } from "../layout-grid.mjs";
import { GAMES } from "../game-registry.mjs";
import { newGame, play, rows, fromFen } from "../chess.mjs";

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

// The pieces shown for a board, as GridLayout keeps them.
function shownFor(board) {
    const shown = new Map();
    board.forEach((line, row) => line.split("").forEach((piece, col) => {
        if (piece !== ".") {
            shown.set(cellKey(row, col), piece);
        }
    }));
    return shown;
}

test("a crowning capture moves the capturer's entity and removes the captured one", () => {
    // Red's man on 10 (r2c3) takes Black's on 6 (r1c2), landing on 1 (r0c1) as a king; a
    // reconciliation by piece alone would reuse the captured black man for the red king
    const before = boardWith({ r1c2: "b", r2c3: "r", r7c2: "b" });
    const after = boardWith({ r0c1: "R", r7c2: "b" });
    const move = { side: 1, notation: "10x1", from: [2, 3], to: [0, 1], path: [[2, 3], [0, 1]], captured: [[1, 2]],
        promoted: true };
    assert.deepEqual(planPieces(shownFor(before), after, move), [
        { op: "move", from: "r2c3", to: "r0c1", piece: "R" },
        { op: "remove", from: "r1c2" }
    ]);
    // Drawn again with the same last move (a countdown redraw): nothing to do
    assert.deepEqual(planPieces(shownFor(after), after, move), []);
});

test("a king's round trip edits nothing but removes the four men it took", () => {
    const before = boardWith({ r2c3: "B", r3c2: "r", r3c4: "r", r5c2: "r", r5c4: "r" });
    const after = boardWith({ r2c3: "B" });
    const move = { from: [2, 3], to: [2, 3], captured: [[3, 2], [5, 2], [5, 4], [3, 4]] };
    assert.deepEqual(planPieces(shownFor(before), after, move), [
        { op: "remove", from: "r3c2" }, { op: "remove", from: "r5c2" },
        { op: "remove", from: "r5c4" }, { op: "remove", from: "r3c4" }
    ]);
});

test("chess moves: castling moves king and rook, en passant removes the victim, a capturing promotion", () => {
    // White castles short: e1g1 with the rook h1f1 (rank 1 is row 7)
    const castle = { from: [7, 4], to: [7, 6], captured: [], castle: { rookFrom: [7, 7], rookTo: [7, 5] } };
    assert.deepEqual(planPieces(shownFor(boardWith({ r7c4: "K", r7c7: "R" })), boardWith({ r7c6: "K", r7c5: "R" }), castle), [
        { op: "move", from: "r7c4", to: "r7c6", piece: "K" },
        { op: "move", from: "r7c7", to: "r7c5", piece: "R" }
    ]);
    // e5xd6 en passant: the black pawn on d5 goes, the white one moves
    const passant = { from: [3, 4], to: [2, 3], captured: [[3, 3]] };
    assert.deepEqual(planPieces(shownFor(boardWith({ r3c4: "P", r3c3: "p" })), boardWith({ r2c3: "P" }), passant), [
        { op: "move", from: "r3c4", to: "r2c3", piece: "P" },
        { op: "remove", from: "r3c3" }
    ]);
    // b7xa8=Q: the pawn's entity becomes the queen; the captured rook's entity goes
    const promote = { from: [1, 1], to: [0, 0], captured: [[0, 0]], promoted: "q" };
    assert.deepEqual(planPieces(shownFor(boardWith({ r1c1: "P", r0c0: "r" })), boardWith({ r0c0: "Q" }), promote), [
        { op: "move", from: "r1c1", to: "r0c0", piece: "Q" },
        { op: "remove", from: "r0c0" }
    ]);
});

test("chess moves as chess.mjs reports them take the move path", () => {
    /** @param {string} fen @param {string} text */
    const plan = (fen, text) => {
        const state = fromFen(fen);
        const result = play(state, text);
        assert.ok("move" in result, text);
        return planPieces(shownFor(rows(state)), rows(result.state), result.move);
    };
    assert.deepEqual(plan("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8"), [
        { op: "move", from: "r0c4", to: "r0c2", piece: "k" },
        { op: "move", from: "r0c0", to: "r0c3", piece: "r" }
    ]);
    assert.deepEqual(plan("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2", "e5d6"), [
        { op: "move", from: "r3c4", to: "r2c3", piece: "P" },
        { op: "remove", from: "r3c3" }
    ]);
    assert.deepEqual(plan("3r3k/4P3/8/8/8/8/8/4K3 w - - 0 1", "e7d8n"), [
        { op: "move", from: "r1c4", to: "r0c3", piece: "N" },
        { op: "remove", from: "r0c3" }
    ]);
});

test("chess pieces stand on their squares, White's turned to face Black", () => {
    const layout = new GridLayout("{board}", { x: 0.6, y: 0.04, z: 0.6 }, "https://<asset-host>/gameTable/", GAMES.chess);
    const board = rows(newGame());
    const king = layout.pieceProperties("r7c4", board[7][4]);
    const queen = layout.pieceProperties("r0c3", board[0][3]);
    assert.ok(king && queen);
    assert.equal(king.modelURL, "https://<asset-host>/gameTable/assets/chess/lowPoly/king_lt.fbx");
    assert.equal(queen.modelURL, "https://<asset-host>/gameTable/assets/chess/lowPoly/queen_dk.fbx");
    assert.deepEqual(king.localRotation, { x: 0, y: 1, z: 0, w: 0 });
    assert.equal(queen.localRotation, undefined);
    // e1 is at White's (+z) edge, d8 at Black's; the king stands on the surface, taller than wide
    assert.ok(king.localPosition.z > 0.25 && queen.localPosition.z < -0.25);
    assert.ok(Math.abs(king.localPosition.y - (0.02 + 0.004 + king.dimensions.y / 2)) < 1e-9);
    assert.ok(king.dimensions.y > king.dimensions.x && king.dimensions.x < 0.075);
    assert.equal(king.ignorePickIntersection, true);
});
