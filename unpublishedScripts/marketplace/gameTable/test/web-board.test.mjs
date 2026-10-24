//
//  web-board.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The web module's board geometry (web/board.mjs) against the referee's notation and rows.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { checkersCell, chessCell, flipped, moveEnds, moveLabel, movesFrom, movesTo, pieceSide } from "../web/board.mjs";
import * as checkers from "../checkers.mjs";
import * as chess from "../chess.mjs";

test("every checkers square lands on a dark square holding the opening's men", () => {
    const rows = checkers.rows(checkers.newGame());
    for (let square = 1; square <= 32; square++) {
        const [row, col] = checkersCell(square);
        assert.equal((row + col) % 2, 1, `square ${square} is dark`);
        assert.equal(rows[row][col], square <= 12 ? "b" : square >= 21 ? "r" : ".", `square ${square}`);
    }
});

test("a legal move starts on its piece and ends on an empty square, in both notations", () => {
    for (const rules of [checkers, chess]) {
        const state = rules.newGame();
        const rows = rules.rows(state);
        for (const move of rules.legalMoves(state)) {
            const { from, to } = moveEnds(rules.GAME, move);
            assert.equal(pieceSide(rules.GAME, rows[from[0]][from[1]]), rules.sideToMove(state), move);
            assert.equal(rows[to[0]][to[1]], ".", move);
        }
    }
    assert.deepEqual(moveEnds("checkers", "22x15x6"), { from: checkersCell(22), to: checkersCell(6) });
    assert.deepEqual(chessCell("a8"), [0, 0]);
    assert.deepEqual(chessCell("h1"), [7, 7]);
});

test("taps pick moves from a square and to a square; promotions say which piece", () => {
    const legal = ["e7e8q", "e7e8r", "e7e8b", "e7e8n", "e7d8q", "a2a3"];
    const fromE7 = movesFrom("chess", legal, chessCell("e7"));
    assert.equal(fromE7.length, 5);
    assert.deepEqual(movesTo("chess", fromE7, chessCell("e8")), ["e7e8q", "e7e8r", "e7e8b", "e7e8n"]);
    assert.equal(moveLabel("chess", "e7e8n"), "Promote to Knight");
    assert.equal(moveLabel("checkers", "22x15x6"), "22x15x6");
});

test("each player sees their own side nearest them", () => {
    assert.equal(flipped("checkers", 0), true);
    assert.equal(flipped("checkers", 1), false);
    assert.equal(flipped("chess", 0), false);
    assert.equal(flipped("chess", 1), true);
    assert.equal(flipped("connect4", 1), false);
    assert.deepEqual([pieceSide("connect4", "R"), pieceSide("connect4", "B"), pieceSide("chess", "k"), pieceSide("checkers", "B"), pieceSide("chess", ".")],
        [0, 1, 1, 0, null]);
});
