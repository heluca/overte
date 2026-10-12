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

import * as connect4 from "./connect4.mjs";

export const DEFAULT_GAME = "connect4";

/**
 * @typedef {import("./referee-core.mjs").Rules} Rules
 * @typedef {{ rules: Rules, layout: "connect4" }} Entry
 */

/** @type {Record<string, Entry>} */
export const GAMES = {
    connect4: { rules: connect4, layout: "connect4" }
};

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
