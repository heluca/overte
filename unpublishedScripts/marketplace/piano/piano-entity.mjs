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
//  Nobody plays it from inside the world; the remotely callable methods only read.
//
//  The engine shares one module instance per URL between every piano that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import {
    STREAM_PATH, PLAYBACK_STATUS_PATH, SYNTH_STATUS_PATH, SONGS_PATH,
    parseConfig, withStatus, compactStatus, pollDelay, Backoff, formatSongs
} from "./piano-core.mjs";

const POSITION_POLL_MS = 1000;
const FETCH_TIMEOUT_MS = 4000;
const SONGS_CACHE_MS = 60_000;
const NULL_UUID = "{00000000-0000-0000-0000-000000000000}";

/**
 * @param {string} url
 * @returns {Promise<any>} the parsed JSON body
 */
async function getJSON(url) {
    const controller = new AbortController();
    // A host that drops packets would otherwise hold the poll chain forever
    const timer = Script.setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
    try {
        const response = await fetch(url, { signal: controller.signal });
        if (!response.ok) {
            throw new Error("HTTP " + response.status + " from " + url);
        }
        return await response.json();
    } finally {
        Script.clearTimeout(timer);
    }
}

/**
 * @param {Vec3 | undefined} a
 * @param {Vec3 | undefined} b
 * @returns {boolean}
 */
function samePosition(a, b) {
    return !!a && !!b && a.x === b.x && a.y === b.y && a.z === b.z;
}

export default class PianoEntity {
    remotelyCallable = ["status", "list"];

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
        // A locked entity takes no edit but to `locked`, so the status could not be mirrored.
        this.locked = properties.locked === true;
        if (this.locked) {
            print("Piano " + entityID + ": locked, so the status is not mirrored into userData");
        }
        this.play();
        this.positionTimer = Script.setInterval(() => this.followEntity(), POSITION_POLL_MS);
        this.pollStatus();
    }

    unload() {
        this.stopped = true;
        for (const timer of [this.reconnectTimer, this.statusTimer]) {
            if (timer !== null) {
                Script.clearTimeout(timer);
            }
        }
        if (this.positionTimer !== null) {
            Script.clearInterval(this.positionTimer);
        }
        this.reconnectTimer = this.statusTimer = this.positionTimer = null;
        if (this.injector) {
            const injector = this.injector;
            this.injector = null;
            injector.stop();
        }
    }

    play() {
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
        this.reconnectTimer = Script.setTimeout(() => this.play(), delay);
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
        const [playback, synth] = await Promise.allSettled([
            getJSON(this.orpheusBase + PLAYBACK_STATUS_PATH),
            getJSON(this.orpheusBase + SYNTH_STATUS_PATH)
        ]);
        if (this.stopped) {
            return;
        }
        const status = compactStatus(
            playback.status === "fulfilled" ? playback.value : null,
            synth.status === "fulfilled" ? synth.value : null,
            playback.status === "rejected" ? String(playback.reason) : undefined);
        this.publish(status);
        this.statusTimer = Script.setTimeout(() => this.pollStatus(), pollDelay(status));
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

    // Remotely callable. The engine passes (entityID, params: string[]) and stamps the caller's
    // session ID in Script.remoteCallerID for the synchronous part only, so it is read first.
    // Entities.callEntityServerMethod returns nothing, so the answer also goes to the caller's
    // client script (piano-client.mjs) on this entity.

    /** @returns {import("./piano-core.mjs").PianoStatus | null} */
    status() {
        this.reply(Script.remoteCallerID, "showStatus", JSON.stringify(this.lastStatus));
        return this.lastStatus;
    }

    /** @returns {Promise<any[] | null>} */
    async list() {
        const caller = Script.remoteCallerID;
        if (this.stopped) {
            return null;
        }
        const songs = await this.songs();
        if (!this.stopped) {
            this.reply(caller, "showSongs", formatSongs(songs));
        }
        return songs;
    }

    /**
     * @param {string | undefined} caller
     * @param {string} method
     * @param {string} text
     */
    reply(caller, method, text) {
        if (caller && caller !== NULL_UUID) {
            Entities.callEntityClientMethod(caller, this.entityID, method, [text]);
        }
    }
}
