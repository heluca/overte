//
//  piano-client.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The client entity script against fake Entities and Script globals: the local label it draws,
//  what it asks the piano for, the song picker a click on the score opens, and the desk screen
//  it opens for the bench. It must never edit the piano.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const PIANO = "{9a40d000-0000-4000-8000-000000000000}";
const PLAYING = { state: "playing", title: "Ode to Joy", output: "synth", progress: 50, elapsed: 8, synth: true, error: null };
const DISPLAY_URL = "https://assets.example/piano/piano-display.html";
const MANIFEST_URL = "https://assets.example/props/manifest.json";
const DESK_JOINT = 107;
const SCORE_MESH = 105;
const SONGS = [
    { id: 1, title: "Gymnopedie No. 1", composer: "Erik Satie", duration: 180 },
    { id: 2, title: "Ode to Joy", composer: null, duration: 16 }
];
const CLICK = { isPrimaryButton: true, pos3D: { x: 0, y: 1.15, z: 0.8 }, direction: { x: 0, y: 0, z: -1 } };

function fakeEngine(status = null, { displayURL = DISPLAY_URL, manifestURL = MANIFEST_URL, joints = { anchor_desk_screen: DESK_JOINT } } = {}) {
    let nextId = 1;
    let now = 1_000_000;
    const piano = status ? { orpheusBase: "http://orpheus.example", status } : {};
    if (displayURL !== null) {
        piano.displayURL = displayURL;
    }
    if (manifestURL !== null) {
        piano.manifestURL = manifestURL;
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
        /** what the precise pick hits: the mesh index, or null for nothing */
        hitMesh: SCORE_MESH,
        picks: [],
        fetched: [],
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
            emitScriptEvent(id, message) { engine.scriptEvents.push({ id, message }); },
            findRayIntersection(ray, filter, include) {
                engine.picks.push({ ray, filter, include });
                return engine.hitMesh === null ? { intersects: false }
                    : { intersects: true, intersection: { x: 0, y: 1.15, z: 0.79 }, extraInfo: { subMeshIndex: engine.hitMesh } };
            }
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
        },
        /** a button on the page with this Web entity */
        press(id, event) {
            engine.webEvent(id, JSON.stringify({ ...event, type: "piano-display" }));
        },
        picker() {
            return [...entities.entries()].find(([, entity]) => entity.name === "Piano.Picker");
        },
        /** the last view sent to a page */
        view(id) {
            const sent = engine.scriptEvents.filter(event => event.id === id);
            return sent.length ? JSON.parse(sent.at(-1).message) : null;
        }
    };
    const fetch = async url => {
        engine.fetched.push(url);
        return { ok: true, status: 200, json: async () => ({ joints: { score: { mesh_index: SCORE_MESH } } }) };
    };
    const Picks = { PICK_DOMAIN_ENTITIES: 1, PICK_AVATAR_ENTITIES: 2, PICK_PRECISE: 4 };
    Object.assign(globalThis, { Entities: engine.Entities, Script: engine.Script, Picks, fetch,
        print: text => engine.printed.push(String(text)) });
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

/** Preloaded, with the manifest read. */
async function ready(engine) {
    const client = loaded(engine);
    await new Promise(resolve => setImmediate(resolve));
    return client;
}

/** Opens the picker with a click on the score and says the page is ready. */
async function withPicker(engine) {
    const client = await ready(engine);
    client.clickDownOnEntity(PIANO, /** @type {any} */ (CLICK));
    const [pickerID] = engine.picker();
    engine.press(pickerID, { action: "ready" });
    return { client, pickerID };
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

test("without a displayURL a click asks for the song list, shown on the label for a few seconds", () => {
    const engine = fakeEngine(PLAYING, { displayURL: null });
    const client = loaded(engine);
    client.clickDownOnEntity(PIANO, /** @type {any} */ ({ ...CLICK, isPrimaryButton: false }));
    assert.deepEqual(engine.serverCalls, []);
    client.clickDownOnEntity(PIANO, /** @type {any} */ (CLICK));
    assert.deepEqual(engine.serverCalls, [{ id: PIANO, method: "list", params: [] }]);
    client.showSongs(PIANO, ["Songs (1)\nOde to Joy", JSON.stringify(SONGS)]);
    engine.advance(7000);
    assert.equal(engine.label().text, "Songs (1)\nOde to Joy");
    engine.advance(2000);
    assert.equal(engine.label().text, "Ode to Joy\nplaying  0:08  50%");
    assert.equal(engine.picker(), undefined);
});

test("a click on the score opens the song picker in front of it, facing the visitor, and asks for the songs", async () => {
    const engine = fakeEngine(PLAYING);
    const client = await ready(engine);
    assert.deepEqual(engine.fetched, [MANIFEST_URL]);
    client.clickDownOnEntity(PIANO, /** @type {any} */ (CLICK));
    const [pickerID, picker] = engine.picker();
    assert.equal(picker.hostType, "local");
    assert.equal(picker.type, "Web");
    assert.equal(picker.sourceUrl, DISPLAY_URL + "?view=picker");
    assert.equal(picker.wantsKeyboardFocus, false);
    assertNear(picker.position, { x: 0, y: 1.15 + 0.05 + 0.24, z: 0.79 + 0.3 });
    assertNear(picker.rotation, { x: 0, y: 0, z: 0, w: 1 });
    assertNear(picker.dimensions, { x: 0.48, y: 0.48 });
    assert.equal(Math.round(0.48 * 39.3701 * picker.dpi), 960);
    // The precise pick starts just short of the click's hit, on the piano only
    const [pick] = engine.picks;
    assertNear(pick.ray.origin, { x: 0, y: 1.15, z: 0.85 });
    assert.deepEqual(pick.include, [PIANO]);
    assert.equal(pick.filter & 4, 4, "a precise pick, so it reports the mesh");
    assert.deepEqual(engine.serverCalls, [{ id: PIANO, method: "list", params: [] }]);
    assert.equal(engine.view(pickerID), null, "nothing is sent before the page says it is ready");
    engine.press(pickerID, { action: "ready" });
    assert.deepEqual(engine.view(pickerID), { type: "piano-view", kind: "picker", songs: { state: "loading" }, notice: null });
    client.showSongs(PIANO, ["Songs (2)", JSON.stringify(SONGS)]);
    const view = engine.view(pickerID);
    assert.equal(view.songs.state, "ready");
    assert.deepEqual(view.songs.items.map(song => [song.id, song.title, song.length]),
        [[1, "Gymnopedie No. 1", "3:00"], [2, "Ode to Joy", "0:16"]]);
    assert.notEqual(engine.label().text, "Songs (2)", "the list goes to the picker, not the label");
    assert.match(engine.printed.join("\n"), /song picker opened from the score, mesh 105/);
});

test("a click elsewhere on the piano opens no picker", async () => {
    const engine = fakeEngine(PLAYING);
    const client = await ready(engine);
    engine.hitMesh = 97;    // the lid
    client.clickDownOnEntity(PIANO, /** @type {any} */ (CLICK));
    engine.hitMesh = null;
    client.clickDownOnEntity(PIANO, /** @type {any} */ (CLICK));
    assert.equal(engine.picker(), undefined);
    assert.deepEqual(engine.serverCalls, []);
});

test("without a manifest a click anywhere on the piano opens the picker", async () => {
    const engine = fakeEngine(PLAYING, { manifestURL: null });
    const client = await ready(engine);
    assert.deepEqual(engine.fetched, []);
    assert.match(engine.printed.join("\n"), /a click anywhere opens the song picker, piano.manifestURL must be/);
    engine.hitMesh = 3;
    client.clickDownOnEntity(PIANO, /** @type {any} */ (CLICK));
    assert.ok(engine.picker());
});

test("picking a song requests it and the picker shows its place in the queue, or why it was refused", async () => {
    const engine = fakeEngine(PLAYING);
    const { client, pickerID } = await withPicker(engine);
    client.showSongs(PIANO, ["Songs (2)", JSON.stringify(SONGS)]);
    engine.press(pickerID, { action: "request", songId: 1 });
    assert.deepEqual(engine.serverCalls.at(-1), { id: PIANO, method: "request", params: ["1"] });
    assert.deepEqual(engine.view(pickerID).notice, { text: "Asking for Gymnopedie No. 1...", tone: "info" });
    client.showResult(PIANO, [JSON.stringify({ action: "request", ok: true, songId: 1, title: "Gymnopedie No. 1", position: 1 })]);
    assert.deepEqual(engine.view(pickerID).notice, { text: "Gymnopedie No. 1: plays next", tone: "ok" });
    engine.advance(3000);
    engine.press(pickerID, { action: "request", songId: 2 });
    client.showResult(PIANO, [JSON.stringify({ action: "request", ok: false, songId: 2, title: "Ode to Joy",
        reason: "One request every 10 s; try again in 7 s" })]);
    assert.deepEqual(engine.view(pickerID).notice, { text: "Ode to Joy: One request every 10 s; try again in 7 s", tone: "refused" });
    assert.match(engine.printed.join("\n"), /request Ode to Joy refused: One request every 10 s/);
    assert.equal(engine.label().text, "Ode to Joy\nplaying  0:08  50%", "the label keeps the status");
    engine.advance(9000);
    assert.equal(engine.view(pickerID).notice, null, "the notice fades");
});

test("the picker pages through the songs, closes from its button, and closes when left alone", async () => {
    const engine = fakeEngine(PLAYING);
    const { client, pickerID } = await withPicker(engine);
    const many = Array.from({ length: 12 }, (_, i) => ({ id: i + 1, title: "Song " + (i + 1), composer: null, duration: null }));
    client.showSongs(PIANO, ["Songs (12)", JSON.stringify(many)]);
    engine.press(pickerID, { action: "page", page: 2 });
    assert.deepEqual(engine.view(pickerID).songs.items.map(song => song.id), [11, 12]);
    engine.press(pickerID, { action: "close" });
    assert.equal(engine.picker(), undefined);
    assert.equal(engine.webHandlers.size, 0);

    client.clickDownOnEntity(PIANO, /** @type {any} */ (CLICK));
    assert.ok(engine.picker());
    assert.equal(engine.serverCalls.filter(call => call.method === "list").length, 1, "a fresh list is not asked for again");
    engine.advance(29_000);
    assert.ok(engine.picker());
    engine.advance(1000);
    assert.equal(engine.picker(), undefined, "30 s without a touch");
});

test("sitting down closes the picker and opens the desk screen", async () => {
    const engine = fakeEngine(PLAYING);
    const { client } = await withPicker(engine);
    client.openDisplay();
    assert.equal(engine.picker(), undefined);
    assert.ok(engine.display());
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
    assert.equal(display.sourceUrl, DISPLAY_URL + "?view=desk");
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

test("the desk screen shows the status and queue, and its buttons light to show playing or paused", () => {
    const engine = fakeEngine({ ...PLAYING, duration: 16, queue: ["Gymnopedie No. 1"], queued: 1 });
    const client = loaded(engine);
    client.openDisplay();
    const [displayID] = engine.display();
    engine.press(displayID, { action: "ready" });
    let view = engine.view(displayID);
    assert.equal(view.kind, "desk");
    assert.equal(view.title, "Ode to Joy");
    assert.deepEqual([view.progress, view.elapsed, view.duration], [50, "0:08", "0:16"]);
    assert.deepEqual(view.queue, { items: ["Gymnopedie No. 1"], more: 0 });
    assert.deepEqual([view.buttons.play.lit, view.buttons.pause.lit], [true, false]);
    engine.publish({ ...PLAYING, state: "paused", duration: 16, queue: [], queued: 0 });
    engine.advance(1000);
    view = engine.view(displayID);
    assert.deepEqual([view.buttons.play.lit, view.buttons.pause.lit, view.buttons.next.dim], [false, true, true]);
    const sent = engine.scriptEvents.length;
    engine.advance(1000);
    assert.equal(engine.scriptEvents.length, sent, "an unchanged view is not sent again");
});

test("the desk's transport buttons call the piano, and a refusal shows its reason", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.openDisplay();
    const [displayID] = engine.display();
    engine.press(displayID, { action: "ready" });
    for (const action of ["previous", "play", "pause", "stop", "next", "replay"]) {
        engine.press(displayID, { action });
    }
    assert.deepEqual(engine.serverCalls.map(call => call.method), ["previous", "play", "pause", "stop", "next", "replay"]);
    assert.deepEqual(engine.view(displayID).notice, { text: "replay...", tone: "info" });
    client.showResult(PIANO, [JSON.stringify({ action: "pause", ok: true })]);
    assert.deepEqual(engine.view(displayID).notice, { text: "Paused", tone: "ok" });
    client.showResult(PIANO, [JSON.stringify({ action: "next", ok: false,
        reason: "Too many controls: at most 5 every 30 s; try again in 29 s" })]);
    assert.deepEqual(engine.view(displayID).notice,
        { text: "Next: Too many controls: at most 5 every 30 s; try again in 29 s", tone: "refused" });
    engine.webEvent("{someone-elses-web-entity}", JSON.stringify({ type: "piano-display", action: "stop" }));
    engine.webEvent(displayID, "rubbish");
    assert.equal(engine.serverCalls.length, 6);
    assert.match(engine.printed.join("\n"), /ignored screen event "rubbish"/);
});

test("the desk's Songs tab asks for the list and requests from it", () => {
    const engine = fakeEngine(PLAYING);
    const client = loaded(engine);
    client.openDisplay();
    const [displayID] = engine.display();
    engine.press(displayID, { action: "ready" });
    assert.equal(engine.view(displayID).songs, null);
    engine.press(displayID, { action: "tab", tab: "songs" });
    assert.deepEqual(engine.serverCalls, [{ id: PIANO, method: "list", params: [] }]);
    assert.deepEqual(engine.view(displayID).songs, { state: "loading" });
    client.showSongs(PIANO, ["Songs (2)", JSON.stringify(SONGS)]);
    assert.equal(engine.view(displayID).songs.items.length, 2);
    engine.press(displayID, { action: "request", songId: 2 });
    assert.deepEqual(engine.serverCalls.at(-1), { id: PIANO, method: "request", params: ["2"] });
    client.showResult(PIANO, [JSON.stringify({ action: "request", ok: true, songId: 2, title: "Ode to Joy", position: 0 })]);
    assert.deepEqual(engine.view(displayID).notice, { text: "Ode to Joy: playing now", tone: "ok" });
    engine.press(displayID, { action: "tab", tab: "queue" });
    assert.equal(engine.view(displayID).tab, "queue");
});

test("no displayURL or no desk joint: no display, the label stays, the log says why", () => {
    let engine = fakeEngine(PLAYING, { displayURL: null });
    let client = loaded(engine);
    assert.match(engine.printed.join("\n"), /no desk display or song picker, piano.displayURL must be an http/);
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
