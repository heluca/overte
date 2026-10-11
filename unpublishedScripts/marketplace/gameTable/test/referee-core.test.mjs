//
//  referee-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { RefereeCore, channelFor, DEFAULT_TIMEOUTS, NAME_LIMIT } from "../referee-core.mjs";

const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";
const CAROL = "{cccccccc-0000-4000-8000-000000000003}";
const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";
const T0 = 1_760_000_000_000;

function fakeWorld() {
    return {
        t: T0,
        published: [],
        rendered: [],
        announced: [],
        now() { return this.t; },
        publish(state) { this.published.push(state); },
        render(view) { this.rendered.push(view); },
        announce(event) { this.announced.push(event); },
        get state() { return this.published[this.published.length - 1]; },
        get event() { return this.announced[this.announced.length - 1]; },
        advance(seconds) { this.t += seconds * 1000; }
    };
}

function table(options = {}) {
    const world = fakeWorld();
    const core = new RefereeCore(world, { tableId: "{table}", ...options });
    core.start(null);
    return { world, core };
}

// A table with Alice in seat 0 (Red, first to move) and Bob in seat 1.
function seated(options) {
    const t = table(options);
    t.core.join(ALICE, "Alice");
    t.core.join(BOB, "Bob");
    return t;
}

// Plays columns alternately starting with whoever is to move.
function playColumns(core, cols) {
    for (const col of cols) {
        const mover = core.seats[core.toMove].session;
        assert.equal(core.move(mover, String(col)), true, `column ${col}`);
    }
}

test("start publishes an empty waiting table with the full state shape", () => {
    const { world } = table();
    assert.deepEqual(world.state, {
        v: 1,
        game: "connect4",
        gameId: null,
        phase: "waiting",
        seats: [null, null],
        toMove: null,
        moveNo: 0,
        rows: [".......", ".......", ".......", ".......", ".......", "......."],
        legal: [],
        last: null,
        result: null,
        turnDeadline: null,
        channel: "gametable:{table}"
    });
    assert.equal(channelFor("{table}"), "gametable:{table}");
    assert.match(world.rendered[0].text, /^Red: \(empty\) {3}Black: \(empty\)\nWaiting for players\n/);
});

test("start after a reload reports the abandoned game and starts clean", () => {
    const world = fakeWorld();
    const core = new RefereeCore(world, { tableId: "{table}" });
    core.start({ phase: "playing", gameId: "{table}:123", rows: ["RRRRRRR"] });
    assert.deepEqual(world.announced, [{
        type: "over", gameId: "{table}:123", result: { winner: null, reason: "abandoned", line: null }, seats: [null, null]
    }]);
    assert.equal(world.state.phase, "waiting");
    assert.equal(world.state.rows[0], ".......");

    const quiet = fakeWorld();
    new RefereeCore(quiet, {}).start({ phase: "over" });
    assert.deepEqual(quiet.announced, []);
});

test("join seats callers in order and the second join starts the game", () => {
    const { world, core } = table();
    assert.equal(core.join(ALICE, "Alice"), true);
    assert.equal(world.state.phase, "waiting");
    assert.deepEqual(world.state.seats, [{ session: ALICE, name: "Alice" }, null]);
    assert.equal(core.join(BOB, "Bob"), true);
    const state = world.state;
    assert.equal(state.phase, "playing");
    assert.deepEqual(state.seats, [{ session: ALICE, name: "Alice" }, { session: BOB, name: "Bob" }]);
    assert.equal(state.toMove, 0);
    assert.equal(state.gameId, "{table}:" + T0);
    assert.deepEqual(state.legal, [0, 1, 2, 3, 4, 5, 6]);
    assert.equal(state.turnDeadline, T0 + DEFAULT_TIMEOUTS.turnTimeoutS * 1000);
    assert.match(world.rendered[world.rendered.length - 1].text, /^Red: Alice {3}Black: Bob\nRed to move \(move 1, 180 s\)/);
});

test("join is idempotent for a seated caller and refuses a third player", () => {
    const { world, core } = table();
    core.join(ALICE, "Alice");
    const published = world.published.length;
    assert.equal(core.join(ALICE, "Alice again"), true);
    assert.equal(world.published.length, published, "a repeated join stores nothing");
    assert.equal(world.event.type, "state");
    assert.deepEqual(world.event.seats, [{ session: ALICE, name: "Alice" }, null]);
    core.join(BOB, "Bob");
    assert.equal(core.join(BOB), true);
    assert.equal(world.state.phase, "playing");
    assert.equal(core.join(CAROL, "Carol"), false);
    assert.deepEqual(world.event, { type: "rejected", to: CAROL, method: "join", reason: "table full" });
});

test("join takes the seat asked for, and refuses a taken or malformed one", () => {
    const { world, core } = table();
    assert.equal(core.join(ALICE, "Alice", "1"), true);
    assert.deepEqual(world.state.seats, [null, { session: ALICE, name: "Alice" }]);
    const published = world.published.length;
    assert.equal(core.join(BOB, "Bob", "1"), false);
    assert.deepEqual(world.event, { type: "rejected", to: BOB, method: "join", reason: "seat taken" });
    ["2", "01", "-1", "x", " 0"].forEach(seat => {
        assert.equal(core.join(BOB, "Bob", seat), false);
        assert.deepEqual(world.event, { type: "rejected", to: BOB, method: "join", reason: "seat must be 0 or 1" });
    });
    assert.equal(world.published.length, published, "a refused join stores nothing");
    // A seated caller asking for the other seat keeps the one it has
    assert.equal(core.join(ALICE, "Alice", "0"), true);
    assert.deepEqual(world.event.seats, [null, { session: ALICE, name: "Alice" }]);
    assert.equal(core.join(BOB, "Bob", "0"), true);
    assert.deepEqual(world.state.seats, [{ session: BOB, name: "Bob" }, { session: ALICE, name: "Alice" }]);
    assert.equal(world.state.phase, "playing");
    assert.equal(world.state.toMove, 0);
});

test("join without a seat takes the free one, whichever it is", () => {
    const { world, core } = table();
    core.join(ALICE, "Alice", "0");
    core.join(ALICE, "Alice", "");
    assert.equal(core.join(BOB, "Bob", ""), true);
    assert.deepEqual(world.state.seats.map(seat => seat.session), [ALICE, BOB]);
    const second = table();
    second.core.join(ALICE, "Alice", "1");
    second.core.join(BOB, "Bob");
    assert.deepEqual(second.world.state.seats.map(seat => seat.session), [BOB, ALICE]);
});

test("names are cosmetic: trimmed, stripped of control characters, capped, defaulted", () => {
    const { world, core } = table();
    core.join(ALICE, "  A\nlice\u0007 ".padEnd(60, "x"));
    const name = world.state.seats[0].name;
    assert.equal(name.length, NAME_LIMIT);
    assert.ok(name.startsWith("Alice"));
    core.join(BOB, "");
    assert.equal(world.state.seats[1].name, "Player bbbbbbbb");
});

test("moves alternate and a move out of turn is refused", () => {
    const { world, core } = seated();
    assert.equal(core.move(BOB, "3"), false);
    assert.deepEqual(world.event, { type: "rejected", to: BOB, method: "move", reason: "not your turn" });
    assert.equal(core.move(ALICE, "3"), true);
    assert.equal(world.state.toMove, 1);
    assert.equal(world.state.moveNo, 1);
    assert.deepEqual(world.state.last, { seat: 0, col: 3, row: 5 });
    assert.equal(world.state.rows[5], "...R...");
    assert.equal(core.move(ALICE, "4"), false);
    assert.equal(core.move(BOB, "3"), true);
    assert.equal(world.state.rows[4], "...B...");
});

test("malformed columns, full columns and unseated movers are refused", () => {
    const { world, core } = seated();
    for (const bad of ["7", "03", "-1", "", "x", "3.0"]) {
        assert.equal(core.move(ALICE, bad), false);
        assert.equal(world.event.reason, "column must be one digit 0-6");
    }
    playColumns(core, [0, 0, 0, 0, 0, 0]);
    assert.equal(core.move(ALICE, "0"), false);
    assert.equal(world.event.reason, "column full");
    assert.deepEqual(world.state.legal, [1, 2, 3, 4, 5, 6]);
    assert.equal(core.move(CAROL, "1"), false);
    assert.deepEqual(world.event, { type: "rejected", to: CAROL, method: "move", reason: "not seated" });
});

test("moves before the game starts or after it ends are refused", () => {
    const { world, core } = table();
    core.join(ALICE, "Alice");
    assert.equal(core.move(ALICE, "0"), false);
    assert.equal(world.event.reason, "game not started");
    core.join(BOB, "Bob");
    playColumns(core, [0, 1, 0, 1, 0, 1, 0]);
    assert.equal(core.move(BOB, "2"), false);
    assert.equal(world.event.reason, "game over");
});

test("four in a row ends the game and announces the winner with the line", () => {
    const { world, core } = seated();
    playColumns(core, [0, 1, 0, 1, 0, 1, 0]);
    const state = world.state;
    assert.equal(state.phase, "over");
    assert.equal(state.toMove, null);
    assert.deepEqual(state.legal, []);
    assert.equal(state.turnDeadline, null);
    assert.deepEqual(state.result, { winner: 0, reason: "four", line: [[2, 0], [3, 0], [4, 0], [5, 0]] });
    assert.deepEqual(world.event, {
        type: "over",
        gameId: state.gameId,
        result: state.result,
        seats: [{ session: ALICE, name: "Alice" }, { session: BOB, name: "Bob" }]
    });
    const view = world.rendered[world.rendered.length - 1];
    assert.deepEqual(view.line, [[2, 0], [3, 0], [4, 0], [5, 0]]);
    assert.match(view.text, /\nRed wins \(four in a row\); newGame to play again\n/);
});

test("a full board without four in a row is a draw", () => {
    const { world, core } = seated();
    // Ends with rows alternating RRBBRRB / BBRRBBR from the top, which holds no line of four
    playColumns(core, [2, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3,
        6, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6]);
    assert.equal(world.state.phase, "over");
    assert.deepEqual(world.state.result, { winner: null, reason: "draw", line: null });
    assert.equal(world.state.moveNo, 42);
    assert.match(world.rendered[world.rendered.length - 1].text, /\nDraw; newGame to play again\n/);
});

test("the mover forfeits when the turn clock runs out", () => {
    const { world, core } = seated({ turnTimeoutS: 60 });
    core.move(ALICE, "3");
    world.advance(59);
    core.tick();
    assert.equal(world.state.phase, "playing");
    assert.match(world.rendered[world.rendered.length - 1].text, /Black to move \(move 2, 5 s\)/);
    world.advance(1);
    core.tick();
    assert.equal(world.state.phase, "over");
    assert.deepEqual(world.state.result, { winner: 0, reason: "timeout", line: null });
    assert.equal(world.event.type, "over");
    assert.match(world.rendered[world.rendered.length - 1].text, /Red wins \(Black out of time\)/);
});

test("each move restarts the turn clock", () => {
    const { world, core } = seated({ turnTimeoutS: 60 });
    world.advance(50);
    core.move(ALICE, "3");
    world.advance(50);
    core.tick();
    assert.equal(world.state.phase, "playing");
    assert.equal(world.state.turnDeadline, T0 + 100_000 + 10_000);
});

test("the countdown redraws only when the shown seconds change", () => {
    const { world, core } = seated();
    const before = world.rendered.length;
    world.advance(1);
    core.tick();
    assert.equal(world.rendered.length, before);
    world.advance(4);
    core.tick();
    assert.equal(world.rendered.length, before + 1);
    assert.match(world.rendered[before].text, /move 1, 175 s/);
});

test("a lone seat expires after the wait timeout", () => {
    const { world, core } = table({ waitTimeoutS: 30 });
    core.join(ALICE, "Alice");
    world.advance(29);
    core.tick();
    assert.deepEqual(world.state.seats, [{ session: ALICE, name: "Alice" }, null]);
    world.advance(1);
    core.tick();
    assert.deepEqual(world.state.seats, [null, null]);
    assert.equal(world.state.phase, "waiting");
    assert.deepEqual(world.event, { type: "expired", seat: 0, session: ALICE, name: "Alice" });
});

test("an idle finished game resets to an empty table", () => {
    const { world, core } = seated();
    playColumns(core, [0, 1, 0, 1, 0, 1, 0]);
    world.advance(DEFAULT_TIMEOUTS.overIdleS);
    core.tick();
    assert.equal(world.state.phase, "waiting");
    assert.deepEqual(world.state.seats, [null, null]);
    assert.equal(world.state.result, null);
    assert.equal(world.state.gameId, null);
    assert.equal(world.state.rows[5], ".......");
});

test("leaving mid-game resigns, frees the seat and keeps the final board on show", () => {
    const { world, core } = seated();
    core.move(ALICE, "3");
    const published = world.published.length;
    const rendered = world.rendered.length;
    assert.equal(core.leave(BOB), true);
    assert.equal(world.published.length, published + 1, "one transition, one publication");
    assert.equal(world.rendered.length, rendered + 1);
    assert.deepEqual(world.event, {
        type: "over",
        gameId: "{table}:" + T0,
        result: { winner: 0, reason: "resign", line: null },
        seats: [{ session: ALICE, name: "Alice" }, { session: BOB, name: "Bob" }]
    });
    const state = world.state;
    assert.equal(state.phase, "waiting");
    assert.deepEqual(state.seats, [{ session: ALICE, name: "Alice" }, null]);
    assert.deepEqual(state.result, { winner: 0, reason: "resign", line: null });
    assert.equal(state.rows[5], "...R...");
    assert.match(world.rendered[world.rendered.length - 1].text, /Red wins \(Black resigned\)\. Waiting for an opponent/);

    // A new opponent starts a fresh game, and Black moves first in the second game
    core.join(CAROL, "Carol");
    assert.equal(world.state.phase, "playing");
    assert.equal(world.state.result, null);
    assert.equal(world.state.rows[5], ".......");
    assert.equal(world.state.toMove, 1);
});

test("leaving when not seated is refused; the last player leaving clears the table", () => {
    const { world, core } = table();
    assert.equal(core.leave(ALICE), false);
    assert.deepEqual(world.event, { type: "rejected", to: ALICE, method: "leave", reason: "not seated" });
    core.join(ALICE, "Alice");
    assert.equal(core.leave(ALICE), true);
    assert.deepEqual(world.state.seats, [null, null]);
});

test("leaving a finished game frees the seat for a new opponent", () => {
    const { world, core } = seated();
    playColumns(core, [0, 1, 0, 1, 0, 1, 0]);
    core.leave(ALICE);
    assert.equal(world.state.phase, "waiting");
    assert.deepEqual(world.state.seats, [null, { session: BOB, name: "Bob" }]);
    assert.equal(core.newGame(BOB), false);
    assert.equal(world.event.reason, "game not over");
    core.join(CAROL, "Carol");
    assert.deepEqual(world.state.seats[0], { session: CAROL, name: "Carol" });
    assert.equal(world.state.phase, "playing");
});

test("newGame resets the board, keeps the seats and swaps the first mover", () => {
    const { world, core } = seated();
    assert.equal(core.newGame(ALICE), false);
    assert.equal(world.event.reason, "game not over");
    playColumns(core, [0, 1, 0, 1, 0, 1, 0]);
    const firstId = world.state.gameId;
    assert.equal(core.newGame(CAROL), false);
    assert.equal(world.event.reason, "not seated");
    world.advance(1);
    assert.equal(core.newGame(BOB), true);
    const state = world.state;
    assert.equal(state.phase, "playing");
    assert.deepEqual(state.seats, [{ session: ALICE, name: "Alice" }, { session: BOB, name: "Bob" }]);
    assert.equal(state.toMove, 1);
    assert.equal(state.moveNo, 0);
    assert.equal(state.result, null);
    assert.notEqual(state.gameId, firstId);
    assert.ok(state.rows.every(row => row === "......."));
    assert.equal(core.move(ALICE, "0"), false);
    assert.equal(core.move(BOB, "0"), true);
});

test("state sends the projection on the channel and stores nothing", () => {
    const { world, core } = seated();
    const count = world.published.length;
    assert.equal(core.state(), true);
    assert.equal(core.state(), true);
    assert.equal(world.published.length, count);
    assert.deepEqual(world.event, { type: "state", ...world.state });
});

test("a move after the deadline but before the next tick is refused", () => {
    const { world, core } = seated({ turnTimeoutS: 60 });
    world.advance(60);
    assert.equal(core.move(ALICE, "3"), false);
    assert.equal(world.event.reason, "game over");
    assert.deepEqual(world.state.result, { winner: 1, reason: "timeout", line: null });
    assert.equal(world.state.rows[5], ".......");
});

test("a join after a lone seat expired, before the next tick, takes a fresh table", () => {
    const { world, core } = table({ waitTimeoutS: 30 });
    core.join(ALICE, "Alice");
    world.advance(30);
    assert.equal(core.join(BOB, "Bob"), true);
    assert.deepEqual(world.state.seats, [{ session: BOB, name: "Bob" }, null]);
    assert.equal(world.state.phase, "waiting");
    assert.ok(world.announced.some(event => event.type === "expired" && event.session === ALICE));
});

test("an emptied table starts the next game with Red", () => {
    const { world, core } = seated();
    playColumns(core, [0, 1, 0, 1, 0, 1, 0]);
    // Black would move first in a rematch
    core.leave(ALICE);
    core.leave(BOB);
    core.join(BOB, "Bob");
    core.join(CAROL, "Carol");
    assert.equal(world.state.toMove, 0);
    assert.equal(world.state.seats[0].session, BOB);
});

test("stop abandons a game in progress and publishes the table offline", () => {
    const { world, core } = seated();
    core.move(ALICE, "3");
    core.stop();
    const state = world.state;
    assert.equal(state.phase, "offline");
    assert.equal(state.toMove, null);
    assert.equal(state.turnDeadline, null);
    assert.deepEqual(state.legal, []);
    assert.deepEqual(world.event.result, { winner: null, reason: "abandoned", line: null });

    // A reload that finds it offline has nothing to report
    const next = fakeWorld();
    new RefereeCore(next, {}).start(state);
    assert.deepEqual(next.announced, []);
});

test("stop with no game in progress announces nothing", () => {
    const { world, core } = table();
    const announced = world.announced.length;
    core.stop();
    assert.equal(world.state.phase, "offline");
    assert.equal(world.announced.length, announced);
});

test("the projection is a copy: mutating it does not reach the referee", () => {
    const { world, core } = seated();
    core.move(ALICE, "3");
    world.state.rows[5] = "RRRRRRR";
    world.state.seats[1].session = ALICE;
    world.state.last.col = 0;
    assert.equal(core.projection().rows[5], "...R...");
    assert.equal(core.move(ALICE, "4"), false);
    assert.equal(core.move(BOB, "4"), true);
});

test("seats follow the caller id, never the arguments: spoofing is impossible", () => {
    const { world, core } = seated();
    // Carol names Alice in every argument; the referee only knows Carol's own id
    assert.equal(core.move(CAROL, ALICE), false);
    assert.equal(core.move(CAROL, "3"), false);
    assert.equal(world.event.reason, "not seated");
    assert.equal(core.leave(CAROL), false);
    assert.equal(core.join(CAROL, ALICE), false);
    assert.equal(core.newGame(CAROL), false);
    assert.deepEqual(world.state.seats, [{ session: ALICE, name: "Alice" }, { session: BOB, name: "Bob" }]);
    assert.equal(world.state.moveNo, 0);
});

test("calls without an authenticated caller are refused", () => {
    const { world, core } = table();
    for (const caller of [NULL_UUID, "00000000-0000-0000-0000-000000000000", "", undefined, null]) {
        assert.equal(core.join(/** @type {any} */ (caller), "Ghost"), false);
        assert.equal(world.event.reason, "no caller id");
    }
    assert.deepEqual(world.state.seats, [null, null]);
});

test("timeouts out of range fall back to the defaults", () => {
    const world = fakeWorld();
    const core = new RefereeCore(world, { turnTimeoutS: 0, waitTimeoutS: "30", overIdleS: 1e9 });
    assert.equal(core.turnTimeoutMs, DEFAULT_TIMEOUTS.turnTimeoutS * 1000);
    assert.equal(core.waitTimeoutMs, DEFAULT_TIMEOUTS.waitTimeoutS * 1000);
    assert.equal(core.overIdleMs, DEFAULT_TIMEOUTS.overIdleS * 1000);
});
