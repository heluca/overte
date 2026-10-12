//
//  chess.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Chess rules (FIDE): pure functions over an immutable, JSON-serialisable state, no engine
//  globals, so the same module runs in the entity script server and under Node.
//
//  Squares are numbered 0-63 row by row, row 0 being rank 8 and column 0 file a, so a square's
//  [row, col] matches `rows`. Side 0 is White (uppercase FEN letters), side 1 Black.
//  The wire notation is UCI long algebraic: e2e4, castling as the king's move e1g1, promotion
//  with a mandatory lowercase suffix e7e8q.
//
//  The state carries its own legal move list (so legalMoves is free and play generates moves
//  once, for the position it creates) and the repetition keys since the last pawn move or
//  capture; no position before an irreversible move can recur.
//

export const GAME = "chess";
export const SEAT_NAMES = ["White", "Black"];
export const REMATCH = "swap";
export const MOVE_HINT = "move must be like e2e4 or e7e8q";

const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const EMPTY = ".";
const FILES = "abcdefgh";
const UCI = /^[a-h][1-8][a-h][1-8][qrbn]?$/;

const KNIGHT = [[-2, -1], [-2, 1], [-1, -2], [-1, 2], [1, -2], [1, 2], [2, -1], [2, 1]];
const KING = [[-1, -1], [-1, 0], [-1, 1], [0, -1], [0, 1], [1, -1], [1, 0], [1, 1]];
const ORTHO = [[-1, 0], [1, 0], [0, -1], [0, 1]];
const DIAG = [[-1, -1], [-1, 1], [1, -1], [1, 1]];
/** @type {Record<string, number[][]>} */
const SLIDES = { r: ORTHO, b: DIAG, q: KING };

// Per castling right: the king's and rook's squares before and after, the squares between them
// that must be empty, and the squares the king stands on or crosses, which must not be attacked.
/** @type {Record<string, { king: number, to: number, rook: number, rookTo: number, empty: number[], safe: number[] }>} */
const CASTLES = {
    K: { king: 60, to: 62, rook: 63, rookTo: 61, empty: [61, 62], safe: [60, 61, 62] },
    Q: { king: 60, to: 58, rook: 56, rookTo: 59, empty: [57, 58, 59], safe: [60, 59, 58] },
    k: { king: 4, to: 6, rook: 7, rookTo: 5, empty: [5, 6], safe: [4, 5, 6] },
    q: { king: 4, to: 2, rook: 0, rookTo: 3, empty: [1, 2, 3], safe: [4, 3, 2] }
};

// The castling rights lost when a move starts or ends on a king's or rook's home square.
/** @type {Record<number, string>} */
const TOUCH = { 60: "KQ", 63: "K", 56: "Q", 4: "kq", 7: "k", 0: "q" };

/**
 * @typedef {{ winner: 0 | 1 | null, reason: string, line: number[][] | null }} Outcome
 * @typedef {{
 *     board: string[], turn: 0 | 1, castling: string, ep: number | null,
 *     halfmove: number, fullmove: number, keys: string[], legal: string[], result: Outcome | null
 * }} State
 *     board: 8 rows of 8 FEN letters or ".", rank 8 first; castling: a subset of "KQkq" in that
 *     order, "" for none; ep: the square behind a pawn that just advanced two, as in FEN;
 *     keys: repetition keys since the last pawn move or capture, current position last.
 * @typedef {{
 *     side: 0 | 1, notation: string, san: string, from: number[], to: number[],
 *     captured: number[][], promoted?: string, castle?: { rookFrom: number[], rookTo: number[] }
 * }} PlayedMove
 * @typedef {{ from: number, to: number, promo: string, kind: "" | "double" | "ep" | "castle" }} Move
 */

/** @param {number} sq @returns {number[]} [row, col] */
const cell = sq => [sq >> 3, sq & 7];
/** @param {number} sq */
const squareName = sq => FILES[sq & 7] + (8 - (sq >> 3));
/** @param {string} text two characters like "e4" */
const parseSquare = text => (8 - Number(text[1])) * 8 + FILES.indexOf(text[0]);
/** @param {string} piece @returns {number} 0 White, 1 Black, -1 empty */
const owner = piece => piece === EMPTY ? -1 : piece < "a" ? 0 : 1;
/** @param {number} side @returns {0 | 1} */
const other = side => side === 0 ? 1 : 0;
/** @param {State} state */
const squaresOf = state => state.board.join("").split("");
/** @param {number} r @param {number} c */
const onBoard = (r, c) => r >= 0 && r < 8 && c >= 0 && c < 8;

/**
 * Whether `by` attacks `sq` on board `b`.
 * @param {string[]} b
 * @param {number} sq
 * @param {number} by
 */
function attacked(b, sq, by) {
    const [pawn, knight, bishop, rook, queen, king] = by === 0 ? "PNBRQK" : "pnbrqk";
    const r = sq >> 3;
    const c = sq & 7;
    const pawnRow = by === 0 ? r + 1 : r - 1;
    for (const dc of [-1, 1]) {
        if (onBoard(pawnRow, c + dc) && b[pawnRow * 8 + c + dc] === pawn) {
            return true;
        }
    }
    for (const [steps, piece] of [[KNIGHT, knight], [KING, king]]) {
        for (const [dr, dc] of /** @type {number[][]} */ (steps)) {
            if (onBoard(r + dr, c + dc) && b[(r + dr) * 8 + c + dc] === piece) {
                return true;
            }
        }
    }
    for (const [rays, slider] of [[ORTHO, rook], [DIAG, bishop]]) {
        for (const [dr, dc] of /** @type {number[][]} */ (rays)) {
            for (let rr = r + dr, cc = c + dc; onBoard(rr, cc); rr += dr, cc += dc) {
                const piece = b[rr * 8 + cc];
                if (piece !== EMPTY) {
                    if (piece === slider || piece === queen) {
                        return true;
                    }
                    break;
                }
            }
        }
    }
    return false;
}

/**
 * @param {string[]} b
 * @param {number} side
 */
function inCheck(b, side) {
    return attacked(b, b.indexOf(side === 0 ? "K" : "k"), other(side));
}

/**
 * Moves that obey how the pieces move, ignoring whether they leave the mover's king attacked
 * (castling already checks its own squares).
 * @param {string[]} b
 * @param {number} side
 * @param {string} castling
 * @param {number | null} ep
 * @returns {Move[]}
 */
function pseudoMoves(b, side, castling, ep) {
    /** @type {Move[]} */
    const moves = [];
    for (let sq = 0; sq < 64; sq++) {
        if (owner(b[sq]) !== side) {
            continue;
        }
        const type = b[sq].toLowerCase();
        const r = sq >> 3;
        const c = sq & 7;
        if (type === "p") {
            const dir = side === 0 ? -1 : 1;
            const ahead = r + dir;
            const promotes = ahead === 0 || ahead === 7;
            /** @param {number} to */
            const add = to => {
                for (const promo of promotes ? "qrbn" : [""]) {
                    moves.push({ from: sq, to, promo, kind: "" });
                }
            };
            const one = ahead * 8 + c;
            if (b[one] === EMPTY) {
                add(one);
                const two = one + dir * 8;
                if (r === (side === 0 ? 6 : 1) && b[two] === EMPTY) {
                    moves.push({ from: sq, to: two, promo: "", kind: "double" });
                }
            }
            for (const dc of [-1, 1]) {
                if (onBoard(ahead, c + dc)) {
                    const to = ahead * 8 + c + dc;
                    if (owner(b[to]) === other(side)) {
                        add(to);
                    } else if (to === ep) {
                        moves.push({ from: sq, to, promo: "", kind: "ep" });
                    }
                }
            }
        } else if (type === "n" || type === "k") {
            for (const [dr, dc] of type === "n" ? KNIGHT : KING) {
                if (onBoard(r + dr, c + dc) && owner(b[(r + dr) * 8 + c + dc]) !== side) {
                    moves.push({ from: sq, to: (r + dr) * 8 + c + dc, promo: "", kind: "" });
                }
            }
        } else {
            for (const [dr, dc] of SLIDES[type]) {
                for (let rr = r + dr, cc = c + dc; onBoard(rr, cc); rr += dr, cc += dc) {
                    const to = rr * 8 + cc;
                    const occupant = owner(b[to]);
                    if (occupant !== side) {
                        moves.push({ from: sq, to, promo: "", kind: "" });
                    }
                    if (occupant !== -1) {
                        break;
                    }
                }
            }
        }
    }
    for (const right of castling) {
        const cs = CASTLES[right];
        if (owner(right) === side
            && b[cs.king] === (side === 0 ? "K" : "k") && b[cs.rook] === (side === 0 ? "R" : "r")
            && cs.empty.every(sq => b[sq] === EMPTY)
            && !cs.safe.some(sq => attacked(b, sq, other(side)))) {
            moves.push({ from: cs.king, to: cs.to, promo: "", kind: "castle" });
        }
    }
    return moves;
}

/**
 * @param {string[]} b
 * @param {Move} m
 * @returns {number} the square of the piece `m` captures, or -1
 */
function victimOf(b, m) {
    if (m.kind === "ep") {
        return (m.from & ~7) | (m.to & 7);
    }
    return b[m.to] === EMPTY ? -1 : m.to;
}

/**
 * The board after `m`, as a new array.
 * @param {string[]} b
 * @param {Move} m
 */
function applyMove(b, m) {
    const next = b.slice();
    const piece = next[m.from];
    if (m.kind === "ep") {
        next[victimOf(b, m)] = EMPTY;
    }
    next[m.to] = m.promo ? (owner(piece) === 0 ? m.promo.toUpperCase() : m.promo) : piece;
    next[m.from] = EMPTY;
    if (m.kind === "castle") {
        const cs = castleFor(m.to);
        next[cs.rookTo] = next[cs.rook];
        next[cs.rook] = EMPTY;
    }
    return next;
}

/** @param {number} kingTo */
function castleFor(kingTo) {
    return Object.values(CASTLES).filter(cs => cs.to === kingTo)[0];
}

/**
 * @param {string[]} b
 * @param {number} side
 * @param {string} castling
 * @param {number | null} ep
 * @returns {Move[]}
 */
function legalMoveList(b, side, castling, ep) {
    return pseudoMoves(b, side, castling, ep).filter(m => !inCheck(applyMove(b, m), side));
}

/** @param {Move} m */
const uci = m => squareName(m.from) + squareName(m.to) + m.promo;

/**
 * Rebuilds the internal move for a UCI string already known to be legal.
 * @param {string[]} b
 * @param {number | null} ep
 * @param {string} text
 * @returns {Move}
 */
function moveFromUci(b, ep, text) {
    const from = parseSquare(text.slice(0, 2));
    const to = parseSquare(text.slice(2, 4));
    const type = b[from].toLowerCase();
    /** @type {Move["kind"]} */
    let kind = "";
    if (type === "p" && Math.abs(to - from) === 16) {
        kind = "double";
    } else if (type === "p" && to === ep && (to & 7) !== (from & 7)) {
        kind = "ep";
    } else if (type === "k" && Math.abs((to & 7) - (from & 7)) === 2) {
        kind = "castle";
    }
    return { from, to, promo: text.slice(4), kind };
}

/**
 * K v K, K+B v K, K+N v K, and K+B v K+B with both bishops on the same colour.
 * @param {string[]} b
 */
function insufficientMaterial(b) {
    /** @type {number[]} */
    const minors = [];
    for (let sq = 0; sq < 64; sq++) {
        const type = b[sq].toLowerCase();
        if (type === "p" || type === "r" || type === "q") {
            return false;
        }
        if (type === "b" || type === "n") {
            minors.push(sq);
        }
    }
    if (minors.length <= 1) {
        return true;
    }
    const [x, y] = minors;
    return minors.length === 2
        && b[x].toLowerCase() === "b" && b[y].toLowerCase() === "b" && owner(b[x]) !== owner(b[y])
        && (((x >> 3) + x) & 1) === (((y >> 3) + y) & 1);
}

/**
 * Assembles a state for the position given, generating its legal moves and judging it.
 * @param {string[]} b
 * @param {0 | 1} turn
 * @param {string} castling
 * @param {number | null} ep
 * @param {number} halfmove
 * @param {number} fullmove
 * @param {string[]} earlierKeys repetition keys of earlier positions that could recur
 * @returns {State}
 */
function build(b, turn, castling, ep, halfmove, fullmove, earlierKeys) {
    const moves = legalMoveList(b, turn, castling, ep);
    const board = Array.from({ length: 8 }, (_, r) => b.slice(r * 8, r * 8 + 8).join(""));
    // FIDE counts the en passant square only when the capture is actually possible.
    const epKey = ep !== null && moves.some(m => m.kind === "ep") ? squareName(ep) : "-";
    const key = [board.join("/"), turn, castling || "-", epKey].join(" ");
    const keys = earlierKeys.concat([key]);
    /** @type {Outcome | null} */
    let result = null;
    if (moves.length === 0) {
        result = inCheck(b, turn)
            ? { winner: other(turn), reason: "checkmate", line: null }
            : { winner: null, reason: "stalemate", line: null };
    } else if (insufficientMaterial(b)) {
        result = { winner: null, reason: "material", line: null };
    } else if (keys.filter(k => k === key).length >= 3) {
        result = { winner: null, reason: "repetition", line: null };
    } else if (halfmove >= 100) {
        result = { winner: null, reason: "fifty-move", line: null };
    }
    const legal = result ? [] : moves.map(uci).sort();
    return { board, turn, castling, ep, halfmove, fullmove, keys, legal, result };
}

/**
 * Standard algebraic notation for `m`, played from `state` (board `b`) and leading to `next`.
 * @param {State} state
 * @param {string[]} b
 * @param {Move} m
 * @param {boolean} capture
 * @param {State} next
 * @param {string[]} nextBoard
 */
function sanOf(state, b, m, capture, next, nextBoard) {
    const type = b[m.from].toLowerCase();
    let san;
    if (m.kind === "castle") {
        san = (m.to & 7) === 6 ? "O-O" : "O-O-O";
    } else if (type === "p") {
        san = (capture ? FILES[m.from & 7] + "x" : "") + squareName(m.to)
            + (m.promo ? "=" + m.promo.toUpperCase() : "");
    } else {
        const rivals = state.legal
            .map(text => parseSquare(text.slice(0, 2)))
            .filter((from, i) => from !== m.from && b[from] === b[m.from]
                && parseSquare(state.legal[i].slice(2, 4)) === m.to);
        let which = "";
        if (rivals.length > 0) {
            if (!rivals.some(from => (from & 7) === (m.from & 7))) {
                which = FILES[m.from & 7];
            } else if (!rivals.some(from => from >> 3 === m.from >> 3)) {
                which = String(8 - (m.from >> 3));
            } else {
                which = squareName(m.from);
            }
        }
        san = type.toUpperCase() + which + (capture ? "x" : "") + squareName(m.to);
    }
    if (next.result?.reason === "checkmate") {
        return san + "#";
    }
    return san + (inCheck(nextBoard, next.turn) ? "+" : "");
}

/**
 * A state from Forsyth-Edwards Notation; the clocks default to 0 and 1 when left out.
 * Meant for tests: the referee always starts from the initial position.
 * @param {string} fen
 * @returns {State}
 */
export function fromFen(fen) {
    const [placement, turn, castling = "-", ep = "-", half = "0", full = "1"] = fen.trim().split(/\s+/);
    /** @type {string[]} */
    const b = [];
    for (const rank of placement.split("/")) {
        const start = b.length;
        for (const ch of rank) {
            if (/^[1-8]$/.test(ch)) {
                b.push(...EMPTY.repeat(Number(ch)));
            } else if (/^[pnbrqkPNBRQK]$/.test(ch)) {
                b.push(ch);
            } else {
                throw new Error(`bad FEN piece ${ch}`);
            }
        }
        if (b.length - start !== 8) {
            throw new Error(`bad FEN rank ${rank}`);
        }
    }
    if (b.length !== 64 || b.filter(p => p === "K").length !== 1 || b.filter(p => p === "k").length !== 1
        || /[pP]/.test(b.slice(0, 8).join("") + b.slice(56).join(""))
        || !/^[wb]$/.test(turn) || !/^(-|(?=.)K?Q?k?q?)$/.test(castling) || !/^(-|[a-h][36])$/.test(ep)) {
        throw new Error(`bad FEN ${fen}`);
    }
    const side = turn === "w" ? 0 : 1;
    if (inCheck(b, other(side))) {
        throw new Error(`side not to move is in check: ${fen}`);
    }
    return build(b, side, castling === "-" ? "" : castling, ep === "-" ? null : parseSquare(ep),
        Number(half), Number(full), []);
}

/**
 * @param {number} [firstSide] ignored: White always moves first
 * @returns {State}
 */
export function newGame(firstSide = 0) {
    return fromFen(START_FEN);
}

/**
 * @param {State} state
 * @returns {0 | 1}
 */
export function sideToMove(state) {
    return state.turn;
}

/**
 * The legal moves in UCI notation, sorted; empty once the game is over.
 * @param {State} state
 * @returns {string[]}
 */
export function legalMoves(state) {
    return state.legal.slice();
}

/**
 * Plays `text`, which is untrusted.
 * @param {State} state
 * @param {unknown} text
 * @returns {{ state: State, move: PlayedMove } | { error: string }}
 */
export function play(state, text) {
    if (typeof text !== "string" || !UCI.test(text)) {
        return { error: MOVE_HINT };
    }
    if (!state.legal.includes(text)) {
        return { error: "illegal move" };
    }
    const b = squaresOf(state);
    const m = moveFromUci(b, state.ep, text);
    const side = state.turn;
    const victim = victimOf(b, m);
    const nextBoard = applyMove(b, m);
    const castling = state.castling.split("")
        .filter(right => !(TOUCH[m.from] ?? "").includes(right) && !(TOUCH[m.to] ?? "").includes(right))
        .join("");
    const irreversible = b[m.from].toLowerCase() === "p" || victim !== -1;
    const next = build(nextBoard, other(side), castling, m.kind === "double" ? (m.from + m.to) >> 1 : null,
        irreversible ? 0 : state.halfmove + 1, state.fullmove + side, irreversible ? [] : state.keys);
    /** @type {PlayedMove} */
    const move = {
        side,
        notation: text,
        san: sanOf(state, b, m, victim !== -1, next, nextBoard),
        from: cell(m.from),
        to: cell(m.to),
        captured: victim === -1 ? [] : [cell(victim)]
    };
    if (m.promo) {
        move.promoted = m.promo;
    }
    if (m.kind === "castle") {
        const cs = castleFor(m.to);
        move.castle = { rookFrom: cell(cs.rook), rookTo: cell(cs.rookTo) };
    }
    return { state: next, move };
}

/**
 * @param {State} state
 * @returns {Outcome | null}
 */
export function outcome(state) {
    return state.result;
}

/**
 * @param {State} state
 * @returns {string[]} 8 rows, rank 8 first, FEN letters and "." for empty
 */
export function rows(state) {
    return state.board.slice();
}

/**
 * The board as monospace ASCII, rank 8 first, rank numbers on the left and files underneath.
 * @param {State} state
 * @returns {string}
 */
export function render(state) {
    const lines = state.board.map((row, r) => (8 - r) + " " + row.split("").join(" "));
    lines.push("  " + FILES.split("").join(" "));
    return lines.join("\n");
}

/**
 * @param {State} state
 * @returns {string}
 */
function fenOf(state) {
    const placement = state.board.map(row => row.replace(/\.+/g, run => String(run.length))).join("/");
    return [placement, state.turn === 0 ? "w" : "b", state.castling || "-",
        state.ep === null ? "-" : squareName(state.ep), state.halfmove, state.fullmove].join(" ");
}

/**
 * Projection fields beyond the board: the position as FEN and whether the side to move is in check.
 * @param {State} state
 * @returns {{ fen: string, check: boolean }}
 */
export function extra(state) {
    return { fen: fenOf(state), check: inCheck(squaresOf(state), state.turn) };
}
