//
//  board.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Game Table web module's board geometry: which cell a move starts and ends on, whose a
//  piece is, and which way up a player sees the board. Pure functions over the referee's
//  published state (README "State" and "Notation"), so the module needs nothing else of the
//  referee's code.
//

/** The two sides' names by game; seat index is side index. */
export const SIDES = { connect4: ["Red", "Black"], checkers: ["Black", "Red"], chess: ["White", "Black"] };

const PROMOTION = { q: "Queen", r: "Rook", b: "Bishop", n: "Knight" };

/**
 * @param {number} square a checkers PDN square, 1-32 (row 0 of `rows` holds 1-4)
 * @returns {[number, number]} its [row, col] in `rows`
 */
export function checkersCell(square) {
    const row = Math.floor((square - 1) / 4);
    return [row, 2 * ((square - 1) % 4) + (row % 2 === 0 ? 1 : 0)];
}

/**
 * @param {string} square a chess square such as "e4"
 * @returns {[number, number]} its [row, col] in `rows` (rank 8 is row 0)
 */
export function chessCell(square) {
    return [8 - Number(square[1]), square.charCodeAt(0) - 97];
}

/**
 * @param {string} game "checkers" or "chess"
 * @param {string} move a legal move in the game's notation
 * @returns {{ from: [number, number], to: [number, number] }}
 */
export function moveEnds(game, move) {
    if (game === "chess") {
        return { from: chessCell(move.slice(0, 2)), to: chessCell(move.slice(2, 4)) };
    }
    const squares = move.split(/[-x]/).map(Number);
    return { from: checkersCell(squares[0]), to: checkersCell(squares[squares.length - 1]) };
}

const same = (a, b) => a[0] === b[0] && a[1] === b[1];

/** The legal moves that start on `cell`. */
export function movesFrom(game, legal, cell) {
    return legal.filter((move) => same(moveEnds(game, move).from, cell));
}

/** Of `moves`, those that end on `cell`. */
export function movesTo(game, moves, cell) {
    return moves.filter((move) => same(moveEnds(game, move).to, cell));
}

/** What a button offering `move` says when several moves share the tapped squares. */
export function moveLabel(game, move) {
    return game === "chess" && move.length === 5 ? `Promote to ${PROMOTION[move[4]]}` : move;
}

/**
 * @param {string} game
 * @param {string} piece one character of `rows`
 * @returns {number | null} the side (seat) the piece belongs to, or null for an empty cell
 */
export function pieceSide(game, piece) {
    if (piece === "." || !piece) { return null; }
    if (game === "connect4") { return piece === "R" ? 0 : 1; }
    if (game === "checkers") { return piece.toLowerCase() === "b" ? 0 : 1; }
    return piece === piece.toUpperCase() ? 0 : 1;
}

/** Whether `rows` should be drawn upside down so `seat` sees their own side nearest them. */
export function flipped(game, seat) {
    // Row 0 is Black's home in checkers (seat 0) and rank 8 in chess (seat 1).
    if (game === "checkers") { return seat === 0; }
    if (game === "chess") { return seat === 1; }
    return false;
}
