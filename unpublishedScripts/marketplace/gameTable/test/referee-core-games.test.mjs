//
//  referee-core-games.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The core's game-independent additions: draw offers, the move text cap, swapped rematches
//  and the result text. referee-core.test.mjs covers the rest, through Connect Four.
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { RefereeCore, MOVE_TEXT_LIMIT } from "../referee-core.mjs";
import { resultLine } from "../status-text.mjs";
import * as connect4 from "../connect4.mjs";

const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";
const CAROL = "{cccccccc-0000-4000-8000-000000000003}";
const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";

function fakeWorld() {
    return {
        t: 1_760_000_000_000,
        published: [],
        rendered: [],
        announced: [],
        now() { return this.t; },
        publish(state) { this.published.push(state); },
        render(view) { this.rendered.push(view); },
        announce(event) { this.announced.push(event); },
        get state() { return this.published[this.published.length - 1]; },
        get event() { return this.announced[this.announced.length - 1]; }
    };
}

function table(options = {}) {
    const world = fakeWorld();
    const core = new RefereeCore(world, { tableId: "{table}", ...options });
    core.start(null);
    return { world, core };
}

function seated(options) {
    const t = table(options);
    t.core.join(ALICE, "Alice");
    t.core.join(BOB, "Bob");
    return t;
}

function playColumns(core, cols) {
    for (const col of cols) {
        const mover = core.seats[core.toMove].session;
        assert.equal(core.move(mover, String(col)), true, `column ${col}`);
    }
}

test("a draw offer stands until the opponent accepts it or moves", () => {
    const { world, core } = seated();
    assert.equal(core.draw(ALICE), true);
    assert.equal(world.state.drawOffer, 0);
    assert.equal(world.published.at(-1).drawOffer, 0, "the offer is published");
    assert.match(world.rendered.at(-1).text, /Red to move \(move 1, 180 s\); Red offers a draw/);
    const published = world.published.length;
    assert.equal(core.draw(ALICE), true, "offering again is a no-op");
    assert.equal(world.published.length, published);
    // The offerer moving keeps the offer; the opponent moving declines it
    core.move(ALICE, "3");
    assert.equal(world.state.drawOffer, 0);
    core.move(BOB, "3");
    assert.equal(world.state.drawOffer, null);
    // Bob offers, Alice accepts
    core.draw(BOB);
    assert.equal(world.state.drawOffer, 1);
    assert.equal(core.draw(ALICE), true);
    assert.equal(world.state.phase, "over");
    assert.deepEqual(world.state.result, { winner: null, reason: "agreement", line: null });
    assert.equal(world.state.drawOffer, null);
    assert.equal(world.event.type, "over");
    assert.match(world.rendered.at(-1).text, /\nDraw \(by agreement\); newGame to play again\n/);
});

test("draw offers are refused before the game, after it and from spectators", () => {
    const { world, core } = table();
    core.join(ALICE, "Alice");
    assert.equal(core.draw(ALICE), false);
    assert.deepEqual(world.event, { type: "rejected", to: ALICE, method: "draw", reason: "game not started" });
    core.join(BOB, "Bob");
    assert.equal(core.draw(CAROL), false);
    assert.equal(world.event.reason, "not seated");
    assert.equal(core.draw(NULL_UUID), false);
    assert.equal(world.event.reason, "no caller id");
    core.leave(BOB);
    core.join(BOB, "Bob");
    playColumns(core, [0, 1, 0, 1, 0, 1, 0]);
    assert.equal(core.draw(BOB), false);
    assert.equal(world.event.reason, "game over");
});

test("move text is capped before the rules parse it", () => {
    const { world, core } = seated();
    let parsed = null;
    core.rules = { ...connect4, play(state, text) { parsed = text; return connect4.play(state, text); } };
    assert.equal(core.move(ALICE, "3".padEnd(MOVE_TEXT_LIMIT + 50, " ")), false);
    assert.equal(parsed.length, MOVE_TEXT_LIMIT);
    assert.equal(world.event.reason, "column must be one digit 0-6");
    assert.equal(core.move(ALICE, /** @type {any} */ (["3"])), false);
    assert.equal(parsed, "");
});

// Connect Four's rules under another name, with swapped rematches.
const SWAPPING = { ...connect4, GAME: "swapping", REMATCH: "swap", SEAT_NAMES: ["First", "Second"] };

test("a swap game always starts with seat 0 and swaps the players between games", () => {
    const { world, core } = seated({ rules: SWAPPING });
    assert.equal(world.state.game, "swapping");
    assert.equal(world.state.toMove, 0);
    assert.deepEqual(world.state.legal, ["0", "1", "2", "3", "4", "5", "6"], "other games publish the legal list as is");
    core.move(ALICE, "3");
    assert.deepEqual(world.state.last, { seat: 0, move: "3" });
    playColumns(core, [0, 3, 0, 3, 0, 3]);
    assert.equal(world.state.result.winner, 0);
    core.newGame(BOB);
    assert.deepEqual(world.state.seats.map(seat => seat.session), [BOB, ALICE]);
    assert.equal(world.state.toMove, 0);
    assert.equal(core.move(BOB, "2"), true);

    // A new opponent after a resignation: the player who stays changes seats too
    core.leave(ALICE);
    assert.deepEqual(world.state.seats.map(seat => seat && seat.session), [BOB, null]);
    core.join(CAROL, "Carol");
    assert.deepEqual(world.state.seats.map(seat => seat.session), [CAROL, BOB]);
    assert.equal(world.state.toMove, 0);
    // An emptied table starts over without a swap
    core.leave(CAROL);
    core.leave(BOB);
    core.join(ALICE, "Alice");
    core.join(BOB, "Bob");
    assert.deepEqual(world.state.seats.map(seat => seat.session), [ALICE, BOB]);
});

test("result lines name every reason", () => {
    const line = (winner, reason) => resultLine({ winner, reason }, ["Red", "Black"]);
    assert.equal(line(0, "no moves"), "Red wins (Black has no moves)");
    assert.equal(line(1, "checkmate"), "Black wins (checkmate)");
    assert.equal(line(null, "repetition"), "Draw (threefold repetition)");
    assert.equal(line(null, "forty-move"), "Draw (forty-move rule)");
    assert.equal(line(null, "fifty-move"), "Draw (fifty-move rule)");
    assert.equal(line(null, "stalemate"), "Draw (stalemate)");
    assert.equal(line(null, "material"), "Draw (insufficient material)");
    assert.equal(line(null, "abandoned"), "Game abandoned");
});
