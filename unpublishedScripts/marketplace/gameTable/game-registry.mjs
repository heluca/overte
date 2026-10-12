//
//  game-registry.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The games a board can play, by the name in userData.gameTableConfig.game. Each entry is the
//  game's rules module and the board layout that draws it. Pure data and pure functions, so
//  the referee, the client and the tests share it.
//
//  An 8x8 game ("grid" layout, layout-grid.mjs) also says which side's home row is row 0 of its
//  `rows`, how a move's notation maps to the cells it passes through (for the client's clicks),
//  and which model draws each piece character. Chess registers here once chess.mjs lands:
//
//      chess: { rules: chess, layout: "grid", rowZeroSide: 1, cells: uciCells, pieces: CHESS_PIECES }
//
//  with CHESS_PIECES mapping "K", "Q", ... "p" to assets/chess/lowPoly/<piece>_lt.fbx and
//  _dk.fbx, sized from the assets/chess/*.json descriptors (in square widths).
//

import * as connect4 from "./connect4.mjs";
import * as checkers from "./checkers.mjs";

export const DEFAULT_GAME = "connect4";

/**
 * @typedef {import("./referee-core.mjs").Rules} Rules
 * @typedef {{ model: string, size: Vec3 }} PieceLook the model relative to this directory; its
 *     size in square widths
 * @typedef {{ rules: Rules, layout: "connect4" }} Connect4Entry
 * @typedef {{ rules: Rules, layout: "grid", rowZeroSide: number, cells: (text: string) => number[][] | null,
 *     pieces: Record<string, PieceLook> }} GridEntry
 * @typedef {Connect4Entry | GridEntry} Entry
 */

// The checker model is a flat disc; a king is drawn as a stack, taller than a man.
const CHECKER_MAN = { x: 0.8, y: 0.14, z: 0.8 };
const CHECKER_KING = { x: 0.8, y: 0.36, z: 0.8 };
/** @type {Record<string, PieceLook>} */
const CHECKERS_PIECES = {
    b: { model: "assets/checkers/black_checker.fbx", size: CHECKER_MAN },
    B: { model: "assets/checkers/black_checker.fbx", size: CHECKER_KING },
    r: { model: "assets/checkers/red_checker.fbx", size: CHECKER_MAN },
    R: { model: "assets/checkers/red_checker.fbx", size: CHECKER_KING }
};

/** @type {Record<string, Entry>} */
export const GAMES = {
    connect4: { rules: connect4, layout: "connect4" },
    checkers: { rules: checkers, layout: "grid", rowZeroSide: 0, cells: checkers.moveCells, pieces: CHECKERS_PIECES }
};

/**
 * The cells of a UCI move ("e2e4", "e7e8q") in rows that put rank 8 first, for chess.
 * @param {string} text
 * @returns {number[][] | null} [from, to], or null when it does not parse
 */
export function uciCells(text) {
    const match = /^([a-h])([1-8])([a-h])([1-8])[qrbn]?$/.exec(text);
    if (!match) {
        return null;
    }
    const file = (/** @type {string} */ letter) => letter.charCodeAt(0) - 97;
    return [[8 - Number(match[2]), file(match[1])], [8 - Number(match[4]), file(match[3])]];
}

/**
 * @param {unknown} name the configured game, untrusted
 * @returns {{ name: string, entry: Entry, known: boolean }} the game, or Connect Four when the
 *     name is absent or not registered (`known` is false only for a name that was given)
 */
export function gameEntry(name) {
    if (typeof name === "string" && Object.hasOwn(GAMES, name)) {
        return { name, entry: GAMES[name], known: true };
    }
    return { name: DEFAULT_GAME, entry: GAMES[DEFAULT_GAME], known: name === undefined || name === null || name === "" };
}
