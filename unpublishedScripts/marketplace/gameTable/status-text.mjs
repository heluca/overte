//
//  status-text.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  What the referee shows on the table's GT.Status Text: the seats, whose turn it is or the
//  result, and the board as the rules render it. Pure, no engine globals.
//

const COUNTDOWN_STEP_S = 5;
/** @type {Record<string, string>} */
const WIN_TEXT = {
    four: "four in a row",
    checkmate: "checkmate"
};
// Said of the loser.
/** @type {Record<string, string>} */
const LOSS_TEXT = {
    timeout: "out of time",
    resign: "resigned",
    "no moves": "has no moves"
};
/** @type {Record<string, string>} */
const DRAW_TEXT = {
    agreement: "by agreement",
    repetition: "threefold repetition",
    "forty-move": "forty-move rule",
    "fifty-move": "fifty-move rule",
    stalemate: "stalemate",
    material: "insufficient material"
};

/**
 * @typedef {{ winner: number | null, reason: string }} Result
 * @typedef {{ names: string[], seats: ({ name: string } | null)[], phase: string, toMove: number, moveNo: number,
 *     secondsLeft: number, drawOffer: number | null, result: Result | null, board: string }} Table
 */

/**
 * @param {Result} result
 * @param {string[]} names the seat names
 * @returns {string} e.g. "Red wins (Black out of time)", "Draw (by agreement)"
 */
export function resultLine(result, names) {
    if (result.winner === null) {
        if (result.reason === "abandoned") {
            return "Game abandoned";
        }
        return result.reason === "draw" ? "Draw" : `Draw (${DRAW_TEXT[result.reason] || result.reason})`;
    }
    const why = WIN_TEXT[result.reason] || names[1 - result.winner] + " " + (LOSS_TEXT[result.reason] || result.reason);
    return `${names[result.winner]} wins (${why})`;
}

/**
 * @param {Table} table
 * @returns {string} the turn and its countdown, or the result, or who is awaited
 */
function statusLine(table) {
    const { names, phase, result } = table;
    if (phase === "playing") {
        const shown = Math.ceil(table.secondsLeft / COUNTDOWN_STEP_S) * COUNTDOWN_STEP_S;
        const offer = table.drawOffer === null ? "" : `; ${names[table.drawOffer]} offers a draw`;
        return `${names[table.toMove]} to move (move ${table.moveNo + 1}, ${shown} s)${offer}`;
    }
    if (phase === "over" && result) {
        return resultLine(result, names) + "; newGame to play again";
    }
    const waiting = table.seats.some(Boolean) ? "Waiting for an opponent" : "Waiting for players";
    return result ? resultLine(result, names) + ". " + waiting : waiting;
}

/**
 * @param {Table} table
 * @returns {string} the status board, monospace
 */
export function statusText(table) {
    const seats = table.seats.map((seat, i) => table.names[i] + ": " + (seat ? seat.name : "(empty)"));
    return [seats.join("   "), statusLine(table), table.board].join("\n");
}
