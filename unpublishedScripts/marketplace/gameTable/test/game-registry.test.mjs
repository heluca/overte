//
//  game-registry.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { GAMES, gameEntry, DEFAULT_GAME } from "../game-registry.mjs";

const CONTRACT = ["GAME", "SEAT_NAMES", "REMATCH", "MOVE_HINT", "newGame", "sideToMove", "legalMoves", "play",
    "outcome", "rows", "render", "extra"];

test("every registered game exports the rules module contract under its own name", () => {
    for (const [name, entry] of Object.entries(GAMES)) {
        CONTRACT.forEach(key => assert.ok(key in entry.rules, name + " lacks " + key));
        assert.equal(entry.rules.GAME, name);
        assert.equal(entry.rules.SEAT_NAMES.length, 2);
        assert.ok(["alternate", "swap"].includes(entry.rules.REMATCH));
    }
});

test("the configured game is looked up; absent means Connect Four, unknown falls back with a flag", () => {
    assert.equal(DEFAULT_GAME, "connect4");
    for (const name of [undefined, null, ""]) {
        assert.deepEqual(gameEntry(name), { name: "connect4", entry: GAMES.connect4, known: true });
    }
    for (const name of ["go", "chess-960", "toString", "__proto__", 4, {}]) {
        const found = gameEntry(name);
        assert.equal(found.name, "connect4");
        assert.equal(found.known, false, String(name));
    }
    assert.equal(gameEntry("connect4").known, true);
});
