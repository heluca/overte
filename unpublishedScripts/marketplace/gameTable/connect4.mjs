//
//  connect4.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Connect Four rules: pure functions over an immutable board, no engine globals, so the
//  same module runs in the entity script server and under Node. It exports the rules module
//  contract the referee core plays every game through (GAME ... extra, below the board
//  helpers).
//
//  A board is an array of ROWS strings of COLUMNS characters, top row first; each character
//  is EMPTY or a piece. A disc dropped into a column lands on the lowest empty row.
//

export const GAME = "connect4";
export const SEAT_NAMES = ["Red", "Black"];
export const REMATCH = "alternate";
export const MOVE_HINT = "column must be one digit 0-6";

export const COLUMNS = 7;
export const ROWS = 6;
export const EMPTY = ".";
export const PIECES = ["R", "B"];
const CONNECT = 4;

const DIRECTIONS = [
    [0, 1],   // horizontal
    [1, 0],   // vertical
    [1, 1],   // diagonal, down to the right
    [1, -1]   // diagonal, down to the left
];

/** @returns {string[]} */
export function newBoard() {
    return new Array(ROWS).fill(EMPTY.repeat(COLUMNS));
}

/**
 * The columns that still have room, in ascending order.
 * @param {string[]} board
 * @returns {number[]}
 */
export function openColumns(board) {
    const moves = [];
    for (let col = 0; col < COLUMNS; col++) {
        if (board[0][col] === EMPTY) {
            moves.push(col);
        }
    }
    return moves;
}

/**
 * Parses a column as sent over the wire: exactly one digit 0-6, nothing else.
 * @param {unknown} text
 * @returns {number | null}
 */
export function parseColumn(text) {
    return typeof text === "string" && /^[0-6]$/.test(text) ? Number(text) : null;
}

/**
 * Drops `piece` into `col`.
 * @param {string[]} board
 * @param {number} col
 * @param {string} piece
 * @returns {{ board: string[], row: number } | { error: string }}
 */
export function applyMove(board, col, piece) {
    if (!PIECES.includes(piece)) {
        return { error: "unknown piece" };
    }
    if (!Number.isInteger(col) || col < 0 || col >= COLUMNS) {
        return { error: "column out of range" };
    }
    for (let row = ROWS - 1; row >= 0; row--) {
        if (board[row][col] === EMPTY) {
            const next = board.slice();
            next[row] = next[row].slice(0, col) + piece + next[row].slice(col + 1);
            return { board: next, row };
        }
    }
    return { error: "column full" };
}

/**
 * The first line of four found, scanning cells top-left to bottom-right. A move can complete
 * two lines at once; only the first is returned, which is enough to show the win.
 * @param {string[]} board
 * @returns {{ piece: string, line: number[][] } | null} line is [row, col] pairs
 */
export function winner(board) {
    for (let row = 0; row < ROWS; row++) {
        for (let col = 0; col < COLUMNS; col++) {
            const piece = board[row][col];
            if (piece === EMPTY) {
                continue;
            }
            for (const [dRow, dCol] of DIRECTIONS) {
                const line = [[row, col]];
                for (let step = 1; step < CONNECT; step++) {
                    const r = row + dRow * step;
                    const c = col + dCol * step;
                    if (r < 0 || r >= ROWS || c < 0 || c >= COLUMNS || board[r][c] !== piece) {
                        break;
                    }
                    line.push([r, c]);
                }
                if (line.length === CONNECT) {
                    return { piece, line };
                }
            }
        }
    }
    return null;
}

/**
 * @param {string[]} board
 * @returns {boolean} true when the board is full and nobody has four in a row
 */
export function isDraw(board) {
    return openColumns(board).length === 0 && winner(board) === null;
}

/**
 * The board as monospace text, top row first, with the column numbers underneath.
 * @param {string[]} board
 * @returns {string}
 */
export function renderBoard(board) {
    const lines = board.map(row => row.split("").join(" "));
    lines.push(Array.from({ length: COLUMNS }, (_, col) => String(col)).join(" "));
    return lines.join("\n");
}

// The rules module contract. A state is { board, toMove }; side 0 plays PIECES[0] (Red).

/**
 * @typedef {{ board: string[], toMove: number }} State
 * @typedef {{ side: number, notation: string, to: number[], captured: number[][] }} Move
 */

/**
 * @param {number} [firstSide] the side that moves first
 * @returns {State}
 */
export function newGame(firstSide = 0) {
    return { board: newBoard(), toMove: firstSide === 1 ? 1 : 0 };
}

/**
 * @param {State} state
 * @returns {number}
 */
export function sideToMove(state) {
    return state.toMove;
}

/**
 * @param {State} state
 * @returns {string[]} the open columns as digits, ascending; [] once the game is over
 */
export function legalMoves(state) {
    return outcome(state) ? [] : openColumns(state.board).map(String);
}

/**
 * @param {State} state
 * @param {unknown} text untrusted
 * @returns {{ state: State, move: Move } | { error: string }}
 */
export function play(state, text) {
    const col = parseColumn(text);
    if (col === null) {
        return { error: MOVE_HINT };
    }
    if (outcome(state)) {
        return { error: "illegal move" };
    }
    const side = state.toMove;
    const played = applyMove(state.board, col, PIECES[side]);
    if ("error" in played) {
        return played;
    }
    return {
        state: { board: played.board, toMove: 1 - side },
        move: { side, notation: String(col), to: [played.row, col], captured: [] }
    };
}

/**
 * @param {State} state
 * @returns {{ winner: number | null, reason: string, line: number[][] | null } | null}
 */
export function outcome(state) {
    const won = winner(state.board);
    if (won) {
        return { winner: PIECES.indexOf(won.piece), reason: "four", line: won.line };
    }
    return isDraw(state.board) ? { winner: null, reason: "draw", line: null } : null;
}

/**
 * @param {State} state
 * @returns {string[]}
 */
export function rows(state) {
    return state.board.slice();
}

/**
 * @param {State} state
 * @returns {string}
 */
export function render(state) {
    return renderBoard(state.board);
}

/**
 * @param {State} _state
 * @returns {object}
 */
export function extra(_state) {
    return {};
}
