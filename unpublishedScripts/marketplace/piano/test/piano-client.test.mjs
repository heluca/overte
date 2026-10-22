//
//  piano-client.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The client entity script against fake Entities and Script globals: the local label it draws,
//  what it asks the piano for, and the desk display it opens for the bench. It must never edit
//  the piano.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const PIANO = "{9a40d000-0000-4000-8000-000000000000}";
const PLAYING = { state: "playing", title: "Ode to Joy", output: "synth", progress: 50, elapsed: 8, synth: true, error: null };
const DISPLAY_URL = "https://assets.example/piano/piano-display.html";
const DESK_JOINT = 107;

function fakeEngine(status = null, { displayURL = DISPLAY_URL, joints = { anchor_desk_screen: DESK_JOINT } } = {}) {
    let nextId = 1;
    let now = 1_000_000;
    const piano = status ? { orpheusBase: "http://orpheus.example", status } : {};
    if (displayURL !== null) {
        piano.displayURL = displayURL;
    }
    const entities = new Map([[PIANO, {
        dimensions: { x: 1.48, y: 1.304, z: 2.11 },
        userData: JSON.stringify({ piano })
    }]]);
    const webHandlers = new Set();
    const engine = {
        entities,
        serverCalls: [],
        scriptEvents: [],
        printed: [],
        webHandlers,
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
            callEntityServerMethod(id, method, params) { engine.serverCalls.push({ id, method, params }); },
            getJointIndex(id, name) {
                assert.equal(id, PIANO);
                return name in joints ? joints[name] : -1;
            },
            webEventReceived: {
                connect(handler) { webHandlers.add(handler); },
                disconnect(handler) { webHandlers.delete(handler); }
            },
            emitScriptEvent(id, message) { engine.scriptEvents.push({ id, message }); }
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
        },
        display() {
            return [...entities.entries()].find(([, entity]) => entity.name === "Piano.Display");
        },
        webEvent(id, message) {
            webHandlers.forEach(handler => handler(id, message));
        }
    };
    Object.assign(globalThis, { Entities: engine.Entities, Script: engine.Script, print: text => engine.printed.push(String(text)) });
    Date.now = () => now;
    return engine;
}

const { default: PianoClient } = await import("../piano-client.mjs");

function assertNear(actual, expected) {
    for (const key of Object.keys(expected)) {
        assert.ok(Math.abs(actual[key] - expected[key]) < 1e-9, key + ": " + actual[key] + " != " + expected[key]);
    }
}

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
    assert.ok(label.localPosition.y > 0.65);
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

test("a call's result shows for a few seconds, a refusal with its reason, and is logged", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    assert.deepEqual(client.remotelyCallable, ["showStatus", "showSongs", "showResult"]);
    client.showResult(PIANO, [JSON.stringify({ action: "request", ok: false, songId: 3, title: "Ode to Joy",
        reason: "One request every 10 s; try again in 4 s" })]);
    assert.equal(engine.label().text, "Piano: request Ode to Joy refused\nOne request every 10 s; try again in 4 s");
    assert.match(engine.printed.at(-1), /request Ode to Joy refused: One request every 10 s/);
    engine.advance(7000);
    client.showResult(PIANO, [JSON.stringify({ action: "request", ok: true, songId: 3, position: 2 })]);
    assert.equal(engine.label().text, "Requested: song 3\nnumber 2 in the queue");
    engine.advance(9000);
    assert.equal(engine.label().text, "Ode to Joy\nplaying  0:08  50%");
    client.showResult(PIANO, ["not json"]);
    client.showResult(PIANO, ["{}"]);
    assert.equal(engine.label().text, "Ode to Joy\nplaying  0:08  50%", "a malformed result is ignored");
});

test("unload removes the label and the refresh timer", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.unload();
    assert.equal(engine.label(), undefined);
    assert.equal(engine.timers.size, 0);
});

test("openDisplay lays a local Web page on the desk anchor and hides the label; closeDisplay undoes both", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.openDisplay();
    client.openDisplay();
    const displays = [...engine.entities.values()].filter(entity => entity.name === "Piano.Display");
    assert.equal(displays.length, 1, "opened once");
    const [, display] = engine.display();
    assert.equal(display.hostType, "local");
    assert.equal(display.type, "Web");
    assert.equal(display.sourceUrl, DISPLAY_URL);
    assert.equal(display.parentID, PIANO);
    assert.equal(display.parentJointIndex, DESK_JOINT);
    assertNear(display.localPosition, { x: 0, y: 0.0275, z: 0.013 });
    assert.deepEqual(display.localRotation, { x: 0, y: 0, z: 0, w: 1 });
    assertNear(display.dimensions, { x: 0.80, y: 0.275 });
    assert.equal(Math.round(display.dimensions.x * 39.3701 * display.dpi), 1600);
    assert.equal(display.wantsKeyboardFocus, false, "the movement keys must still reach the avatar");
    assert.equal(engine.label().visible, false);
    client.closeDisplay();
    assert.equal(engine.display(), undefined);
    assert.equal(engine.label().visible, true);
    assert.equal(engine.webHandlers.size, 0);
});

test("display buttons arrive as web events: printed and acknowledged; others' events ignored", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.openDisplay();
    const [displayID] = engine.display();
    engine.webEvent(displayID, JSON.stringify({ type: "piano-display", action: "next" }));
    engine.webEvent(displayID, JSON.stringify({ type: "piano-display", action: "play-song", songId: 3 }));
    engine.webEvent("{someone-elses-web-entity}", JSON.stringify({ type: "piano-display", action: "stop" }));
    engine.webEvent(displayID, "rubbish");
    assert.deepEqual(engine.scriptEvents.map(event => [event.id, JSON.parse(event.message)]), [
        [displayID, { type: "piano-display-ack", action: "next", songId: null }],
        [displayID, { type: "piano-display-ack", action: "play-song", songId: 3 }]
    ]);
    const log = engine.printed.join("\n");
    assert.match(log, /display next/);
    assert.match(log, /display play-song 3/);
    assert.match(log, /ignored display event "rubbish"/);
    assert.deepEqual(engine.serverCalls, [], "nothing reaches Orpheus yet");
});

test("no displayURL or no desk joint: no display, the label stays, the log says why", () => {
    let engine = fakeEngine(PLAYING, { displayURL: null });
    let client = loaded(engine);
    assert.match(engine.printed.join("\n"), /no desk display, piano.displayURL must be an http/);
    client.openDisplay();
    assert.equal(engine.display(), undefined);
    assert.notEqual(engine.label().visible, false);

    engine = fakeEngine(PLAYING, { displayURL: "https://<asset-host>/piano/piano-display.html" });
    loaded(engine).openDisplay();
    assert.equal(engine.display(), undefined, "a README placeholder is not a URL");

    engine = fakeEngine(PLAYING, { joints: {} });
    client = loaded(engine);
    client.openDisplay();
    assert.equal(engine.display(), undefined);
    assert.match(engine.printed.join("\n"), /no anchor_desk_screen joint/);
});

test("unload closes an open display", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.openDisplay();
    client.unload();
    assert.equal(engine.display(), undefined);
    assert.equal(engine.webHandlers.size, 0);
});

test("a change to piano.display re-tunes the open display within a second; bad values log once", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.openDisplay();
    const [displayID, display] = engine.display();
    const setDisplay = value => {
        const userData = JSON.parse(engine.entities.get(PIANO).userData);
        userData.piano.display = value;
        engine.entities.get(PIANO).userData = JSON.stringify(userData);
    };
    setDisplay({ width: 0.7, height: 0.25, offset: { x: 0, y: 0.03, z: 0.015 } });
    engine.advance(1000);
    assertNear(display.dimensions, { x: 0.7, y: 0.25 });
    assertNear(display.localPosition, { x: 0, y: 0.03, z: 0.015 });
    assert.equal(Math.round(0.7 * 39.3701 * display.dpi), 1600);
    assert.equal(engine.display()[0], displayID, "edited in place, not recreated");

    setDisplay({ width: 9 });
    engine.advance(1000);
    engine.advance(1000);
    assertNear(display.dimensions, { x: 0.80, y: 0.275 });
    assert.equal(engine.printed.filter(line => /piano\.display\.width must be/.test(line)).length, 1);

    client.closeDisplay();
    setDisplay({ width: 0.5 });
    engine.advance(1000);
    client.openDisplay();
    assertNear(engine.display()[1].dimensions, { x: 0.5, y: 0.275 }, "a new display takes the current values");
});
