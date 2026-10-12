//
//  piano-entity.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The serverScripts entry against fake Entities, Audio, Script and fetch globals: the stream it
//  plays, the status it mirrors and the answers it sends back.
//

import { test } from "node:test";
import assert from "node:assert/strict";

const PIANO = "{9a40d000-0000-4000-8000-000000000000}";
const CALLER = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BASE = "http://orpheus.example:8080";

const IDLE = { is_playing: false, is_paused: false, status: "idle", title: null, output: null, progress: 0, last_error: null };
const PLAYING = { ...IDLE, is_playing: true, status: "playing", title: "Ode to Joy", output: "synth", progress: 0.5 };
const SYNTH = { enabled: true, active_voices: 2, listeners: 1 };
const SONGS = [{ id: 1, title: "Ode to Joy", composer: "Beethoven", genre: null, duration_seconds: 16, created_at: "" }];

function fakeEngine(userData = { piano: { orpheusBase: BASE, volume: 0.5 } }) {
    let nextTimer = 1;
    let now = 1_000_000;
    const engine = {
        entity: { userData: JSON.stringify({ owner: "kept", ...userData }), position: { x: 1, y: 2, z: 3 }, locked: false },
        edits: 0,
        printed: [],
        injectors: [],
        clientCalls: [],
        timers: new Map(),
        /** @type {Record<string, any>} path -> JSON body, or an Error to fail with */
        routes: { "/api/playback/status": IDLE, "/api/synth/status": SYNTH, "/api/songs": SONGS },
        fetched: [],
        Entities: {
            getEntityProperties(id, names) {
                assert.equal(id, PIANO);
                return Object.fromEntries(names.map(name => [name, engine.entity[name]]));
            },
            editEntity(id, properties) {
                assert.equal(id, PIANO);
                engine.edits++;
                Object.assign(engine.entity, properties);
                return id;
            },
            callEntityClientMethod(session, id, method, params) {
                engine.clientCalls.push({ session, id, method, params });
            }
        },
        Audio: {
            playStream(url, options) {
                const handlers = [];
                const injector = {
                    url, options, stopped: false,
                    finished: { connect(handler) { handlers.push(handler); } },
                    finish() { handlers.forEach(handler => handler()); },
                    setOptions(next) { injector.options = next; },
                    stop() { injector.stopped = true; injector.finish(); }
                };
                engine.injectors.push(injector);
                return injector;
            }
        },
        Script: {
            remoteCallerID: undefined,
            setTimeout(callback, ms) { engine.timers.set(nextTimer, { callback, ms, repeat: false }); return nextTimer++; },
            setInterval(callback, ms) { engine.timers.set(nextTimer, { callback, ms, repeat: true }); return nextTimer++; },
            clearTimeout(id) { engine.timers.delete(id); },
            clearInterval(id) { engine.timers.delete(id); }
        },
        print(text) { engine.printed.push(text); },
        async fetch(url) {
            engine.fetched.push(url);
            const body = engine.routes[url.slice(BASE.length)];
            if (body instanceof Error) {
                throw body;
            }
            return body === undefined
                ? { ok: false, status: 404, json: async () => ({}) }
                : { ok: true, status: 200, json: async () => structuredClone(body) };
        },
        advance(ms) { now += ms; },
        /** The pending timers whose delay is `ms`, as the engine would run them. */
        pending(ms) {
            return [...engine.timers].filter(([, timer]) => timer.ms === ms);
        },
        async run(ms) {
            for (const [id, timer] of engine.pending(ms)) {
                if (!timer.repeat) {
                    engine.timers.delete(id);
                }
                await timer.callback();
            }
            await settle();
        },
        status() { return JSON.parse(engine.entity.userData).piano.status; }
    };
    Object.assign(globalThis, {
        Entities: engine.Entities, Audio: engine.Audio, Script: engine.Script, print: engine.print, fetch: engine.fetch
    });
    Date.now = () => now;
    return engine;
}

async function settle() {
    for (let i = 0; i < 10; i++) {
        await new Promise(resolve => setImmediate(resolve));
    }
}

const { default: PianoEntity } = await import("../piano-entity.mjs");

async function loaded(engine) {
    const piano = new PianoEntity();
    piano.preload(PIANO);
    await settle();
    return piano;
}

test("the remotely callable methods are read-only", () => {
    assert.deepEqual(new PianoEntity().remotelyCallable, ["status", "list"]);
});

test("without a usable orpheusBase it logs once and does nothing", async () => {
    for (const userData of [{}, { piano: { orpheusBase: "http://<host>:<port>" } }]) {
        const engine = fakeEngine(userData);
        const piano = await loaded(engine);
        assert.equal(engine.printed.length, 1);
        assert.match(engine.printed[0], /not starting/);
        assert.equal(engine.injectors.length, 0);
        assert.equal(engine.timers.size, 0);
        assert.equal(engine.fetched.length, 0);
        piano.unload();
    }
});

test("preload plays the PCM stream at the entity and mirrors the status into userData", async () => {
    const engine = fakeEngine();
    await loaded(engine);
    assert.equal(engine.injectors.length, 1);
    assert.equal(engine.injectors[0].url, BASE + "/api/synth/stream.pcm");
    assert.deepEqual(engine.injectors[0].options, { position: { x: 1, y: 2, z: 3 }, volume: 0.5 });
    const userData = JSON.parse(engine.entity.userData);
    assert.equal(userData.owner, "kept");
    assert.equal(userData.piano.orpheusBase, BASE);
    assert.equal(userData.piano.status.state, "idle");
    assert.equal(engine.pending(5000).length, 1, "idle polls every 5 s");
});

test("the status is written only when it changes, and polled every second while playing", async () => {
    const engine = fakeEngine();
    await loaded(engine);
    assert.equal(engine.edits, 1);
    await engine.run(5000);
    assert.equal(engine.edits, 1, "an unchanged status is not written again");
    engine.routes["/api/playback/status"] = PLAYING;
    await engine.run(5000);
    assert.equal(engine.edits, 2);
    assert.equal(engine.status().title, "Ode to Joy");
    assert.equal(engine.pending(1000).length, 2, "the position poll and the status poll");
    engine.routes["/api/playback/status"] = new Error("connection refused");
    await engine.run(1000);
    assert.deepEqual([engine.status().state, engine.status().error], ["offline", "Error: connection refused"]);
    assert.ok(engine.printed.some(line => /offline/.test(line)));
});

test("a locked piano still plays but leaves userData alone", async () => {
    const engine = fakeEngine();
    engine.entity.locked = true;
    const piano = await loaded(engine);
    assert.equal(engine.edits, 0);
    assert.equal(piano.lastStatus?.state, "idle");
    assert.equal(engine.injectors.length, 1);
});

test("the injector follows the entity when it moves", async () => {
    const engine = fakeEngine();
    await loaded(engine);
    const injector = engine.injectors[0];
    const before = injector.options;
    await engine.run(1000);
    assert.equal(injector.options, before, "no setOptions while the entity stays put");
    engine.entity.position = { x: 4, y: 2, z: 3 };
    await engine.run(1000);
    assert.deepEqual(injector.options, { position: { x: 4, y: 2, z: 3 }, volume: 0.5 });
});

test("a finished stream reconnects with backoff, reset after a long connection", async () => {
    const engine = fakeEngine();
    await loaded(engine);
    const delays = [];
    for (let i = 0; i < 7; i++) {
        engine.injectors.at(-1).finish();
        const [[id, timer]] = [...engine.timers].filter(([, t]) => !t.repeat && t.ms !== 5000);
        delays.push(timer.ms);
        engine.timers.delete(id);
        timer.callback();
    }
    assert.deepEqual(delays, [1000, 2000, 4000, 8000, 16000, 30000, 30000]);
    assert.equal(engine.injectors.length, 8);
    engine.advance(61_000);
    engine.injectors.at(-1).finish();
    assert.equal(engine.pending(1000).filter(([, t]) => !t.repeat).length, 1, "back to 1 s");
});

test("unload stops the injector, clears every timer and does not reconnect", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    engine.injectors[0].finish();
    piano.unload();
    assert.equal(engine.timers.size, 0);
    const second = await loaded(engine);
    second.unload();
    assert.equal(engine.injectors.at(-1).stopped, true);
    assert.equal(engine.timers.size, 0);
});

test("status() answers the caller with the last status", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    engine.Script.remoteCallerID = CALLER;
    const status = piano.status();
    engine.Script.remoteCallerID = undefined;
    assert.equal(status?.state, "idle");
    assert.deepEqual(engine.clientCalls, [{ session: CALLER, id: PIANO, method: "showStatus", params: [JSON.stringify(status)] }]);
});

test("list() answers the caller read before the await, and caches /api/songs for a minute", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    const call = () => {
        engine.Script.remoteCallerID = CALLER;
        const result = piano.list();
        engine.Script.remoteCallerID = undefined;
        return result;
    };
    const [first, second] = await Promise.all([call(), call()]);
    assert.deepEqual(first, SONGS);
    assert.equal(second, first);
    const songFetches = () => engine.fetched.filter(url => url.endsWith("/api/songs")).length;
    assert.equal(songFetches(), 1, "concurrent calls share one request");
    assert.deepEqual(engine.clientCalls.map(call => [call.session, call.method, call.params[0]]),
        [[CALLER, "showSongs", "Songs (1)\nOde to Joy - Beethoven (0:16)"], [CALLER, "showSongs", "Songs (1)\nOde to Joy - Beethoven (0:16)"]]);
    engine.advance(59_000);
    await call();
    assert.equal(songFetches(), 1);
    engine.advance(2_000);
    await call();
    assert.equal(songFetches(), 2);
});

test("list() without a caller ID answers nobody, and a failed request says so", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    assert.deepEqual(await piano.list(), SONGS);
    assert.equal(engine.clientCalls.length, 0);
    engine.routes["/api/songs"] = new Error("HTTP 401");
    engine.advance(61_000);
    engine.Script.remoteCallerID = CALLER;
    const pending = piano.list();
    engine.Script.remoteCallerID = undefined;
    assert.equal(await pending, null);
    assert.equal(engine.clientCalls[0].params[0], "Songs\n(Orpheus did not answer)");
});
