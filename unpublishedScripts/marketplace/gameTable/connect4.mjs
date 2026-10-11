//
//  connect4.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Connect Four rules: pure functions over an immutable board, no engine globals, so the
//  same module runs in the entity script server and under Node.
//
//  A board is an array of ROWS strings of COLUMNS characters, top row first; each character
//  is EMPTY or a piece. A disc dropped into a column lands on the lowest empty row.
//

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
export function legalMoves(board) {
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
    return legalMoves(board).length === 0 && winner(board) === null;
}

/**
 * The board as monospace text, top row first, with the column numbers underneath.
 * @param {string[]} board
 * @returns {string}
 */
export function render(board) {
    const lines = board.map(row => row.split("").join(" "));
    lines.push(Array.from({ length: COLUMNS }, (_, col) => String(col)).join(" "));
    return lines.join("\n");
}
