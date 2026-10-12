//
//  checkers.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import * as checkers from "../checkers.mjs";

const { newGame, legalMoves, play, outcome, rows, render, sideToMove, fromPosition, squareCell, cellSquare, moveCells,
    MOVE_HINT } = checkers;

/**
 * A board from the pieces on numbered squares, e.g. { b: [1], R: [32] }.
 * @param {Record<string, number[]>} pieces
 */
function board(pieces) {
    const grid = Array.from({ length: 8 }, () => Array(8).fill("."));
    for (const [piece, squares] of Object.entries(pieces)) {
        squares.forEach(square => {
            const [row, col] = squareCell(square);
            grid[row][col] = piece;
        });
    }
    return grid.map(line => line.join(""));
}

function mustPlay(state, text) {
    const result = play(state, text);
    assert.ok("state" in result, `${text}: ${"error" in result ? result.error : ""}`);
    return result;
}

test("squares are numbered 1-32 across the dark squares, 1-4 on Black's home row", () => {
    assert.deepEqual(squareCell(1), [0, 1]);
    assert.deepEqual(squareCell(4), [0, 7]);
    assert.deepEqual(squareCell(5), [1, 0]);
    assert.deepEqual(squareCell(29), [7, 0]);
    assert.deepEqual(squareCell(32), [7, 6]);
    for (let square = 1; square <= 32; square++) {
        const [row, col] = squareCell(square);
        assert.equal((row + col) % 2, 1, "a dark square");
        assert.equal(cellSquare(row, col), square);
    }
    assert.equal(cellSquare(0, 0), 0);
    [0, 33, 1.5].forEach(square => assert.equal(squareCell(square), null));
    assert.deepEqual(moveCells("22x15x6"), [[5, 2], [3, 4], [1, 2]]);
    assert.equal(moveCells("22x40"), null);
});

test("a new game: Black on 1-12, Red on 21-32, Black to move with seven moves", () => {
    const state = newGame(1);
    assert.equal(sideToMove(state), 0, "Black moves first whatever the referee asks");
    assert.deepEqual(rows(state), [
        ".b.b.b.b", "b.b.b.b.", ".b.b.b.b", "........", "........", "r.r.r.r.", ".r.r.r.r", "r.r.r.r."
    ]);
    assert.deepEqual(legalMoves(state), ["10-14", "10-15", "11-15", "11-16", "12-16", "9-13", "9-14"]);
    assert.equal(outcome(state), null);
    assert.deepEqual(checkers.extra(state), {});
});

test("the notation is strict: anything else gets the hint, a well-formed wrong move is illegal", () => {
    const state = newGame();
    for (const text of ["", "11 - 15", "11-15 ", " 11-15", "11–15", "a-b", "11x15-19", "111-15", "11-15-19", "11x",
        "11", "x15"]) {
        assert.deepEqual(play(state, text), { error: MOVE_HINT }, JSON.stringify(text));
    }
    for (const value of [11, null, undefined, ["11-15"]]) {
        assert.deepEqual(play(state, value), { error: MOVE_HINT });
    }
    for (const text of ["15-11", "11x15", "99-10", "11-14", "21-17", "09-13"]) {
        assert.deepEqual(play(state, text), { error: "illegal move" }, text);
    }
    const { state: next, move } = mustPlay(state, "11-15");
    assert.deepEqual(move, { side: 0, notation: "11-15", from: [2, 5], to: [3, 4], path: [[2, 5], [3, 4]], captured: [],
        promoted: false });
    assert.equal(sideToMove(next), 1);
    assert.equal(rows(state)[3], "........", "the input state is untouched");
});

test("capturing is compulsory", () => {
    // Black's man on 14 can take Red's on 18; the man on 1 could otherwise step
    const state = fromPosition(board({ b: [1, 14], r: [18, 30] }), 0);
    assert.deepEqual(legalMoves(state), ["14x23"]);
    assert.deepEqual(play(state, "1-6"), { error: "illegal move" });
    const { state: next, move } = mustPlay(state, "14x23");
    assert.deepEqual(move.captured, [squareCell(18)]);
    assert.deepEqual(next.board, board({ b: [1, 23], r: [30] }));
});

test("a jump sequence continues while it can, and any sequence may be chosen", () => {
    const state = fromPosition(board({ b: [1], r: [6, 15, 7] }), 0);
    // From 1 over 6 to 10, then over 15 to 19; the part-way stop is illegal
    assert.deepEqual(legalMoves(state), ["1x10x19"]);
    assert.deepEqual(play(state, "1x10"), { error: "illegal move" });
    const { state: next, move } = mustPlay(state, "1x10x19");
    assert.deepEqual(move.path, [[0, 1], [2, 3], [4, 5]]);
    assert.deepEqual(move.captured, [squareCell(6), squareCell(15)]);
    assert.deepEqual(next.board, board({ b: [19], r: [7] }));

    // Two branches: the short one is legal too (no majority rule)
    const branching = fromPosition(board({ b: [1], r: [6, 15, 14, 23] }), 0);
    assert.deepEqual(legalMoves(branching), ["1x10x17", "1x10x19x26"].sort());
});

test("a king jumps both ways; a man only forward; a king may come back through its own square", () => {
    const pieces = { b: [14, 15, 6, 7] };
    assert.deepEqual(legalMoves(fromPosition(board({ R: [10], ...pieces }), 1)), ["10x1", "10x17", "10x19", "10x3"]);
    assert.deepEqual(legalMoves(fromPosition(board({ r: [10], ...pieces }), 1)), ["10x1", "10x3"]);
    // Round the four men and back to 10, either way round
    const square = fromPosition(board({ B: [10], r: [14, 15, 22, 23] }), 0);
    assert.deepEqual(legalMoves(square), ["10x17x26x19x10", "10x19x26x17x10"]);
    const { state, move } = mustPlay(square, "10x17x26x19x10");
    assert.equal(move.captured.length, 4);
    assert.deepEqual(state.board, board({ B: [10] }));
});

test("a man reaching the far row is crowned and the jump ends there", () => {
    // Black's man on 22 takes 26 landing on 31; as a king it could go on over 27, but may not
    const state = fromPosition(board({ b: [22], r: [26, 27, 1] }), 0);
    assert.deepEqual(legalMoves(state), ["22x31"]);
    const { state: next, move } = mustPlay(state, "22x31");
    assert.equal(move.promoted, true);
    assert.deepEqual(next.board, board({ B: [31], r: [27, 1] }));
    // A step onto the far row crowns too
    const red = mustPlay(fromPosition(board({ r: [5], b: [32] }), 1), "5-1");
    assert.equal(red.move.promoted, true);
    assert.deepEqual(red.state.board, board({ R: [1], b: [32] }));
});

test("a side that cannot move loses, blocked or with nothing left", () => {
    // Red's man on 29 is blocked by 25, and the jump to 22 is blocked too
    const blocked = mustPlay(fromPosition(board({ r: [29], b: [25, 18] }), 0), "18-22");
    assert.deepEqual(outcome(blocked.state), { winner: 0, reason: "no moves", line: null });
    assert.deepEqual(legalMoves(blocked.state), []);
    assert.deepEqual(play(blocked.state, "29-25"), { error: "illegal move" });
    const wiped = mustPlay(fromPosition(board({ b: [14], r: [18] }), 0), "14x23");
    assert.deepEqual(outcome(wiped.state), { winner: 0, reason: "no moves", line: null });
});

test("the same position three times with the same side to move is a draw", () => {
    let state = fromPosition(board({ B: [1], R: [32] }), 0);
    const shuffle = ["1-6", "32-27", "6-1", "27-32"];
    for (let ply = 0; ply < 8; ply++) {
        assert.equal(outcome(state), null, "ply " + ply);
        state = mustPlay(state, shuffle[ply % 4]).state;
    }
    assert.deepEqual(outcome(state), { winner: null, reason: "repetition", line: null });
    assert.deepEqual(legalMoves(state), []);
});

test("eighty plies without a capture or a man moving is a draw; a man moving resets the count", () => {
    const base = fromPosition(board({ B: [1], R: [32], b: [12], r: [21] }), 0);
    const late = { ...base, quiet: 79 };
    const kingMove = mustPlay(late, "1-6");
    assert.equal(kingMove.state.quiet, 80);
    assert.deepEqual(outcome(kingMove.state), { winner: null, reason: "forty-move", line: null });
    const manMove = mustPlay(late, "12-16");
    assert.equal(manMove.state.quiet, 0);
    assert.equal(outcome(manMove.state), null);
});

test("render shows the rows with the square numbers of each row", () => {
    const lines = render(newGame()).split("\n");
    assert.equal(lines.length, 8);
    assert.equal(lines[0], ". b . b . b . b    1- 4");
    assert.equal(lines[7], "r . r . r . r .   29-32");
});

// "The Perfect Game?", the Glasgow game Wyllie and Martins repeated in 21 of the 40 games of
// their match, as published move by move with a diagram after each (quadibloc.com,
// other/bo1211.htm). It uses the old from-to style for jumps too ("27-11" is 27x18x11), so each
// move is matched to the one legal move with that start and landing square.
const GLASGOW = ("11-15 23-19 8-11 22-17 11-16 24-20 16-23 27-11 7-16 20-11 3-7 28-24 7-16 24-20 16-19 25-22 4-8 " +
    "29-25 9-14 22-18 14-23 17-14 10-17 21-14 2-7 31-27 6-10 27-18 10-17 25-21 1-6 21-14 6-10 30-25 10-17 25-21 " +
    "19-23 26-19 17-22 19-15 22-26 18-14 26-31 15-10 5-9 10-3 9-18 21-17 18-22 17-14 22-26 20-16 12-19 3-12 " +
    "26-30 12-16").split(" ");

test("a published game replays move for move to its recorded final position", () => {
    let state = newGame();
    const played = [];
    for (const text of GLASGOW) {
        const [from, to] = text.split("-").map(Number);
        const matching = legalMoves(state).filter(move => {
            const squares = move.split(/[-x]/).map(Number);
            return squares[0] === from && squares[squares.length - 1] === to;
        });
        assert.equal(matching.length, 1, `${text}: ${legalMoves(state).join(" ")}`);
        const result = mustPlay(state, matching[0]);
        played.push(result.move);
        state = result.state;
    }
    assert.equal(played[7].notation, "27x18x11", "the double jump");
    assert.equal(played.filter(move => move.captured.length).length, 17);
    assert.deepEqual(played.filter(move => move.promoted).map(move => move.notation), ["26-31", "10x3", "26-30"]);
    // The final diagram: Black men 19, kings 30 and 31; Red (White there) men 14 and 32, king 16
    assert.deepEqual(rows(state), board({ b: [19], B: [30, 31], r: [14, 32], R: [16] }));
    assert.equal(sideToMove(state), 0);
    assert.equal(outcome(state), null, "abandoned as a draw by the players, not by rule");
});
