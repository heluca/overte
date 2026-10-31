//
//  seat-server.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The seat's classic serverScripts, evaluated the way the entity script server does it (the
//  file is one function expression, constructed with `new`) in a Node vm context that holds only
//  fake Entities, Script, print and Date: who gets which seat, and the answers sent back.
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const SOURCE = readFileSync(new URL("../seat-server.js", import.meta.url), "utf8");
const MODEL = "{5ea75ea7-0000-4000-8000-000000000000}";
const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";
const CAROL = "{cccccccc-0000-4000-8000-000000000003}";
const ALL = ["seat_1", "seat_2", "seat_3"];

/** @param {object | string} userData an object, or raw userData text */
function loaded(userData = {}) {
    let now = 1_000_000;
    const engine = {
        clientCalls: [],
        printed: [],
        Script: { remoteCallerID: undefined },
        advance(ms) { now += ms; },
        /** Calls a remotely callable method as the engine does: with the caller stamped */
        as(caller, method, params = []) {
            engine.Script.remoteCallerID = caller;
            try {
                return engine.seat[method](MODEL, params);
            } finally {
                engine.Script.remoteCallerID = undefined;
            }
        },
        /** The answers sent since the last look */
        replies() {
            return engine.clientCalls.splice(0).map(call => {
                assert.equal(call.id, MODEL);
                return [call.session, call.method, ...call.params];
            });
        }
    };
    const context = vm.createContext({
        Entities: {
            getEntityProperties(id, names) {
                assert.equal(id, MODEL);
                assert.deepEqual([...names], ["userData"]);
                return { userData: typeof userData === "string" ? userData : JSON.stringify(userData) };
            },
            callEntityClientMethod(session, id, method, params) {
                engine.clientCalls.push({ session, id, method, params: [...params] });
            }
        },
        Script: engine.Script,
        print: text => engine.printed.push(String(text)),
        Date: { now: () => now }
    });
    // ScriptManager evaluates the file, then constructs the script object from the result
    const constructor = vm.runInContext(SOURCE, context, { filename: "seat-server.js" });
    engine.seat = new constructor();
    engine.seat.preload(MODEL);
    return engine;
}

test("only claim and release are remotely callable", () => {
    assert.deepEqual([...loaded().seat.remotelyCallable], ["claim", "release"]);
});

test("each caller gets the first free seat in its own order of preference", () => {
    const engine = loaded();
    assert.equal(engine.as(ALICE, "claim", ["seat_3", "seat_2", "seat_1"]), true);
    assert.equal(engine.as(BOB, "claim", ["seat_3", "seat_2", "seat_1"]), true);
    assert.equal(engine.as(CAROL, "claim", ["seat_2", "seat_3", "seat_1"]), true);
    assert.deepEqual(engine.replies(), [
        [ALICE, "seatGranted", "seat_3", "15000"],
        [BOB, "seatGranted", "seat_2", "15000"],
        [CAROL, "seatGranted", "seat_1", "15000"]
    ]);
});

test("a holder's claim renews its own seat, whatever it asks for", () => {
    const engine = loaded();
    engine.as(ALICE, "claim", ["seat_1"]);
    engine.as(BOB, "claim", ["seat_2"]);
    engine.replies();
    assert.equal(engine.as(ALICE, "claim", ["seat_3"]), true);
    assert.equal(engine.as(ALICE, "claim", []), true);
    assert.equal(engine.as(ALICE, "claim", ["seat_2"]), true);
    assert.deepEqual(engine.replies(), [
        [ALICE, "seatGranted", "seat_1", "15000"],
        [ALICE, "seatGranted", "seat_1", "15000"],
        [ALICE, "seatGranted", "seat_1", "15000"]
    ]);
    assert.equal(engine.as(CAROL, "claim", ["seat_3"]), true, "seat_3 was not taken by Alice's asking");
});

test("release frees only the caller's seat", () => {
    const engine = loaded();
    engine.as(ALICE, "claim", ["seat_1"]);
    engine.as(BOB, "claim", ["seat_2"]);
    assert.equal(engine.as(CAROL, "release"), false, "Carol holds nothing");
    assert.equal(engine.as(BOB, "release"), true);
    assert.equal(engine.as(BOB, "release"), false, "already released");
    engine.replies();
    engine.as(CAROL, "claim", ["seat_1", "seat_2"]);
    assert.deepEqual(engine.replies(), [[CAROL, "seatGranted", "seat_2", "15000"]], "Alice still holds seat_1");
    assert.equal(engine.as(ALICE, "release", ["seat_2"]), true);
    engine.as(BOB, "claim", ["seat_2", "seat_1"]);
    assert.deepEqual(engine.replies(), [[BOB, "seatGranted", "seat_1", "15000"]], "Carol's seat_2 is untouched");
});

test("a lease that runs out is taken by another caller; the old holder is refused after", () => {
    const engine = loaded({ seat: { leaseMs: 6000 } });
    engine.as(ALICE, "claim", ["seat_1"]);
    engine.advance(4000);
    engine.as(ALICE, "claim", ["seat_1"]);
    engine.advance(5999);
    assert.equal(engine.as(BOB, "claim", ["seat_1"]), false, "renewed at 4 s");
    engine.advance(1);
    assert.equal(engine.as(BOB, "claim", ["seat_1"]), true);
    assert.equal(engine.as(ALICE, "claim", ["seat_1"]), false, "Alice's next renewal is refused, so her client stands up");
    assert.equal(engine.as(ALICE, "release"), false, "her lease ran out: she holds nothing");
    assert.deepEqual(engine.replies(), [
        [ALICE, "seatGranted", "seat_1", "6000"],
        [ALICE, "seatGranted", "seat_1", "6000"],
        [BOB, "seatRefused", "taken"],
        [BOB, "seatGranted", "seat_1", "6000"],
        [ALICE, "seatRefused", "taken"]
    ]);
});

test("a caller whose lease ran out claims afresh, and holds one seat only", () => {
    const engine = loaded();
    engine.as(ALICE, "claim", ["seat_1"]);
    engine.advance(15000);
    engine.as(ALICE, "claim", ["seat_2"]);
    engine.replies();
    engine.as(BOB, "claim", ["seat_1"]);
    assert.deepEqual(engine.replies(), [[BOB, "seatGranted", "seat_1", "15000"]]);
});

test("every named seat taken: refused as taken; no seat named: refused as no-seat", () => {
    const engine = loaded();
    engine.as(ALICE, "claim", ["seat_1"]);
    engine.as(BOB, "claim", ["seat_2"]);
    engine.replies();
    assert.equal(engine.as(CAROL, "claim", ["seat_1", "seat_2"]), false);
    assert.equal(engine.as(CAROL, "claim", []), false);
    assert.equal(engine.as(CAROL, "claim", undefined), false);
    assert.equal(engine.as(CAROL, "claim", "seat_3"), false, "not a list");
    assert.deepEqual(engine.replies(), [
        [CAROL, "seatRefused", "taken"],
        [CAROL, "seatRefused", "no-seat"],
        [CAROL, "seatRefused", "no-seat"],
        [CAROL, "seatRefused", "no-seat"]
    ]);
});

test("anything that is not a seat_N name is ignored, and only the first 64 entries are read", () => {
    const engine = loaded();
    const junk = ["seat_0", "seat_01", "seat_", "Seat_1", "stand_1", "seat_1 ", "x", 3, null, { name: "seat_1" }];
    assert.equal(engine.as(ALICE, "claim", junk), false);
    assert.equal(engine.as(ALICE, "claim", [...junk, "seat_12"]), true);
    const long = [...Array(64).fill("nope"), "seat_1"];
    assert.equal(engine.as(BOB, "claim", long), false);
    assert.deepEqual(engine.replies(), [
        [ALICE, "seatRefused", "no-seat"],
        [ALICE, "seatGranted", "seat_12", "15000"],
        [BOB, "seatRefused", "no-seat"]
    ]);
});

test("a bad leaseMs is logged and replaced; none, or unparseable userData, is the default", () => {
    for (const leaseMs of [-1, 1000, "long", 200_000]) {
        const engine = loaded({ seat: { leaseMs } });
        assert.match(engine.printed.join("\n"),
            /^Seat \{5ea75ea7-[^}]+\}: seat\.leaseMs must be a number from 3000 to 120000, got .*; using 15000$/);
        assert.equal(engine.seat.leaseMs, 15000);
    }
    for (const userData of [{}, "", "not json", { seat: null }, { seat: { hipsAboveSeat: 0.1 } }]) {
        const engine = loaded(userData);
        assert.deepEqual(engine.printed, []);
        assert.equal(engine.seat.leaseMs, 15000);
    }
    assert.equal(loaded({ seat: { leaseMs: 30000 } }).seat.leaseMs, 30000);
});

test("a call with no caller is ignored", () => {
    const engine = loaded();
    assert.equal(engine.as("{00000000-0000-0000-0000-000000000000}", "claim", ALL), false);
    assert.equal(engine.as(undefined, "claim", ALL), false);
    assert.equal(engine.as(null, "claim", ALL), false);
    assert.equal(engine.as(undefined, "release"), false);
    assert.deepEqual(engine.clientCalls, []);
    assert.equal(engine.as(ALICE, "claim", ["seat_1"]), true, "and took no seat");
});

test("nothing is written to the entity", () => {
    const engine = loaded();
    engine.as(ALICE, "claim", ALL);
    engine.as(ALICE, "release");
    assert.doesNotMatch(SOURCE, /editEntity|setEntityProperties|\.userData\s*=/);
});

test("the file is a classic script with no module syntax or fork-only globals", () => {
    assert.doesNotMatch(SOURCE, /^\s*(import|export)\b/m);
    assert.doesNotMatch(SOURCE, /\bimport\s*\(|import\.meta|\bfetch\s*\(|structuredClone|\bawait\b|\basync\b/);
    assert.doesNotMatch(SOURCE, /^\s*(let|const|class)\s/m);
    assert.doesNotMatch(SOURCE, /=>/);
});
