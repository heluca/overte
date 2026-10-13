//
//  chess.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import {
    GAME, SEAT_NAMES, REMATCH, MOVE_HINT, newGame, sideToMove, legalMoves, play, outcome, rows, render,
    extra, fromFen
} from "../chess.mjs";

/** @typedef {import("../chess.mjs").State} State */

/**
 * Plays UCI moves in order, failing the test on a rejected one.
 * @param {State} state
 * @param {string[]} moves
 * @returns {{ state: State, sans: string[], last: import("../chess.mjs").PlayedMove | null }}
 */
function replay(state, moves) {
    const sans = [];
    let last = null;
    for (const [i, text] of moves.entries()) {
        const result = play(state, text);
        assert.ok("state" in result, `ply ${i + 1} ${text}: ${"error" in result ? result.error : ""}`);
        state = result.state;
        last = result.move;
        sans.push(last.san);
    }
    return { state, sans, last };
}

/** @param {State} state @param {string} text */
function san(state, text) {
    const result = play(state, text);
    assert.ok("move" in result, `${text}: ${"error" in result ? result.error : ""}`);
    return result.move.san;
}

/**
 * Leaf count of the legal move tree, through the public API.
 * @param {State} state
 * @param {number} depth
 * @returns {number}
 */
function perft(state, depth) {
    const moves = legalMoves(state);
    if (depth === 1) {
        return moves.length;
    }
    let nodes = 0;
    for (const text of moves) {
        const result = play(state, text);
        assert.ok("state" in result);
        nodes += perft(result.state, depth - 1);
    }
    return nodes;
}

const PERFT = [
    ["start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", [20, 400, 8902, 197281]],
    ["Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", [48, 2039, 97862]],
    ["position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", [14, 191, 2812, 43238]]
];

for (const [label, fen, counts] of PERFT) {
    test(`perft ${label}`, t => {
        const state = fromFen(/** @type {string} */ (fen));
        /** @type {number[]} */ (counts).forEach((count, i) => {
            const started = performance.now();
            assert.equal(perft(state, i + 1), count, `depth ${i + 1}`);
            t.diagnostic(`depth ${i + 1}: ${count} in ${(performance.now() - started).toFixed(0)} ms`);
        });
    });
}

test("module constants follow the rules contract", () => {
    assert.equal(GAME, "chess");
    assert.deepEqual(SEAT_NAMES, ["White", "Black"]);
    assert.equal(REMATCH, "swap");
    assert.equal(MOVE_HINT, "move must be like e2e4 or e7e8q");
});

test("new game: White to move, initial position, JSON round trip", () => {
    const state = newGame(1);
    assert.equal(sideToMove(state), 0);
    assert.deepEqual(rows(state), [
        "rnbqkbnr", "pppppppp", "........", "........", "........", "........", "PPPPPPPP", "RNBQKBNR"
    ]);
    assert.equal(outcome(state), null);
    assert.deepEqual(extra(state), { fen: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", check: false });
    assert.equal(legalMoves(state).length, 20);
    assert.deepEqual(legalMoves(state), [...legalMoves(state)].sort());
    const copy = JSON.parse(JSON.stringify(state));
    assert.deepEqual(copy, state);
    const result = play(copy, "e2e4");
    assert.ok("state" in result);
    assert.deepEqual(copy, state, "play leaves its input untouched");
    assert.equal(sideToMove(result.state), 1);
    assert.equal(extra(result.state).fen, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
});

test("render: ranks 8 to 1 with labels, files underneath, ASCII only", () => {
    const text = render(newGame());
    assert.equal(text, [
        "8 r n b q k b n r",
        "7 p p p p p p p p",
        "6 . . . . . . . .",
        "5 . . . . . . . .",
        "4 . . . . . . . .",
        "3 . . . . . . . .",
        "2 P P P P P P P P",
        "1 R N B Q K B N R",
        "  a b c d e f g h"
    ].join("\n"));
    assert.match(text, /^[\x20-\x7e\n]*$/);
});

test("played move carries cells for the renderer", () => {
    const result = play(newGame(), "g1f3");
    assert.ok("move" in result);
    assert.deepEqual(result.move, { side: 0, notation: "g1f3", san: "Nf3", from: [7, 6], to: [5, 5], captured: [] });
});

test("SAN disambiguates by file, by rank, and by both", () => {
    const knights = fromFen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
    assert.equal(san(knights, "b1d2"), "Nbd2");
    assert.equal(san(knights, "f1d2"), "Nfd2");
    assert.equal(san(knights, "b1c3"), "Nc3");
    const rooks = fromFen("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1");
    assert.equal(san(rooks, "a1a3"), "R1a3");
    assert.equal(san(rooks, "a5a3"), "R5a3");
    const queens = fromFen("6k1/8/8/8/8/Q7/8/Q1Q4K w - - 0 1");
    assert.equal(san(queens, "a1b2"), "Qa1b2");
    assert.equal(san(queens, "a3b2"), "Q3b2");
    assert.equal(san(queens, "c1b2"), "Qcb2");
});

test("castling both ways for both sides, with the rook's cells", () => {
    const state = fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    const short = play(state, "e1g1");
    assert.ok("move" in short);
    assert.equal(short.move.san, "O-O");
    assert.deepEqual(short.move.castle, { rookFrom: [7, 7], rookTo: [7, 5] });
    assert.equal(rows(short.state)[7], "R....RK.");
    assert.equal(short.state.castling, "kq");
    const long = play(short.state, "e8c8");
    assert.ok("move" in long);
    assert.equal(long.move.san, "O-O-O");
    assert.deepEqual(long.move.castle, { rookFrom: [0, 0], rookTo: [0, 3] });
    assert.equal(rows(long.state)[0], "..kr...r");
    assert.equal(extra(long.state).fen, "2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2");
    assert.equal(san(state, "e1c1"), "O-O-O");
});

test("promotion needs a suffix, gives check, and under-promotes", () => {
    const state = fromFen("k7/4P3/8/8/8/8/8/4K3 w - - 0 1");
    assert.deepEqual(play(state, "e7e8"), { error: "illegal move" });
    const queen = play(state, "e7e8q");
    assert.ok("move" in queen);
    assert.equal(queen.move.san, "e8=Q+");
    assert.equal(queen.move.promoted, "q");
    assert.equal(rows(queen.state)[0], "k...Q...");
    assert.equal(extra(queen.state).check, true);
    assert.equal(san(state, "e7e8n"), "e8=N");
    assert.deepEqual(legalMoves(state).filter(text => text.startsWith("e7")), ["e7e8b", "e7e8n", "e7e8q", "e7e8r"]);
    const capture = fromFen("3r3k/4P3/8/8/8/8/8/4K3 w - - 0 1");
    assert.equal(san(capture, "e7d8q"), "exd8=Q+");
});

test("Fool's mate: SAN with #, outcome checkmate, no moves after", () => {
    const { state, sans } = replay(newGame(), ["f2f3", "e7e5", "g2g4", "d8h4"]);
    assert.deepEqual(sans, ["f3", "e5", "g4", "Qh4#"]);
    assert.deepEqual(outcome(state), { winner: 1, reason: "checkmate", line: null });
    assert.deepEqual(legalMoves(state), []);
    assert.equal(extra(state).check, true);
    assert.deepEqual(play(state, "e2e4"), { error: "illegal move" });
});

test("en passant removes the pawn beside the capturer", () => {
    const { state, last } = replay(newGame(), ["e2e4", "a7a6", "e4e5", "d7d5", "e5d6"]);
    assert.ok(last);
    assert.equal(last.san, "exd6");
    assert.deepEqual(last.captured, [[3, 3]]);
    assert.deepEqual(last.to, [2, 3]);
    assert.equal(rows(state)[3], "........");
    assert.equal(rows(state)[2], "p..P....");
    assert.equal(state.halfmove, 0);
});

test("en passant is only available on the very next move", () => {
    const { state } = replay(newGame(), ["e2e4", "a7a6", "e4e5", "d7d5", "g1f3", "a6a5"]);
    assert.deepEqual(play(state, "e5d6"), { error: "illegal move" });
});

test("stalemate is a draw", () => {
    const { state, sans } = replay(fromFen("k7/8/8/2Q5/8/8/8/7K w - - 0 1"), ["c5b6"]);
    assert.deepEqual(sans, ["Qb6"]);
    assert.deepEqual(outcome(state), { winner: null, reason: "stalemate", line: null });
    assert.deepEqual(legalMoves(state), []);
});

test("insufficient material: each listed case draws, others play on", () => {
    for (const fen of [
        "k7/8/8/8/8/8/8/7K w - - 0 1",           // K v K
        "k7/8/8/8/8/8/8/5B1K w - - 0 1",         // K+B v K
        "k7/8/8/8/8/8/8/5n1K w - - 0 1",         // K v K+N
        "k1b5/8/8/8/8/8/8/5B1K w - - 0 1"        // K+B v K+B, both bishops on light squares
    ]) {
        assert.deepEqual(outcome(fromFen(fen)), { winner: null, reason: "material", line: null }, fen);
    }
    for (const fen of [
        "kb6/8/8/8/8/8/8/5B1K w - - 0 1",        // bishops on opposite colours
        "kn6/8/8/8/8/8/8/5N1K w - - 0 1",        // K+N v K+N
        "k7/8/8/8/8/8/8/4BB1K w - - 0 1",        // two bishops
        "k7/8/8/8/8/8/P7/7K w - - 0 1",          // a pawn
        "k7/8/8/8/8/8/8/6RK w - - 0 1"           // a rook
    ]) {
        assert.equal(outcome(fromFen(fen)), null, fen);
    }
    const { state } = replay(fromFen("k7/8/8/8/8/8/1q6/K7 w - - 0 1"), ["a1b2"]);
    assert.deepEqual(outcome(state), { winner: null, reason: "material", line: null });
});

test("threefold repetition draws on the third occurrence", () => {
    const shuffle = ["g1f3", "g8f6", "f3g1", "f6g8"];
    const twice = replay(newGame(), [...shuffle, ...shuffle.slice(0, 3)]);
    assert.equal(outcome(twice.state), null);
    const { state } = replay(twice.state, ["f6g8"]);
    assert.deepEqual(outcome(state), { winner: null, reason: "repetition", line: null });
    assert.deepEqual(legalMoves(state), []);
});

test("repetition: the en passant square counts only when the capture is legal", () => {
    // After e2e4 Black's d4 pawn could take en passant; the knights then shuffle back to the
    // same placement with Black to move but no en passant square.
    const shuffle = ["a8b6", "a3b1", "b6a8", "b1a3"];
    const moves = ["e2e4", ...shuffle, ...shuffle];
    // Legal en passant: the position after e2e4 differs, so two shuffles make only two repeats.
    const open = replay(fromFen("n7/8/8/4k3/3p4/N7/4P3/7K w - - 0 1"), moves);
    assert.ok(legalMoves(replay(fromFen("n7/8/8/4k3/3p4/N7/4P3/7K w - - 0 1"), ["e2e4"]).state).includes("d4e3"));
    assert.equal(outcome(open.state), null);
    // The bishop pins d4 to the king along b2-e5, so the capture is illegal and the position
    // after e2e4 already counts as the first occurrence.
    const pinned = replay(fromFen("n7/8/8/4k3/3p4/N7/1B2P3/7K w - - 0 1"), moves);
    assert.ok(!legalMoves(replay(fromFen("n7/8/8/4k3/3p4/N7/1B2P3/7K w - - 0 1"), ["e2e4"]).state).includes("d4e3"));
    assert.deepEqual(outcome(pinned.state), { winner: null, reason: "repetition", line: null });
});

test("fifty-move rule: 100 plies without pawn move or capture", () => {
    const state = fromFen("k7/8/8/8/8/8/P7/6RK w - - 98 80");
    const quiet = replay(state, ["g1g2"]);
    assert.equal(outcome(quiet.state), null);
    const { state: drawn } = replay(quiet.state, ["a8b8"]);
    assert.equal(drawn.halfmove, 100);
    assert.deepEqual(outcome(drawn), { winner: null, reason: "fifty-move", line: null });
    const pawn = replay(fromFen("k7/8/8/8/8/8/P7/6RK w - - 99 80"), ["a2a3"]);
    assert.equal(outcome(pawn.state), null);
    assert.equal(pawn.state.halfmove, 0);
});

test("checkmate on the hundredth ply is a win, not a fifty-move draw", () => {
    const { state, sans } = replay(fromFen("k7/8/1K6/8/8/8/8/6R1 w - - 99 80"), ["g1g8"]);
    assert.deepEqual(sans, ["Rg8#"]);
    assert.deepEqual(outcome(state), { winner: 0, reason: "checkmate", line: null });
});

test("illegal moves: into check, pinned piece, castling rules", () => {
    const king = fromFen("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    assert.deepEqual(play(king, "e1d2"), { error: "illegal move" });
    assert.ok("state" in play(king, "e1e2"));
    const pinned = fromFen("4k3/4r3/8/8/8/8/4N3/4K3 w - - 0 1");
    assert.deepEqual(play(pinned, "e2c3"), { error: "illegal move" });
    const through = fromFen("4kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    assert.deepEqual(play(through, "e1g1"), { error: "illegal move" });
    assert.ok("state" in play(through, "e1c1"));
    const free = fromFen("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    assert.ok("state" in play(free, "e1g1"));
    const inCheck = fromFen("4k3/4r3/8/8/8/8/8/R3K2R w KQ - 0 1");
    assert.deepEqual(play(inCheck, "e1g1"), { error: "illegal move" });
    assert.deepEqual(play(inCheck, "e1c1"), { error: "illegal move" });
    const blocked = fromFen("4k3/8/8/8/8/8/8/RN2K2R w KQ - 0 1");
    assert.deepEqual(play(blocked, "e1c1"), { error: "illegal move" });
    const moved = replay(fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), ["e1f1", "a8b8", "f1e1", "b8a8"]);
    assert.equal(moved.state.castling, "k");
    assert.deepEqual(play(moved.state, "e1g1"), { error: "illegal move" });
    assert.deepEqual(play(moved.state, "e1c1"), { error: "illegal move" });
    assert.deepEqual(play(newGame(), "e2e4q"), { error: "illegal move" });
    assert.deepEqual(play(newGame(), "e2e5"), { error: "illegal move" });
    assert.deepEqual(play(newGame(), "e7e5"), { error: "illegal move" }, "not your piece");
});

test("text that does not parse gets the move hint", () => {
    for (const text of [
        "E2E4", "e2E4", "Nf3", "e2-e4", "e2 e4", " e2e4", "e2e4 ", "e2e4\n", "e7e8Q", "e7e8k", "e9e4",
        "i2i4", "e2", "", "O-O", "e2e4".repeat(20), "x".repeat(100)
    ]) {
        assert.deepEqual(play(newGame(), text), { error: MOVE_HINT }, JSON.stringify(text));
    }
    for (const value of [null, undefined, 42, ["e2e4"], { toString: () => "e2e4" }]) {
        assert.deepEqual(play(newGame(), value), { error: MOVE_HINT });
    }
});

test("the Opera Game (Morphy, Paris 1858) replays to its mating position", () => {
    const uci = [
        "e2e4", "e7e5", "g1f3", "d7d6", "d2d4", "c8g4", "d4e5", "g4f3", "d1f3", "d6e5", "f1c4", "g8f6",
        "f3b3", "d8e7", "b1c3", "c7c6", "c1g5", "b7b5", "c3b5", "c6b5", "c4b5", "b8d7", "e1c1", "a8d8",
        "d1d7", "d8d7", "h1d1", "e7e6", "b5d7", "f6d7", "b3b8", "d7b8", "d1d8"
    ];
    const { state, sans } = replay(newGame(), uci);
    assert.deepEqual(sans, [
        "e4", "e5", "Nf3", "d6", "d4", "Bg4", "dxe5", "Bxf3", "Qxf3", "dxe5", "Bc4", "Nf6",
        "Qb3", "Qe7", "Nc3", "c6", "Bg5", "b5", "Nxb5", "cxb5", "Bxb5+", "Nbd7", "O-O-O", "Rd8",
        "Rxd7", "Rxd7", "Rd1", "Qe6", "Bxd7+", "Nxd7", "Qb8+", "Nxb8", "Rd8#"
    ]);
    assert.deepEqual(extra(state), { fen: "1n1Rkb1r/p4ppp/4q3/4p1B1/4P3/8/PPP2PPP/2K5 b k - 1 17", check: true });
    assert.deepEqual(outcome(state), { winner: 0, reason: "checkmate", line: null });
});
