//
//  piano-bridge-core.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The per-session limits on song requests and transport, their refusal reasons, and what a
//  visitor is shown.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import {
    LIMIT_DEFAULTS, SessionLimits, parseLimits, songIdOf, refusalReason, formatResult, songEntries
} from "../piano-bridge-core.mjs";

const A = "{aaaaaaaa-0000-4000-8000-000000000001}";
const B = "{bbbbbbbb-0000-4000-8000-000000000002}";

test("parseLimits: defaults, overrides, and out-of-range values reported and replaced", () => {
    assert.deepEqual(parseLimits({}), { limits: LIMIT_DEFAULTS, problems: [] });
    assert.deepEqual(LIMIT_DEFAULTS, { requestCooldownMs: 10_000, requestQueueCap: 5, transportCalls: 5, transportWindowMs: 30_000 });
    const { limits, problems } = parseLimits({ requestCooldownMs: 20_000, requestQueueCap: 0, transportCalls: 1.5,
        transportWindowMs: 10 });
    assert.deepEqual(limits, { ...LIMIT_DEFAULTS, requestCooldownMs: 20_000, requestQueueCap: 0 });
    assert.equal(problems.length, 2);
    assert.match(problems[0], /piano.transportCalls must be an integer from 1 to 100, got 1.5; using 5/);
    assert.match(problems[1], /piano.transportWindowMs/);
});

test("one waiting request per session, held while the first is in flight", () => {
    const limits = new SessionLimits({ ...LIMIT_DEFAULTS, requestCooldownMs: 0 });
    assert.equal(limits.admitRequest(A, 0), null);
    assert.match(limits.admitRequest(A, 1) || "", /already have a song waiting/, "refused while the first is in flight");
    limits.requested(A, 41, 2, 2);
    assert.match(limits.admitRequest(A, 3) || "", /already have a song waiting/);
    assert.equal(limits.admitRequest(B, 3), null, "another session is not held back");
});

test("settle frees a request once it has left Orpheus's queue, but not on a reply older than it", () => {
    const limits = new SessionLimits({ ...LIMIT_DEFAULTS, requestCooldownMs: 0 });
    limits.admitRequest(A, 100);
    limits.requested(A, 41, 1, 100);
    limits.settle([], 100);
    assert.match(limits.admitRequest(A, 101) || "", /waiting/, "a status asked for at the same moment proves nothing");
    limits.settle([41], 200);
    assert.match(limits.admitRequest(A, 201) || "", /waiting/, "still queued");
    limits.settle([7], 300);
    assert.equal(limits.admitRequest(A, 301), null, "started, so free again");
});

test("a song that starts at once is not waiting; a failed request frees the slot but keeps the cool-down", () => {
    const limits = new SessionLimits(LIMIT_DEFAULTS);
    limits.admitRequest(A, 0);
    limits.requested(A, 41, 0, 0);
    assert.equal(limits.waiting.size, 0);
    limits.admitRequest(B, 0);
    limits.failed(B);
    assert.equal(limits.waiting.size, 0);
    assert.match(limits.admitRequest(B, 1000) || "", /One request every 10 s; try again in 9 s/);
});

test("the cool-down: one request per session per window, with how long to wait", () => {
    const limits = new SessionLimits(LIMIT_DEFAULTS);
    assert.equal(limits.admitRequest(A, 0), null);
    limits.requested(A, 41, 0, 0);
    assert.equal(limits.admitRequest(A, 9_999), "One request every 10 s; try again in 1 s");
    assert.equal(limits.admitRequest(A, 10_000), null);
});

test("the queue cap counts this piano's waiting requests; 0 turns requests off", () => {
    const limits = new SessionLimits({ ...LIMIT_DEFAULTS, requestQueueCap: 2 });
    const sessions = ["s1", "s2", "s3"];
    sessions.slice(0, 2).forEach((session, i) => {
        assert.equal(limits.admitRequest(session, 0), null);
        limits.requested(session, 10 + i, i + 1, 0);
    });
    assert.equal(limits.admitRequest("s3", 0), "2 requests from this piano are already waiting (the cap is 2); "
        + "try again when one has started");
    limits.settle([11], 1);
    assert.equal(limits.admitRequest("s3", 1), null);
    const off = new SessionLimits({ ...LIMIT_DEFAULTS, requestQueueCap: 0 });
    assert.equal(off.admitRequest(A, 0), "Song requests are turned off on this piano");
});

test("transport: a sliding window per session, with a reason and how long to wait", () => {
    const limits = new SessionLimits({ ...LIMIT_DEFAULTS, transportCalls: 3, transportWindowMs: 10_000 });
    for (let i = 0; i < 3; i++) {
        assert.equal(limits.admitTransport(A, i * 1000), null);
    }
    assert.equal(limits.admitTransport(A, 3000), "Too many controls: at most 3 every 10 s; try again in 7 s");
    assert.equal(limits.admitTransport(B, 3000), null, "another session has its own window");
    assert.equal(limits.admitTransport(A, 10_000), null, "the first call has left the window");
    assert.match(limits.admitTransport(A, 10_500) || "", /Too many controls/);
    limits.admitTransport(B, 100_000);
    assert.deepEqual([...limits.transport.keys()], [B], "a quiet session is forgotten");
});

test("prune forgets cool-downs that ran out", () => {
    const limits = new SessionLimits(LIMIT_DEFAULTS);
    limits.admitRequest(A, 0);
    limits.requested(A, 1, 0, 0);
    limits.prune(9_999);
    assert.equal(limits.lastRequest.size, 1);
    limits.prune(10_000);
    assert.equal(limits.lastRequest.size, 0);
});

test("songIdOf takes a positive integer id, as text or a number, and nothing else", () => {
    assert.equal(songIdOf("12"), 12);
    assert.equal(songIdOf(3), 3);
    for (const bad of [undefined, "", "0", "-1", "1.5", "1e3", "12abc", " 4", null, {}]) {
        assert.equal(songIdOf(bad), null, JSON.stringify(bad));
    }
});

test("refusalReason: FastAPI's detail, else the status", () => {
    assert.equal(refusalReason(429, JSON.stringify({ detail: "One request per visitor every 10 s" })),
        "One request per visitor every 10 s");
    assert.equal(refusalReason(502, "<html>Bad gateway</html>"), "Orpheus answered HTTP 502");
    assert.equal(refusalReason(422, JSON.stringify({ detail: [{ msg: "x" }] })), "Orpheus answered HTTP 422");
});

test("formatResult: what the visitor's label shows", () => {
    assert.equal(formatResult({ action: "request", ok: true, songId: 1, title: "Gymnopedie", position: 0 }),
        "Requested: Gymnopedie\nplaying now");
    assert.equal(formatResult({ action: "request", ok: true, songId: 1, position: 1 }), "Requested: song 1\nplays next");
    assert.equal(formatResult({ action: "select", ok: true, songId: 1, title: "Gymnopedie" }), "Playing now: Gymnopedie");
    assert.equal(formatResult({ action: "next", ok: true }), "Piano: next");
    assert.equal(formatResult({ action: "pause", ok: false, reason: "Nothing is playing" }),
        "Piano: pause refused\nNothing is playing");
});

test("songEntries: ids and what a picker shows; null when Orpheus did not answer", () => {
    assert.equal(songEntries(null), null);
    assert.deepEqual(songEntries([
        { id: 1, title: "Gymnopedie", composer: "Satie", duration_seconds: 180.4, genre: null },
        { id: 2, title: null, composer: "", duration_seconds: null },
        { title: "no id" }
    ]), [
        { id: 1, title: "Gymnopedie", composer: "Satie", duration: 180 },
        { id: 2, title: "Untitled", composer: null, duration: null }
    ]);
});
