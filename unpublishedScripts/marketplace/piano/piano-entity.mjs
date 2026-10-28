//
//  piano-entity.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano's serverScripts, run by the entity script server. The piano is a speaker for an
//  Orpheus synthesizer: it plays Orpheus's live PCM stream at the entity's position, follows
//  the entity when it moves, and mirrors Orpheus's playback status into userData.piano.status.
//  Visitors request songs and run the transport through its remotely callable methods, each
//  held to per-session limits (Morpheus ADRs 0020 and 0022), and the lid, stick and fallboard
//  follow the status (ADR 0021). Nobody plays its keys from inside the world.
//
//  The bearer for Orpheus comes from the domain's script credential table, which the engine
//  attaches to this bundle's requests under the credential's URL prefix; it is never in userData.
//
//  The engine shares one module instance per URL between every piano that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import {
    STREAM_PATH, PLAYBACK_STATUS_PATH, SYNTH_STATUS_PATH, SONGS_PATH,
    parseObject, parseConfig, parseManifestURL, withStatus, compactStatus, pollDelay, Backoff, formatSongs
} from "./piano-core.mjs";
import {
    REQUEST_PATH, TRANSPORT_ROUTES, SESSION_HEADER, LIMIT_DEFAULTS, SessionLimits, parseLimits, songIdOf,
    refusalReason, songEntries
} from "./piano-bridge-core.mjs";
import { parseJoints, poseFor, stepPose, samePose, jointEdit } from "./piano-pose-core.mjs";

const POSITION_POLL_MS = 1000;
const FETCH_TIMEOUT_MS = 4000;
const SONGS_CACHE_MS = 60_000;
const TWEEN_TICK_MS = 100;
const MANIFEST_RETRY_MS = 60_000;
const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";

/**
 * @param {string} url
 * @param {Request.Init} [init]
 * @returns {Promise<Response>}
 */
async function send(url, init = {}) {
    const controller = new AbortController();
    // A host that drops packets would otherwise hold the poll chain forever
    const timer = Script.setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
    try {
        return await fetch(url, { ...init, signal: controller.signal });
    } finally {
        Script.clearTimeout(timer);
    }
}

/**
 * @param {string} url
 * @returns {Promise<any>} the parsed JSON body
 */
async function getJSON(url) {
    const response = await send(url);
    if (!response.ok) {
        throw new Error("HTTP " + response.status + " from " + url);
    }
    return await response.json();
}

/**
 * @param {Vec3 | undefined} a
 * @param {Vec3 | undefined} b
 * @returns {boolean}
 */
function samePosition(a, b) {
    return !!a && !!b && a.x === b.x && a.y === b.y && a.z === b.z;
}

/**
 * @param {string | undefined} caller
 * @returns {string} the caller's session, or "" for none
 */
function sessionOf(caller) {
    return caller && caller !== NULL_UUID ? caller : "";
}

/** @typedef {import("./piano-bridge-core.mjs").CallResult} CallResult */
/** @typedef {"play" | "pause" | "stop" | "next" | "previous" | "replay" | "select"} TransportAction */

export default class PianoEntity {
    remotelyCallable = ["status", "list", "request", "play", "pause", "stop", "next", "previous", "replay", "select"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    orpheusBase = "";
    volume = 0;
    stopped = false;
    locked = false;
    /** @type {AudioInjector | null} */ injector = null;
    connectedAt = 0;
    backoff = new Backoff();
    /** @type {Vec3 | undefined} */ position;
    /** @type {number | null} */ reconnectTimer = null;
    /** @type {number | null} */ positionTimer = null;
    /** @type {number | null} */ statusTimer = null;
    /** @type {import("./piano-core.mjs").PianoStatus | null} */ lastStatus = null;
    lastStatusJSON = "";
    /** @type {{ at: number, songs: any[] } | null} */ songsCache = null;
    /** @type {Promise<any[] | null> | null} */ songsLoading = null;
    limits = new SessionLimits(LIMIT_DEFAULTS);
    manifestURL = "";
    /** @type {number | null} */ manifestTimer = null;
    /** @type {import("./piano-pose-core.mjs").JointMap | null} */ joints = null;
    /** @type {import("./piano-pose-core.mjs").Pose | null} what the entity's joints were last given */
    pose = null;
    /** @type {import("./piano-pose-core.mjs").Pose | null} */ target = null;
    /** @type {number | null} */ tweenTimer = null;
    tweenAt = 0;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["userData", "locked", "position"]);
        const config = parseConfig(properties.userData);
        if ("error" in config) {
            print("Piano " + entityID + ": not starting, " + config.error);
            this.stopped = true;
            return;
        }
        this.orpheusBase = config.orpheusBase;
        this.volume = config.volume;
        this.position = properties.position;
        const { limits, problems } = parseLimits(parseObject(properties.userData).piano);
        problems.forEach(problem => print("Piano " + entityID + ": " + problem));
        this.limits = new SessionLimits(limits);
        // A locked entity takes no edit but to `locked`, so neither the status nor the lid could move.
        this.locked = properties.locked === true;
        if (this.locked) {
            print("Piano " + entityID + ": locked, so the status is not mirrored into userData and the lid stays put");
        }
        const manifest = parseManifestURL(properties.userData);
        if ("error" in manifest) {
            print("Piano " + entityID + ": the lid stays put, " + manifest.error);
        } else if (!this.locked) {
            this.manifestURL = manifest.manifestURL;
            this.loadManifest();
        }
        this.connect();
        this.positionTimer = Script.setInterval(() => this.followEntity(), POSITION_POLL_MS);
        this.pollStatus();
    }

    unload() {
        this.stopped = true;
        for (const timer of [this.reconnectTimer, this.statusTimer, this.manifestTimer]) {
            if (timer !== null) {
                Script.clearTimeout(timer);
            }
        }
        for (const timer of [this.positionTimer, this.tweenTimer]) {
            if (timer !== null) {
                Script.clearInterval(timer);
            }
        }
        this.reconnectTimer = this.statusTimer = this.manifestTimer = this.positionTimer = this.tweenTimer = null;
        if (this.injector) {
            const injector = this.injector;
            this.injector = null;
            injector.stop();
        }
    }

    /** Plays the stream at the entity. */
    connect() {
        this.reconnectTimer = null;
        if (this.stopped) {
            return;
        }
        this.connectedAt = Date.now();
        const injector = /** @type {AudioInjector | null} */ (Audio.playStream(this.orpheusBase + STREAM_PATH,
            { position: this.position, volume: this.volume }));
        this.injector = injector;
        if (injector === null) {
            this.reconnect();
            return;
        }
        injector.finished.connect(() => {
            if (this.injector === injector) {
                this.injector = null;
                this.reconnect();
            }
        });
    }

    reconnect() {
        if (this.stopped || this.reconnectTimer !== null) {
            return;
        }
        const delay = this.backoff.next(Date.now() - this.connectedAt);
        print("Piano " + this.entityID + ": stream ended, reconnecting in " + delay / 1000 + " s");
        this.reconnectTimer = Script.setTimeout(() => this.connect(), delay);
    }

    followEntity() {
        const position = Entities.getEntityProperties(this.entityID, ["position"]).position;
        if (!position || samePosition(position, this.position)) {
            return;
        }
        this.position = position;
        if (this.injector) {
            // setOptions replaces every option, so the volume goes along with the position
            this.injector.setOptions({ position, volume: this.volume });
        }
    }

    async pollStatus() {
        this.statusTimer = null;
        const polledAt = Date.now();
        const [playback, synth] = await Promise.allSettled([
            getJSON(this.orpheusBase + PLAYBACK_STATUS_PATH),
            getJSON(this.orpheusBase + SYNTH_STATUS_PATH)
        ]);
        if (this.stopped) {
            return;
        }
        if (playback.status === "fulfilled" && Array.isArray(playback.value.queue)) {
            this.limits.settle(playback.value.queue.map(/** @param {any} row */ row => row.id), polledAt);
        }
        this.limits.prune(Date.now());
        const status = compactStatus(
            playback.status === "fulfilled" ? playback.value : null,
            synth.status === "fulfilled" ? synth.value : null,
            playback.status === "rejected" ? String(playback.reason) : undefined);
        this.publish(status);
        this.follow(status);
        this.statusTimer = Script.setTimeout(() => this.pollStatus(), pollDelay(status));
    }

    /** After a call that changes playback: poll now, unless a poll is already on its way. */
    pollSoon() {
        if (this.statusTimer !== null && !this.stopped) {
            Script.clearTimeout(this.statusTimer);
            this.pollStatus();
        }
    }

    /** @param {import("./piano-core.mjs").PianoStatus} status */
    publish(status) {
        this.lastStatus = status;
        const json = JSON.stringify(status);
        if (json === this.lastStatusJSON) {
            return;
        }
        this.lastStatusJSON = json;
        if (status.state === "offline") {
            print("Piano " + this.entityID + ": offline, " + status.error);
        }
        if (!this.locked) {
            const userData = Entities.getEntityProperties(this.entityID, ["userData"]).userData;
            Entities.editEntity(this.entityID, { userData: withStatus(userData, status) });
        }
    }

    async loadManifest() {
        this.manifestTimer = null;
        let manifest;
        try {
            manifest = await getJSON(this.manifestURL);
        } catch (error) {
            if (!this.stopped) {
                print("Piano " + this.entityID + ": manifest failed, retrying in " + MANIFEST_RETRY_MS / 1000 + " s, " + error);
                this.manifestTimer = Script.setTimeout(() => this.loadManifest(), MANIFEST_RETRY_MS);
            }
            return;
        }
        if (this.stopped) {
            return;
        }
        const joints = parseJoints(manifest);
        if ("error" in joints) {
            print("Piano " + this.entityID + ": the lid stays put, " + joints.error);
            return;
        }
        this.joints = joints;
        if (this.lastStatus) {
            this.follow(this.lastStatus);
        }
    }

    /**
     * Moves the lid, stick and fallboard toward the pose the status asks for. The first pose is
     * written at once, since what the joints held before this script loaded is not known.
     * @param {import("./piano-core.mjs").PianoStatus} status
     */
    follow(status) {
        if (!this.joints) {
            return;
        }
        this.target = poseFor(status, this.joints);
        if (this.pose === null) {
            this.writePose(this.target);
        } else if (this.tweenTimer === null && !samePose(this.pose, this.target)) {
            this.tweenAt = Date.now();
            this.tweenTimer = Script.setInterval(() => this.tween(), TWEEN_TICK_MS);
        }
    }

    tween() {
        if (!this.joints || !this.pose || !this.target) {
            return;
        }
        const now = Date.now();
        this.writePose(stepPose(this.pose, this.target, now - this.tweenAt, this.joints));
        this.tweenAt = now;
        if (samePose(this.pose, this.target) && this.tweenTimer !== null) {
            Script.clearInterval(this.tweenTimer);
            this.tweenTimer = null;
        }
    }

    /** @param {import("./piano-pose-core.mjs").Pose} pose */
    writePose(pose) {
        if (!this.joints) {
            return;
        }
        this.pose = pose;
        Entities.editEntity(this.entityID, jointEdit(pose, this.joints));
    }

    /** @returns {Promise<any[] | null>} /api/songs, at most a minute old; null if Orpheus did not answer */
    songs() {
        if (this.songsCache && Date.now() - this.songsCache.at < SONGS_CACHE_MS) {
            return Promise.resolve(this.songsCache.songs);
        }
        if (!this.songsLoading) {
            this.songsLoading = getJSON(this.orpheusBase + SONGS_PATH).then(songs => {
                this.songsCache = { at: Date.now(), songs };
                return songs;
            }, error => {
                print("Piano " + this.entityID + ": song list failed, " + error);
                return null;
            }).finally(() => {
                this.songsLoading = null;
            });
        }
        return this.songsLoading;
    }

    /**
     * @param {number} songId
     * @returns {string | undefined} its title, if the cached song list has it
     */
    titleOf(songId) {
        const songs = this.songsCache ? this.songsCache.songs : [];
        const song = Array.isArray(songs) ? songs.find(entry => entry && entry.id === songId) : undefined;
        return song && typeof song.title === "string" ? song.title : undefined;
    }

    /**
     * POSTs to a bridge route for a visitor. Orpheus limits by the session it is sent; the
     * X-Orpheus-Request header is what its sandbox network grant asks of a credential-less POST.
     * @param {string} path
     * @param {string} session
     * @returns {Promise<{ ok: true, body: Record<string, any> } | { ok: false, reason: string }>}
     */
    async post(path, session) {
        /** @type {Record<string, string>} */
        const headers = { "X-Orpheus-Request": "1" };
        if (session) {
            headers[SESSION_HEADER] = session;
        }
        try {
            const response = await send(this.orpheusBase + path, { method: "POST", headers });
            const text = await response.text();
            return response.ok ? { ok: true, body: parseObject(text) } : { ok: false, reason: refusalReason(response.status, text) };
        } catch (error) {
            return { ok: false, reason: "Orpheus did not answer: " + error };
        }
    }

    // Remotely callable. The engine passes (entityID, params: string[]) and stamps the caller's
    // session ID in Script.remoteCallerID for the synchronous part only, so it is read first.
    // Entities.callEntityServerMethod returns nothing, so the answer also goes to the caller's
    // client script (piano-client.mjs) on this entity.

    /** @returns {import("./piano-core.mjs").PianoStatus | null} */
    status() {
        this.reply(sessionOf(Script.remoteCallerID), "showStatus", [JSON.stringify(this.lastStatus)]);
        return this.lastStatus;
    }

    /**
     * Answers showSongs([text, json]): the text the label shows, and the songs with their ids
     * for a song picker, `[{id, title, composer, duration}]` (null when Orpheus did not answer).
     * @returns {Promise<any[] | null>}
     */
    async list() {
        const session = sessionOf(Script.remoteCallerID);
        if (this.stopped) {
            return null;
        }
        const songs = await this.songs();
        if (!this.stopped) {
            this.reply(session, "showSongs", [formatSongs(songs), JSON.stringify(songEntries(songs))]);
        }
        return songs;
    }

    /**
     * Appends a library song to Orpheus's queue on the synth. Never plays it now.
     * @param {Uuid} _id
     * @param {string[]} params [songId]
     * @returns {Promise<CallResult>}
     */
    async request(_id, params) {
        const session = sessionOf(Script.remoteCallerID);
        const songId = songIdOf(params && params[0]);
        return this.answer(session, await this.requestSong(session, songId));
    }

    /**
     * @param {string} session
     * @param {number | null} songId
     * @returns {Promise<CallResult>}
     */
    async requestSong(session, songId) {
        if (songId === null) {
            return { action: "request", ok: false, reason: "Request a song by its library id" };
        }
        /** @type {CallResult} */
        const result = { action: "request", ok: false, songId, title: this.titleOf(songId) };
        if (this.stopped) {
            return { ...result, reason: "This piano is not connected to Orpheus" };
        }
        const refusal = this.limits.admitRequest(session, Date.now());
        if (refusal !== null) {
            return { ...result, reason: refusal };
        }
        const reply = await this.post(REQUEST_PATH + songId + "?output=synth", session);
        if (!reply.ok) {
            this.limits.failed(session);
            return { ...result, reason: reply.reason };
        }
        const position = Number(reply.body.position);
        this.limits.requested(session, reply.body.queue_id, position, Date.now());
        this.pollSoon();
        return { ...result, ok: true, position };
    }

    play() { return this.transport("play", sessionOf(Script.remoteCallerID), null); }
    pause() { return this.transport("pause", sessionOf(Script.remoteCallerID), null); }
    stop() { return this.transport("stop", sessionOf(Script.remoteCallerID), null); }
    next() { return this.transport("next", sessionOf(Script.remoteCallerID), null); }
    previous() { return this.transport("previous", sessionOf(Script.remoteCallerID), null); }
    replay() { return this.transport("replay", sessionOf(Script.remoteCallerID), null); }

    /**
     * Plays a library song now, replacing what plays and what is queued (ADR 0022).
     * @param {Uuid} _id
     * @param {string[]} params [songId]
     */
    select(_id, params) {
        return this.transport("select", sessionOf(Script.remoteCallerID), songIdOf(params && params[0]));
    }

    /**
     * @param {TransportAction} action
     * @param {string} session
     * @param {number | null} songId for select
     * @returns {Promise<CallResult>}
     */
    async transport(action, session, songId) {
        return this.answer(session, await this.runTransport(action, session, songId));
    }

    /**
     * @param {TransportAction} action
     * @param {string} session
     * @param {number | null} songId
     * @returns {Promise<CallResult>}
     */
    async runTransport(action, session, songId) {
        /** @type {CallResult} */
        const result = songId !== null ? { action, ok: false, songId, title: this.titleOf(songId) } : { action, ok: false };
        if (action === "select" && songId === null) {
            return { ...result, reason: "Select a song by its library id" };
        }
        if (this.stopped) {
            return { ...result, reason: "This piano is not connected to Orpheus" };
        }
        const refusal = this.limits.admitTransport(session, Date.now());
        if (refusal !== null) {
            return { ...result, reason: refusal };
        }
        let path;
        if (action === "select") {
            path = "/api/playback/play-song/" + songId + "?output=synth&play_now=true";
        } else if (action === "play" || action === "pause") {
            // Orpheus's pause toggles, so the piano asks what is playing to keep each button one-way
            let playback;
            try {
                playback = await getJSON(this.orpheusBase + PLAYBACK_STATUS_PATH);
            } catch (error) {
                return { ...result, reason: "Orpheus did not answer: " + error };
            }
            const playing = playback.is_playing === true;
            const paused = playing && playback.is_paused === true;
            if (action === "pause" && !playing) {
                return { ...result, reason: "Nothing is playing" };
            }
            if (action === "pause" && paused) {
                return { ...result, reason: "Already paused" };
            }
            if (action === "play" && playing && !paused) {
                return { ...result, reason: "Already playing" };
            }
            // Play from idle plays the last piece again, as Orpheus's own Play does
            path = action === "play" && !playing ? TRANSPORT_ROUTES.replay : TRANSPORT_ROUTES.pause;
        } else {
            path = TRANSPORT_ROUTES[action];
        }
        const reply = await this.post(path, session);
        if (!reply.ok) {
            return { ...result, reason: reply.reason };
        }
        if (action === "next" && reply.body.skipped === false) {
            return { ...result, reason: "Nothing is queued to skip to" };
        }
        this.pollSoon();
        return { ...result, ok: true };
    }

    /**
     * Logs a call's result and sends it to the caller's client script as showResult([json]).
     * @param {string} session
     * @param {CallResult} result
     * @returns {CallResult}
     */
    answer(session, result) {
        print("Piano " + this.entityID + ": " + result.action + (result.songId !== undefined ? " " + result.songId : "")
            + " from " + (session || "nobody") + (result.ok ? " done" : " refused, " + result.reason));
        this.reply(session, "showResult", [JSON.stringify(result)]);
        return result;
    }

    /**
     * @param {string} session
     * @param {string} method
     * @param {string[]} params
     */
    reply(session, method, params) {
        if (session) {
            Entities.callEntityClientMethod(session, this.entityID, method, params);
        }
    }
}
