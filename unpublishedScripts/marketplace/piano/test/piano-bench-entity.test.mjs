//
//  piano-bench-entity.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The bench's serverScripts against fake Entities and Script globals: who gets the seat, and
//  the answers sent back to the caller's client script.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const BENCH = "{b0b0b000-0000-4000-8000-000000000000}";
const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";

function fakeEngine(userData = {}) {
    let now = 1_000_000;
    const engine = {
        clientCalls: [],
        printed: [],
        Entities: {
            getEntityProperties(id, names) {
                assert.equal(id, BENCH);
                return Object.fromEntries(names.map(name => [name, name === "userData" ? JSON.stringify(userData) : undefined]));
            },
            callEntityClientMethod(session, id, method, params) { engine.clientCalls.push({ session, id, method, params }); }
        },
        Script: { remoteCallerID: undefined },
        advance(ms) { now += ms; },
        /** Calls a remotely callable method as the engine does: with the caller stamped */
        as(caller, entity, method) {
            engine.Script.remoteCallerID = caller;
            try {
                return entity[method](BENCH, []);
            } finally {
                engine.Script.remoteCallerID = undefined;
            }
        }
    };
    Object.assign(globalThis, { Entities: engine.Entities, Script: engine.Script, print: text => engine.printed.push(String(text)) });
    Date.now = () => now;
    return engine;
}

const { default: PianoBenchEntity } = await import("../piano-bench-entity.mjs");

function loaded() {
    const bench = new PianoBenchEntity();
    bench.preload(BENCH);
    return bench;
}

test("only claim and release are remotely callable", () => {
    fakeEngine();
    assert.deepEqual(loaded().remotelyCallable, ["claim", "release"]);
});

test("one sitter: the first claim is granted, a second caller is refused until release", () => {
    const engine = fakeEngine();
    const bench = loaded();
    assert.equal(engine.as(ALICE, bench, "claim"), true);
    assert.equal(engine.as(BOB, bench, "claim"), false);
    assert.equal(engine.as(BOB, bench, "release"), false, "Bob cannot free Alice's seat");
    assert.equal(engine.as(ALICE, bench, "release"), true);
    assert.equal(engine.as(BOB, bench, "claim"), true);
    assert.deepEqual(engine.clientCalls, [
        { session: ALICE, id: BENCH, method: "seatGranted", params: [] },
        { session: BOB, id: BENCH, method: "seatRefused", params: [] },
        { session: BOB, id: BENCH, method: "seatGranted", params: [] }
    ]);
});

test("a sitter who stops renewing loses the seat after leaseMs", () => {
    const engine = fakeEngine({ pianoBench: { leaseMs: 6000 } });
    const bench = loaded();
    engine.as(ALICE, bench, "claim");
    engine.advance(4000);
    engine.as(ALICE, bench, "claim");
    engine.advance(5999);
    assert.equal(engine.as(BOB, bench, "claim"), false, "renewed at 4 s");
    engine.advance(1);
    assert.equal(engine.as(BOB, bench, "claim"), true);
    assert.equal(engine.as(ALICE, bench, "claim"), false, "Alice's next renewal is refused, so her client stands up");
});

test("a call with no caller is ignored; a bad config is logged", () => {
    const engine = fakeEngine({ pianoBench: { leaseMs: -1 } });
    const bench = loaded();
    assert.match(engine.printed.join("\n"), /pianoBench\.leaseMs must be a number/);
    assert.equal(engine.as("{00000000-0000-0000-0000-000000000000}", bench, "claim"), false);
    assert.equal(engine.as(undefined, bench, "claim"), false);
    assert.deepEqual(engine.clientCalls, []);
});
