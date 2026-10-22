//
//  piano-bench-entity.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The bench's classic serverScripts, evaluated the way the entity script server does it (the
//  file is one function expression, constructed with `new`) in a Node vm context that holds only
//  fake Entities, Script, print and Date: who gets the seat, and the answers sent back.
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const SOURCE = readFileSync(new URL("../piano-bench-entity.js", import.meta.url), "utf8");
const BENCH = "{b0b0b000-0000-4000-8000-000000000000}";
const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";

/** @param {object | string} userData an object, or raw userData text */
function loaded(userData = {}) {
    let now = 1_000_000;
    const engine = {
        clientCalls: [],
        printed: [],
        Script: { remoteCallerID: undefined },
        advance(ms) { now += ms; },
        /** Calls a remotely callable method as the engine does: with the caller stamped */
        as(caller, method) {
            engine.Script.remoteCallerID = caller;
            try {
                return engine.bench[method](BENCH, []);
            } finally {
                engine.Script.remoteCallerID = undefined;
            }
        }
    };
    const context = vm.createContext({
        Entities: {
            getEntityProperties(id, names) {
                assert.equal(id, BENCH);
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
    const constructor = vm.runInContext(SOURCE, context, { filename: "piano-bench-entity.js" });
    engine.bench = new constructor();
    engine.bench.preload(BENCH);
    return engine;
}

test("only claim and release are remotely callable", () => {
    assert.deepEqual([...loaded().bench.remotelyCallable], ["claim", "release"]);
});

test("one sitter: the first claim is granted, a second caller is refused until release", () => {
    const engine = loaded();
    assert.equal(engine.as(ALICE, "claim"), true);
    assert.equal(engine.as(BOB, "claim"), false);
    assert.equal(engine.as(BOB, "release"), false, "Bob cannot free Alice's seat");
    assert.equal(engine.as(ALICE, "claim"), true, "a renewal");
    assert.equal(engine.as(ALICE, "release"), true);
    assert.equal(engine.as(BOB, "claim"), true);
    assert.deepEqual(engine.clientCalls, [
        { session: ALICE, id: BENCH, method: "seatGranted", params: ["15000"] },
        { session: BOB, id: BENCH, method: "seatRefused", params: ["15000"] },
        { session: ALICE, id: BENCH, method: "seatGranted", params: ["15000"] },
        { session: BOB, id: BENCH, method: "seatGranted", params: ["15000"] }
    ]);
});

test("a sitter who stops renewing loses the seat after leaseMs, and is refused after", () => {
    const engine = loaded({ pianoBench: { leaseMs: 6000 } });
    engine.as(ALICE, "claim");
    engine.advance(4000);
    engine.as(ALICE, "claim");
    engine.advance(5999);
    assert.equal(engine.as(BOB, "claim"), false, "renewed at 4 s");
    engine.advance(1);
    assert.equal(engine.as(BOB, "claim"), true);
    assert.equal(engine.as(ALICE, "claim"), false, "Alice's next renewal is refused, so her client stands up");
    assert.deepEqual(engine.clientCalls[0].params, ["6000"], "the grant tells the client the lease");
});

test("a bad leaseMs is logged and replaced; none, or unparseable userData, is the default", () => {
    for (const leaseMs of [-1, 1000, "long", 200_000]) {
        const engine = loaded({ pianoBench: { leaseMs } });
        assert.match(engine.printed.join("\n"), /pianoBench\.leaseMs must be a number from 3000 to 120000, .*; using 15000/);
        assert.equal(engine.bench.leaseMs, 15000);
    }
    for (const userData of [{}, "", "not json", { pianoBench: null }]) {
        const engine = loaded(userData);
        assert.deepEqual(engine.printed, []);
        assert.equal(engine.bench.leaseMs, 15000);
    }
});

test("a call with no caller is ignored", () => {
    const engine = loaded();
    assert.equal(engine.as("{00000000-0000-0000-0000-000000000000}", "claim"), false);
    assert.equal(engine.as(undefined, "claim"), false);
    assert.equal(engine.as(undefined, "release"), false);
    assert.deepEqual(engine.clientCalls, []);
});

test("the file is a classic script with no module syntax or fork-only globals", () => {
    assert.doesNotMatch(SOURCE, /^\s*(import|export)\b/m);
    assert.doesNotMatch(SOURCE, /\bimport\s*\(|import\.meta|\bfetch\s*\(|structuredClone|\bawait\b|\basync\b/);
});
