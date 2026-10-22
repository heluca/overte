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
        /** @type {{ path: string, headers: Record<string, string> }[]} */
        posts: [],
        /** @type {Record<string, { status: number, body: any } | Error>} POST path -> reply; 200 {} otherwise */
        postReplies: {},
        async fetch(url, init = {}) {
            const path = url.startsWith(BASE) ? url.slice(BASE.length) : url;
            const respond = (status, body) => ({
                ok: status >= 200 && status <= 299, status,
                json: async () => structuredClone(body),
                text: async () => JSON.stringify(body)
            });
            if (init.method === "POST") {
                engine.posts.push({ path, headers: init.headers });
                const reply = engine.postReplies[path];
                if (reply instanceof Error) {
                    throw reply;
                }
                return reply ? respond(reply.status, reply.body) : respond(200, {});
            }
            engine.fetched.push(url);
            const body = engine.routes[path];
            if (body instanceof Error) {
                throw body;
            }
            return body === undefined ? respond(404, {}) : respond(200, body);
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

test("the remotely callable methods: the reads, the request and the transport", () => {
    assert.deepEqual(new PianoEntity().remotelyCallable,
        ["status", "list", "request", "play", "pause", "stop", "next", "previous", "replay", "select"]);
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
    assert.deepEqual(JSON.parse(engine.clientCalls[0].params[1]),
        [{ id: 1, title: "Ode to Joy", composer: "Beethoven", duration: 16 }], "and the songs with their ids, for a picker");
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

const MANIFEST_URL = "https://media.example/salon-grand/1.0.0/manifest.json";
const { MANIFEST } = await import("./salon-grand-manifest.mjs");

/**
 * A remote call as the engine makes it: the caller stamped for the synchronous part only.
 * @param {any} engine
 * @param {any} piano
 * @param {string} method
 * @param {string[]} [params]
 * @param {string} [caller]
 */
function call(engine, piano, method, params = [], caller = CALLER) {
    engine.Script.remoteCallerID = caller;
    const result = piano[method](PIANO, params);
    engine.Script.remoteCallerID = undefined;
    return result;
}

/** @param {any} engine @returns {any[]} the showResult answers, parsed */
function results(engine) {
    return engine.clientCalls.filter(c => c.method === "showResult").map(c => ({ to: c.session, ...JSON.parse(c.params[0]) }));
}

test("request appends the song on the synth for the caller's session and answers with its place", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    engine.postReplies["/api/playback/request/1?output=synth"] = { status: 200, body: { queue_id: 41, position: 1 } };
    const result = await call(engine, piano, "request", ["1"]);
    assert.deepEqual(result, { action: "request", ok: true, songId: 1, title: undefined, position: 1 });
    assert.deepEqual(engine.posts, [{ path: "/api/playback/request/1?output=synth",
        headers: { "X-Orpheus-Request": "1", "X-Overte-Session": CALLER } }]);
    assert.deepEqual(results(engine).map(r => [r.to, r.ok, r.position]), [[CALLER, true, 1]]);
    assert.ok(engine.printed.some(line => line.includes("request 1 from " + CALLER + " done")));
    assert.ok(!JSON.stringify(engine.entity).toLowerCase().includes("bearer"), "no credential in the entity");
});

test("a second request inside the cool-down is refused with its reason, without reaching Orpheus", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    engine.postReplies["/api/playback/request/1?output=synth"] = { status: 200, body: { queue_id: 41, position: 0 } };
    await call(engine, piano, "request", ["1"]);
    engine.advance(4000);
    const second = await call(engine, piano, "request", ["1"]);
    assert.equal(second.ok, false);
    assert.equal(second.reason, "One request every 10 s; try again in 6 s");
    assert.equal(engine.posts.length, 1);
    assert.equal(results(engine)[1].reason, second.reason);
});

test("one waiting request per session until the status poll shows it has started", async () => {
    const engine = fakeEngine({ piano: { orpheusBase: BASE, volume: 0.5, requestCooldownMs: 0 } });
    const piano = await loaded(engine);
    engine.postReplies["/api/playback/request/1?output=synth"] = { status: 200, body: { queue_id: 41, position: 2 } };
    engine.routes["/api/playback/status"] = { ...PLAYING, queue: [{ id: 40 }, { id: 41 }] };
    await call(engine, piano, "request", ["1"]);
    engine.advance(1000);
    await engine.run(1000);
    const refused = await call(engine, piano, "request", ["1"]);
    assert.match(refused.reason, /already have a song waiting/);
    engine.routes["/api/playback/status"] = { ...PLAYING, queue: [] };
    engine.advance(1000);
    await engine.run(1000);
    assert.equal((await call(engine, piano, "request", ["1"])).ok, true);
});

test("Orpheus's refusal reaches the caller with its reason; a bad id never leaves the piano", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    engine.postReplies["/api/playback/request/1?output=synth"] = { status: 429,
        body: { detail: "3 in-world requests are already waiting (the cap is 3); try again when one has started" } };
    const refused = await call(engine, piano, "request", ["1"]);
    assert.equal(refused.reason, "3 in-world requests are already waiting (the cap is 3); try again when one has started");
    assert.equal(piano.limits.waiting.size, 0, "the slot is free again");
    const bad = await call(engine, piano, "request", ["../../system/reboot"]);
    assert.equal(bad.reason, "Request a song by its library id");
    assert.equal(engine.posts.length, 1);
});

test("transport routes: stop, next, previous, replay and select on the synth only", async () => {
    const engine = fakeEngine({ piano: { orpheusBase: BASE, volume: 0.5, transportCalls: 10 } });
    const piano = await loaded(engine);
    engine.postReplies["/api/playback/next"] = { status: 200, body: { skipped: true } };
    for (const [method, params] of [["stop", []], ["next", []], ["previous", []], ["replay", []], ["select", ["7"]]]) {
        assert.equal((await call(engine, piano, method, params)).ok, true, method);
    }
    assert.deepEqual(engine.posts.map(post => post.path), ["/api/playback/stop", "/api/playback/next",
        "/api/playback/previous?output=synth", "/api/playback/replay?output=synth",
        "/api/playback/play-song/7?output=synth&play_now=true"]);
    assert.ok(engine.posts.every(post => post.headers["X-Overte-Session"] === CALLER));
    engine.postReplies["/api/playback/next"] = { status: 200, body: { skipped: false } };
    assert.equal((await call(engine, piano, "next")).reason, "Nothing is queued to skip to");
    assert.equal((await call(engine, piano, "select", ["x"])).reason, "Select a song by its library id");
});

test("play and pause each go one way, though Orpheus's pause toggles", async () => {
    const engine = fakeEngine({ piano: { orpheusBase: BASE, volume: 0.5, transportCalls: 10 } });
    const piano = await loaded(engine);
    assert.equal((await call(engine, piano, "pause")).reason, "Nothing is playing");
    assert.equal((await call(engine, piano, "play")).ok, true);
    assert.deepEqual(engine.posts.map(post => post.path), ["/api/playback/replay?output=synth"], "play from idle replays");
    engine.routes["/api/playback/status"] = PLAYING;
    assert.equal((await call(engine, piano, "play")).reason, "Already playing");
    assert.equal((await call(engine, piano, "pause")).ok, true);
    engine.routes["/api/playback/status"] = { ...PLAYING, is_paused: true };
    assert.equal((await call(engine, piano, "pause")).reason, "Already paused");
    assert.equal((await call(engine, piano, "play")).ok, true);
    assert.deepEqual(engine.posts.map(post => post.path).slice(1), ["/api/playback/pause", "/api/playback/pause"]);
});

test("a burst of transport calls is rate limited per session, with the reason", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    const answers = [];
    for (let i = 0; i < 7; i++) {
        answers.push(await call(engine, piano, "stop"));
    }
    assert.deepEqual(answers.map(answer => answer.ok), [true, true, true, true, true, false, false]);
    assert.equal(answers[5].reason, "Too many controls: at most 5 every 30 s; try again in 30 s");
    assert.equal(engine.posts.length, 5);
    const other = "{cccccccc-0000-4000-8000-000000000003}";
    assert.equal((await call(engine, piano, "stop", [], other)).ok, true, "another visitor is not held back");
    engine.advance(30_000);
    assert.equal((await call(engine, piano, "stop")).ok, true);
});

test("a call that changes playback polls the status at once", async () => {
    const engine = fakeEngine();
    const piano = await loaded(engine);
    const before = engine.fetched.filter(url => url.endsWith("/api/playback/status")).length;
    await call(engine, piano, "stop");
    await settle();
    assert.equal(engine.fetched.filter(url => url.endsWith("/api/playback/status")).length, before + 1);
    assert.equal(engine.pending(5000).length, 1, "and still one poll scheduled");
});

test("limits from userData; a bad value is logged and replaced", async () => {
    const engine = fakeEngine({ piano: { orpheusBase: BASE, requestQueueCap: 0, transportCalls: "lots" } });
    const piano = await loaded(engine);
    assert.ok(engine.printed.some(line => /piano.transportCalls must be an integer/.test(line)));
    assert.equal((await call(engine, piano, "request", ["1"])).reason, "Song requests are turned off on this piano");
});

test("an unconfigured piano refuses calls with a reason", async () => {
    const engine = fakeEngine({});
    const piano = await loaded(engine);
    assert.equal((await call(engine, piano, "stop")).reason, "This piano is not connected to Orpheus");
    assert.equal(engine.posts.length, 0);
});

test("the lid follows the status: joints from the manifest, written at once, then tweened", async () => {
    const engine = fakeEngine({ piano: { orpheusBase: BASE, volume: 0.5, manifestURL: MANIFEST_URL } });
    engine.routes[MANIFEST_URL] = MANIFEST;
    const piano = await loaded(engine);
    assert.ok(engine.fetched.includes(MANIFEST_URL));
    const lid = () => engine.entity.jointRotations[100];
    assert.deepEqual(engine.entity.jointRotationsSet.flatMap((set, i) => set ? [i] : []), [9, 100, 101]);
    assert.deepEqual(lid(), { x: 0, y: 0, z: 0, w: 1 }, "idle: closed, written at once");
    engine.routes["/api/playback/status"] = PLAYING;
    await engine.run(5000);
    assert.equal(engine.pending(100).length, 1, "a tween runs");
    const edits = engine.edits;
    for (let i = 0; i < 40 && engine.pending(100).length; i++) {
        engine.advance(100);
        await engine.run(100);
    }
    assert.equal(engine.pending(100).length, 0, "and stops at the pose");
    assert.ok(engine.edits - edits >= 20 && engine.edits - edits <= 30, "about ten edits a second for 2.5 s");
    assert.ok(Math.abs(lid().z - Math.sin(20 * Math.PI / 180)) < 1e-9, "the lid at 40 degrees");
    assert.equal(piano.pose?.lid_prop, 103.3876);
});

test("offline shuts the fallboard; a missing manifest or URL leaves the lid alone and says why", async () => {
    const engine = fakeEngine({ piano: { orpheusBase: BASE, volume: 0.5, manifestURL: MANIFEST_URL } });
    engine.routes[MANIFEST_URL] = MANIFEST;
    engine.routes["/api/playback/status"] = new Error("connection refused");
    await loaded(engine);
    assert.ok(Math.abs(engine.entity.jointRotations[9].x - Math.SQRT1_2) < 1e-9, "fallboard at 90 degrees");

    const none = fakeEngine();
    await loaded(none);
    assert.ok(none.printed.some(line => /the lid stays put, piano.manifestURL must be/.test(line)));
    assert.equal(none.entity.jointRotations, undefined);

    const failing = fakeEngine({ piano: { orpheusBase: BASE, manifestURL: MANIFEST_URL } });
    failing.routes[MANIFEST_URL] = new Error("HTTP 404");
    const piano = await loaded(failing);
    assert.ok(failing.printed.some(line => /manifest failed, retrying in 60 s/.test(line)));
    assert.equal(failing.pending(60_000).length, 1);
    piano.unload();
    assert.equal(failing.timers.size, 0);
});

test("a locked piano does not move its lid", async () => {
    const engine = fakeEngine({ piano: { orpheusBase: BASE, manifestURL: MANIFEST_URL } });
    engine.entity.locked = true;
    engine.routes[MANIFEST_URL] = MANIFEST;
    await loaded(engine);
    assert.ok(!engine.fetched.includes(MANIFEST_URL));
    assert.equal(engine.edits, 0);
});
