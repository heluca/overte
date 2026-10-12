//
//  connect4.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { COLUMNS, ROWS, EMPTY, newBoard, legalMoves, parseColumn, applyMove, winner, isDraw, render }
    from "../connect4.mjs";

// Plays columns alternately, R first; fails the test on an illegal move.
function play(cols, board = newBoard()) {
    cols.forEach((col, i) => {
        const result = applyMove(board, col, i % 2 === 0 ? "R" : "B");
        assert.ok("board" in result, `move ${i} in column ${col}: ${"error" in result ? result.error : ""}`);
        board = result.board;
    });
    return board;
}

test("new board is empty and every column is legal", () => {
    const board = newBoard();
    assert.equal(board.length, ROWS);
    board.forEach(row => assert.equal(row, EMPTY.repeat(COLUMNS)));
    assert.deepEqual(legalMoves(board), [0, 1, 2, 3, 4, 5, 6]);
    assert.equal(winner(board), null);
    assert.equal(isDraw(board), false);
});

test("a disc lands on the lowest empty row and the input board is untouched", () => {
    const board = newBoard();
    const first = applyMove(board, 3, "R");
    assert.ok("board" in first);
    assert.equal(first.row, ROWS - 1);
    assert.equal(first.board[ROWS - 1], "...R...");
    assert.equal(board[ROWS - 1], ".......");
    const second = applyMove(first.board, 3, "B");
    assert.ok("board" in second);
    assert.equal(second.row, ROWS - 2);
    assert.equal(second.board[ROWS - 2], "...B...");
});

test("out-of-range and non-integer columns are refused", () => {
    const board = newBoard();
    for (const col of [-1, 7, 2.5, NaN, Infinity]) {
        assert.deepEqual(applyMove(board, col, "R"), { error: "column out of range" });
    }
    assert.deepEqual(applyMove(board, 0, "X"), { error: "unknown piece" });
});

test("a full column is refused and drops out of the legal moves", () => {
    const board = play([2, 2, 2, 2, 2, 2]);
    assert.equal(board.map(row => row[2]).join(""), "BRBRBR");
    assert.deepEqual(applyMove(board, 2, "R"), { error: "column full" });
    assert.deepEqual(legalMoves(board), [0, 1, 3, 4, 5, 6]);
});

test("parseColumn takes exactly one digit 0-6", () => {
    ["0", "3", "6"].forEach(text => assert.equal(parseColumn(text), Number(text)));
    for (const text of ["03", "3.0", "7", "-1", "", "x", " 3", "3 ", "+3", "٣"]) {
        assert.equal(parseColumn(text), null, JSON.stringify(text));
    }
    for (const value of [3, null, undefined, ["3"], { toString: () => "3" }]) {
        assert.equal(parseColumn(value), null);
    }
});

test("horizontal four wins", () => {
    // R: 0 1 2 3 on the bottom row; B stacks on top
    const board = play([0, 0, 1, 1, 2, 2, 3]);
    assert.deepEqual(winner(board), { piece: "R", line: [[5, 0], [5, 1], [5, 2], [5, 3]] });
});

test("vertical four wins", () => {
    const board = play([4, 5, 4, 5, 4, 5, 4]);
    assert.deepEqual(winner(board), { piece: "R", line: [[2, 4], [3, 4], [4, 4], [5, 4]] });
});

test("diagonal four rising to the right wins", () => {
    // R at (5,0) (4,1) (3,2) (2,3)
    const board = play([0, 1, 1, 2, 2, 3, 2, 3, 3, 6, 3]);
    assert.deepEqual(winner(board), { piece: "R", line: [[2, 3], [3, 2], [4, 1], [5, 0]] });
});

test("diagonal four rising to the left wins", () => {
    // R at (5,6) (4,5) (3,4) (2,3)
    const board = play([6, 5, 5, 4, 4, 3, 4, 3, 3, 0, 3]);
    assert.deepEqual(winner(board), { piece: "R", line: [[2, 3], [3, 4], [4, 5], [5, 6]] });
});

test("black can win too", () => {
    const board = play([0, 6, 1, 6, 0, 6, 1, 6]);
    assert.equal(winner(board)?.piece, "B");
});

test("three in a row is not a win", () => {
    assert.equal(winner(play([0, 0, 1, 1, 2])), null);
});

test("a full board with no four is a draw", () => {
    const board = [
        "RRBBRRB",
        "BBRRBBR",
        "RRBBRRB",
        "BBRRBBR",
        "RRBBRRB",
        "BBRRBBR"
    ];
    assert.equal(winner(board), null);
    assert.deepEqual(legalMoves(board), []);
    assert.equal(isDraw(board), true);
});

test("a full board with a four is a win, not a draw", () => {
    const board = [
        "RRRRBBR",
        "BBRRBBR",
        "RRBBRRB",
        "BBRRBBR",
        "RRBBRRB",
        "BBRRBBR"
    ];
    assert.equal(winner(board)?.piece, "R");
    assert.equal(isDraw(board), false);
});

test("render prints rows top first and the column numbers", () => {
    const board = play([3, 3]);
    assert.equal(render(board), [
        ". . . . . . .",
        ". . . . . . .",
        ". . . . . . .",
        ". . . . . . .",
        ". . . B . . .",
        ". . . R . . .",
        "0 1 2 3 4 5 6"
    ].join("\n"));
});
