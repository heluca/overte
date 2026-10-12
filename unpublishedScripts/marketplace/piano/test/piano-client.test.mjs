//
//  piano-client.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The client entity script against fake Entities and Script globals: the local label it draws
//  and what it asks the piano for. It must never edit the piano.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const PIANO = "{9a40d000-0000-4000-8000-000000000000}";
const PLAYING = { state: "playing", title: "Ode to Joy", output: "synth", progress: 50, elapsed: 8, synth: true, error: null };

function fakeEngine(status = null) {
    let nextId = 1;
    let now = 1_000_000;
    const entities = new Map([[PIANO, {
        dimensions: { x: 1.5, y: 1, z: 0.6 },
        userData: JSON.stringify({ piano: status ? { orpheusBase: "http://orpheus.example", status } : {} })
    }]]);
    const engine = {
        entities,
        serverCalls: [],
        timers: new Map(),
        Entities: {
            addEntity(properties, hostType) {
                const id = `{00000000-0000-4000-8000-${String(nextId++).padStart(12, "0")}}`;
                entities.set(id, { ...properties, hostType: hostType || "domain" });
                return id;
            },
            editEntity(id, properties) {
                assert.equal(entities.get(id)?.hostType, "local", "editEntity on a domain entity " + id);
                Object.assign(entities.get(id), properties);
                return id;
            },
            deleteEntity(id) { entities.delete(id); },
            getEntityProperties(id, names) {
                const entity = entities.get(id) || {};
                return Object.fromEntries(names.map(name => [name, entity[name]]));
            },
            callEntityServerMethod(id, method, params) { engine.serverCalls.push({ id, method, params }); }
        },
        Script: {
            setInterval(callback, ms) { engine.timers.set(nextId, { callback, ms }); return nextId++; },
            clearInterval(id) { engine.timers.delete(id); }
        },
        advance(ms) {
            now += ms;
            engine.timers.forEach(timer => timer.callback());
        },
        publish(next) {
            const userData = JSON.parse(entities.get(PIANO).userData);
            userData.piano.status = next;
            entities.get(PIANO).userData = JSON.stringify(userData);
        },
        label() {
            return [...entities.values()].find(entity => entity.name === "Piano.Status");
        }
    };
    Object.assign(globalThis, { Entities: engine.Entities, Script: engine.Script });
    Date.now = () => now;
    return engine;
}

const { default: PianoClient } = await import("../piano-client.mjs");

function loaded(engine) {
    const client = new PianoClient();
    client.preload(PIANO);
    return client;
}

test("preload floats a local label above the piano showing the published status", () => {
    const engine = fakeEngine(PLAYING);
    loaded(engine);
    const label = engine.label();
    assert.equal(label.hostType, "local");
    assert.equal(label.parentID, PIANO);
    assert.ok(label.localPosition.y > 0.5);
    assert.equal(label.text, "Ode to Joy\nplaying  0:08  50%");
    assert.deepEqual(engine.serverCalls, [], "a published status needs no status() call");
});

test("the label follows userData changes", () => {
    const engine = fakeEngine(PLAYING);
    loaded(engine);
    engine.publish({ ...PLAYING, state: "idle", title: null, progress: null, elapsed: null });
    engine.advance(1000);
    assert.equal(engine.label().text, "Piano\nidle");
});

test("without a published status it asks the piano once and shows the answer", () => {
    const engine = fakeEngine();
    const client = loaded(engine);
    assert.deepEqual(engine.serverCalls, [{ id: PIANO, method: "status", params: [] }]);
    assert.equal(engine.label().text, "Piano\nwaiting for Orpheus");
    client.showStatus(PIANO, [JSON.stringify(PLAYING)]);
    assert.equal(engine.label().text, "Ode to Joy\nplaying  0:08  50%");
    client.showStatus(PIANO, ["null"]);
    assert.equal(engine.label().text, "Piano\nwaiting for Orpheus");
});

test("a click asks for the song list, shown for a few seconds before the status returns", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.clickDownOnEntity(PIANO, /** @type {any} */ ({ isPrimaryButton: false }));
    assert.deepEqual(engine.serverCalls, []);
    client.clickDownOnEntity(PIANO, /** @type {any} */ ({ isPrimaryButton: true }));
    assert.deepEqual(engine.serverCalls, [{ id: PIANO, method: "list", params: [] }]);
    client.showSongs(PIANO, ["Songs (1)\nOde to Joy"]);
    engine.advance(7000);
    assert.equal(engine.label().text, "Songs (1)\nOde to Joy");
    engine.advance(2000);
    assert.equal(engine.label().text, "Ode to Joy\nplaying  0:08  50%");
});

test("unload removes the label and the refresh timer", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.unload();
    assert.equal(engine.label(), undefined);
    assert.equal(engine.timers.size, 0);
});
