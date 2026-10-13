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
//  and which model draws each piece character.
//

import * as connect4 from "./connect4.mjs";
import * as checkers from "./checkers.mjs";
import * as chess from "./chess.mjs";

export const DEFAULT_GAME = "connect4";

/**
 * @typedef {import("./referee-core.mjs").Rules} Rules
 * @typedef {{ model: string, size: Vec3, rotation?: { x: number, y: number, z: number, w: number } }} PieceLook
 *     the model relative to this directory; its size in square widths; its rotation relative to
 *     the board (none: unrotated)
 * @typedef {{ rules: Rules, layout: "connect4" }} Connect4Entry
 * @typedef {{ rules: Rules, layout: "grid", rowZeroSide: number, cells: (text: string) => number[][] | null,
 *     pieces: Record<string, PieceLook> }} GridEntry
 * @typedef {Connect4Entry | GridEntry} Entry
 */

// The checker model is a flat disc; a king is the same model stretched taller than a man.
const CHECKER_MAN = { x: 0.8, y: 0.14, z: 0.8 };
const CHECKER_KING = { x: 0.8, y: 0.36, z: 0.8 };
/** @type {Record<string, PieceLook>} */
const CHECKERS_PIECES = {
    b: { model: "assets/checkers/black_checker.fbx", size: CHECKER_MAN },
    B: { model: "assets/checkers/black_checker.fbx", size: CHECKER_KING },
    r: { model: "assets/checkers/red_checker.fbx", size: CHECKER_MAN },
    R: { model: "assets/checkers/red_checker.fbx", size: CHECKER_KING }
};

// The assets/chess/*.json descriptors size the models in metres for the squares of the old
// table's 1.045 m mat; here they are in square widths. Letter -> model name, width, height.
const CHESS_SQUARE_M = 1.045 / 8;
/** @type {Record<string, [string, number, number]>} */
const CHESS_MODELS = {
    k: ["king", 0.0715, 0.1983],
    q: ["queen", 0.0715, 0.1665],
    r: ["rook", 0.0653, 0.1083],
    b: ["bishop", 0.0660, 0.1383],
    n: ["knight", 0.0618, 0.1209],
    p: ["pawn", 0.0603, 0.0897]
};
// The models face their local +z (the knights show it). White (seat 0) plays from the board's
// +z edge, so White's pieces turn half round to face Black; Black's face White unturned.
const HALF_TURN = { x: 0, y: 1, z: 0, w: 0 };
/** @type {Record<string, PieceLook>} */
export const CHESS_PIECES = Object.fromEntries(Object.entries(CHESS_MODELS).flatMap(([letter, [name, width, height]]) => {
    const size = { x: width / CHESS_SQUARE_M, y: height / CHESS_SQUARE_M, z: width / CHESS_SQUARE_M };
    /** @type {[string, PieceLook][]} */
    const looks = [
        [letter.toUpperCase(), { model: `assets/chess/lowPoly/${name}_lt.fbx`, size, rotation: HALF_TURN }],
        [letter, { model: `assets/chess/lowPoly/${name}_dk.fbx`, size }]
    ];
    return looks;
}));

/** @type {Record<string, Entry>} */
export const GAMES = {
    connect4: { rules: connect4, layout: "connect4" },
    checkers: { rules: checkers, layout: "grid", rowZeroSide: 0, cells: checkers.moveCells, pieces: CHECKERS_PIECES },
    chess: { rules: chess, layout: "grid", rowZeroSide: 1, cells: uciCells, pieces: CHESS_PIECES }
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
