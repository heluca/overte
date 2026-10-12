//
//  checkers.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  English draughts (American checkers) rules for the referee, as the rules module contract
//  documented in connect4.mjs: pure functions over an immutable state, no engine globals.
//
//  The board is 8 strings of 8 characters, row 0 (Black's home row) first: "." for an empty or
//  light square, "b" / "r" for Black and Red men, "B" / "R" for kings. The dark squares are the
//  ones with (row + col) odd, numbered 1-32 in the standard (PDN) way: 1-4 along row 0, 29-32
//  along row 7. Black (side 0) starts on 1-12 and moves first; Red (side 1) starts on 21-32.
//
//  Men move and capture diagonally forward, kings both ways, one square at a time. Capturing
//  is compulsory and a capturing sequence continues while it can, though any sequence may be
//  chosen; a man reaching the far row is crowned and the move ends there. A side with no legal
//  move loses. A position repeated three times with the same side to move, or 80 plies without
//  a capture or a man moving, is drawn.
//

export const GAME = "checkers";
export const SEAT_NAMES = ["Black", "Red"];
export const REMATCH = "swap";
export const MOVE_HINT = "move must be like 11-15 or 22x15x6";

export const SIZE = 8;
export const EMPTY = ".";
const MEN = ["b", "r"];
const KINGS = ["B", "R"];
// The row direction each side's men move in, and the row they are crowned on.
const FORWARD = [1, -1];
const CROWN_ROW = [SIZE - 1, 0];
const QUIET_LIMIT = 80;
const REPEATS = 3;
const NOTATION = /^\d{1,2}(-\d{1,2}|(x\d{1,2})+)$/;

/**
 * @typedef {{ board: string[], toMove: number, quiet: number, seen: string[] }} State
 *     quiet: plies since the last capture or man move; seen: the position keys since then,
 *     the current one last (no earlier position can recur)
 * @typedef {{ notation: string, path: number[][], captured: number[][], promoted: boolean }} Candidate
 * @typedef {{ side: number, notation: string, from: number[], to: number[], path: number[][],
 *     captured: number[][], promoted: boolean }} Move
 */

/**
 * @param {number} square 1-32
 * @returns {number[] | null} its [row, col], or null when out of range
 */
export function squareCell(square) {
    if (!Number.isInteger(square) || square < 1 || square > 32) {
        return null;
    }
    const row = Math.floor((square - 1) / 4);
    const col = 2 * ((square - 1) % 4) + (row % 2 === 0 ? 1 : 0);
    return [row, col];
}

/**
 * @param {number} row
 * @param {number} col
 * @returns {number} the square number, or 0 for a light square
 */
export function cellSquare(row, col) {
    return (row + col) % 2 === 1 ? row * 4 + Math.floor(col / 2) + 1 : 0;
}

/**
 * The cells a move's notation passes through, for the client to match clicks against.
 * @param {string} text a move as legalMoves lists it
 * @returns {number[][] | null} [from, ..., to], or null when it does not parse
 */
export function moveCells(text) {
    if (!NOTATION.test(text)) {
        return null;
    }
    const cells = text.split(/[-x]/).map(Number).map(squareCell);
    return cells.every(Boolean) ? /** @type {number[][]} */ (cells) : null;
}

/**
 * @param {string} piece
 * @returns {number} 0 for Black, 1 for Red, -1 for an empty square
 */
function sideOf(piece) {
    return piece === "b" || piece === "B" ? 0 : piece === "r" || piece === "R" ? 1 : -1;
}

/**
 * @param {string[]} board
 * @param {number} toMove
 * @returns {string}
 */
function positionKey(board, toMove) {
    return board.join("/") + ":" + toMove;
}

/**
 * A position for tests and analysis; the referee only ever starts from newGame().
 * @param {string[]} board 8 rows as `rows` gives them
 * @param {number} [toMove]
 * @returns {State}
 */
export function fromPosition(board, toMove = 0) {
    return { board: board.slice(), toMove, quiet: 0, seen: [positionKey(board, toMove)] };
}

/**
 * @param {number} [_firstSide] ignored: Black always moves first
 * @returns {State}
 */
export function newGame(_firstSide = 0) {
    const board = Array.from({ length: SIZE }, (_, row) => Array.from({ length: SIZE }, (_, col) => {
        if ((row + col) % 2 === 0 || (row > 2 && row < SIZE - 3)) {
            return EMPTY;
        }
        return row < 3 ? MEN[0] : MEN[1];
    }).join(""));
    return fromPosition(board, 0);
}

/**
 * @param {State} state
 * @returns {number}
 */
export function sideToMove(state) {
    return state.toMove;
}

/**
 * @param {number} row
 * @param {number} col
 * @returns {boolean}
 */
function inside(row, col) {
    return row >= 0 && row < SIZE && col >= 0 && col < SIZE;
}

/**
 * @param {string} piece
 * @returns {number[]} the row directions the piece moves in
 */
function rowSteps(piece) {
    return KINGS.includes(piece) ? [1, -1] : [FORWARD[sideOf(piece)]];
}

/**
 * Every capturing sequence for the side to move that cannot be continued. Captured men stay
 * on the board until the move ends (so they block, and cannot be jumped twice); the moving
 * piece's own square counts as empty, so a king can come back to it.
 * @param {string[]} board
 * @param {number} side
 * @returns {Candidate[]}
 */
function jumps(board, side) {
    /** @type {Candidate[]} */
    const found = [];
    for (let row = 0; row < SIZE; row++) {
        for (let col = 0; col < SIZE; col++) {
            const piece = board[row][col];
            if (sideOf(piece) !== side) {
                continue;
            }
            const empty = board.map((line, r) => r === row ? line.slice(0, col) + EMPTY + line.slice(col + 1) : line);
            /**
             * @param {number[][]} path
             * @param {number[][]} captured
             */
            const extend = (path, captured) => {
                const [r, c] = path[path.length - 1];
                let extended = false;
                for (const dr of rowSteps(piece)) {
                    for (const dc of [-1, 1]) {
                        const [mr, mc, lr, lc] = [r + dr, c + dc, r + 2 * dr, c + 2 * dc];
                        if (!inside(lr, lc) || sideOf(empty[mr][mc]) !== 1 - side || empty[lr][lc] !== EMPTY
                            || captured.some(([cr, cc]) => cr === mr && cc === mc)) {
                            continue;
                        }
                        extended = true;
                        const nextPath = [...path, [lr, lc]];
                        const nextCaptured = [...captured, [mr, mc]];
                        if (MEN.includes(piece) && lr === CROWN_ROW[side]) {
                            found.push(candidate(nextPath, nextCaptured, true));
                        } else {
                            extend(nextPath, nextCaptured);
                        }
                    }
                }
                if (!extended && path.length > 1) {
                    found.push(candidate(path, captured, false));
                }
            };
            extend([[row, col]], []);
        }
    }
    return found;
}

/**
 * @param {number[][]} path
 * @param {number[][]} captured
 * @param {boolean} promoted
 * @returns {Candidate}
 */
function candidate(path, captured, promoted) {
    const separator = captured.length ? "x" : "-";
    return { notation: path.map(([r, c]) => cellSquare(r, c)).join(separator), path, captured, promoted };
}

/**
 * @param {string[]} board
 * @param {number} side
 * @returns {Candidate[]} the one-square moves, for when there is no capture
 */
function steps(board, side) {
    /** @type {Candidate[]} */
    const found = [];
    for (let row = 0; row < SIZE; row++) {
        for (let col = 0; col < SIZE; col++) {
            const piece = board[row][col];
            if (sideOf(piece) !== side) {
                continue;
            }
            for (const dr of rowSteps(piece)) {
                for (const dc of [-1, 1]) {
                    const [r, c] = [row + dr, col + dc];
                    if (inside(r, c) && board[r][c] === EMPTY) {
                        found.push(candidate([[row, col], [r, c]], [], MEN.includes(piece) && r === CROWN_ROW[side]));
                    }
                }
            }
        }
    }
    return found;
}

/**
 * @param {State} state
 * @returns {Candidate[]}
 */
function candidates(state) {
    const captures = jumps(state.board, state.toMove);
    return captures.length ? captures : steps(state.board, state.toMove);
}

/**
 * @param {State} state
 * @returns {string[]} sorted; [] once the game is over
 */
export function legalMoves(state) {
    return outcome(state) ? [] : candidates(state).map(move => move.notation).sort();
}

/**
 * @param {State} state
 * @param {unknown} text untrusted
 * @returns {{ state: State, move: Move } | { error: string }}
 */
export function play(state, text) {
    if (typeof text !== "string" || !NOTATION.test(text)) {
        return { error: MOVE_HINT };
    }
    if (outcome(state)) {
        return { error: "illegal move" };
    }
    const chosen = candidates(state).find(move => move.notation === text);
    if (!chosen) {
        return { error: "illegal move" };
    }
    const side = state.toMove;
    const from = chosen.path[0];
    const to = chosen.path[chosen.path.length - 1];
    const piece = state.board[from[0]][from[1]];
    const grid = state.board.map(line => line.split(""));
    grid[from[0]][from[1]] = EMPTY;
    chosen.captured.forEach(([r, c]) => {
        grid[r][c] = EMPTY;
    });
    grid[to[0]][to[1]] = chosen.promoted ? KINGS[side] : piece;
    const board = grid.map(line => line.join(""));
    const toMove = 1 - side;
    const key = positionKey(board, toMove);
    const irreversible = chosen.captured.length > 0 || MEN.includes(piece);
    return {
        state: {
            board,
            toMove,
            quiet: irreversible ? 0 : state.quiet + 1,
            seen: irreversible ? [key] : [...state.seen, key]
        },
        move: {
            side,
            notation: chosen.notation,
            from,
            to,
            path: chosen.path,
            captured: chosen.captured,
            promoted: chosen.promoted
        }
    };
}

/**
 * @param {State} state
 * @returns {{ winner: number | null, reason: string, line: null } | null}
 */
export function outcome(state) {
    if (candidates(state).length === 0) {
        return { winner: 1 - state.toMove, reason: "no moves", line: null };
    }
    const key = state.seen[state.seen.length - 1];
    if (state.seen.filter(seen => seen === key).length >= REPEATS) {
        return { winner: null, reason: "repetition", line: null };
    }
    if (state.quiet >= QUIET_LIMIT) {
        return { winner: null, reason: "forty-move", line: null };
    }
    return null;
}

/**
 * @param {State} state
 * @returns {string[]}
 */
export function rows(state) {
    return state.board.slice();
}

/**
 * The board as monospace text, Black's side on top, with the square numbers of each row.
 * @param {State} state
 * @returns {string}
 */
export function render(state) {
    return state.board.map((line, row) => {
        const first = row * 4 + 1;
        return line.split("").join(" ") + "   " + String(first).padStart(2) + "-" + String(first + 3).padStart(2);
    }).join("\n");
}

/**
 * @param {State} _state
 * @returns {object}
 */
export function extra(_state) {
    return {};
}
