//
//  piano-client.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano's client entity script, run by Interface for each visitor. It floats a local Text
//  entity above the piano showing what Orpheus is playing, read from userData.piano.status. It
//  writes nothing to the piano; everything a visitor does goes to the piano's server script as
//  a remote call (piano-entity.mjs), held there to per-session limits.
//
//  A click on the score (the `score` mesh, told by its mesh index in the prop's manifest.json)
//  opens the song picker: the page at userData.piano.displayURL in a local Web entity in front
//  of the score. Choosing a song calls `request`; the answer, the queue position or why it was
//  refused, shows on the picker.
//
//  For whoever sits at the piano, the bench's client script (piano-bench-client.mjs) calls
//  openDisplay and closeDisplay here: the same page as the desk screen on the music desk, with
//  the title, progress and queue, the transport buttons, and the song list. Its size and place
//  come from userData.piano.display, reread every second, and the floating label hides meanwhile.
//

import { statusOf, formatStatus, parseDisplayURL, parseDisplayGeometry, parseManifestURL } from "./piano-core.mjs";
import { formatResult } from "./piano-bridge-core.mjs";
import { meshIndexOf } from "./piano-pose-core.mjs";
import {
    TRANSPORT_ACTIONS, parseSongs, resultNotice, waitingNotice, pickerView, deskView, withView, pickerPlacement
} from "./piano-screen-core.mjs";
import { PianoScreen } from "./piano-screen.mjs";

const LABEL_NAME = "Piano.Status";
const LABEL_SIZE = { x: 1.2, y: 0.5, z: 0.01 };
const LABEL_GAP = 0.1;
const LABEL_LINE_HEIGHT = 0.06;
const REFRESH_MS = 1000;
const NOTICE_SHOWN_MS = 8000;
const DISPLAY_NAME = "Piano.Display";
const DISPLAY_JOINT = "anchor_desk_screen";
const PICKER_NAME = "Piano.Picker";
// 0.48 m square: the page's 24 x 24 rem picker canvas at 20 mm a rem, legible from a few metres
const PICKER = { width: 0.48, height: 0.48, pagePixelsWide: 960, toward: 0.3, lift: 0.05 };
const PICKER_IDLE_MS = 30_000;
const SONGS_FRESH_MS = 60_000;
const SONGS_ASK_MS = 5000;
const METERS_TO_INCHES = 39.3701;
// The click's own hit is on the surface; the precise pick that names the mesh starts just short of it
const PICK_BACK = 0.05;

/** @typedef {import("./piano-screen-core.mjs").ScreenEvent} ScreenEvent */
/** @typedef {import("./piano-screen-core.mjs").Notice} Notice */

export default class PianoClient {
    // openDisplay and closeDisplay are left out on purpose: only scripts in this Interface
    // (the bench's) may call them, never the server.
    remotelyCallable = ["showStatus", "showSongs", "showResult"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {Uuid} */ labelID;
    /** @type {number} */ refreshTimer;
    /** @type {PianoScreen} */ desk;
    /** @type {PianoScreen} */ picker;
    shown = "";
    noticeUntil = 0;
    /** @type {import("./piano-core.mjs").PianoStatus | null} from status(), for a piano that cannot publish */
    reported = null;
    /** @type {import("./piano-core.mjs").PianoStatus | null} */ status = null;
    /** @type {{ displayURL: string } | { error: string }} */ display;
    /** piano.display as JSON when the open display last took it */
    displaySource = "";
    /** @type {number | null} the score's mesh index; null until the manifest says, and on a piano without one */
    scoreMesh = null;
    /** @type {import("./piano-screen-core.mjs").Song[] | null | undefined} undefined until the piano answers */
    songs = undefined;
    songsAt = 0;
    songsAsked = -Infinity;
    /** @type {{ tab: "queue" | "songs", page: number, notice: Notice | null }} */
    deskState = { tab: "queue", page: 0, notice: null };
    /** @type {{ page: number, notice: Notice | null, touched: number }} */
    pickerState = { page: 0, notice: null, touched: 0 };

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const owner = "Piano " + entityID;
        this.desk = new PianoScreen(owner, event => this.onDeskEvent(event));
        this.picker = new PianoScreen(owner, event => this.onPickerEvent(event));
        const properties = /** @type {Partial<Entities.EntityProperties_Box>} */ (
            Entities.getEntityProperties(entityID, ["dimensions", "userData"]));
        const height = properties.dimensions ? properties.dimensions.y : 1;
        this.display = parseDisplayURL(properties.userData);
        if ("error" in this.display) {
            print(owner + ": no desk display or song picker, " + this.display.error);
        }
        // A local entity: this user sees it, nobody else does, and it needs no edit rights.
        this.labelID = Entities.addEntity({
            type: "Text",
            name: LABEL_NAME,
            parentID: entityID,
            localPosition: { x: 0, y: height / 2 + LABEL_GAP + LABEL_SIZE.y / 2, z: 0 },
            dimensions: LABEL_SIZE,
            lineHeight: LABEL_LINE_HEIGHT,
            textColor: { red: 240, green: 240, blue: 240 },
            backgroundColor: { red: 20, green: 24, blue: 32 },
            backgroundAlpha: 0.8,
            unlit: true,
            billboardMode: "yaw",
            collisionless: true,
            ignorePickIntersection: true,
            grab: { grabbable: false }
        }, "local");
        // A new or locked piano has no status in userData yet: ask once
        if (statusOf(properties.userData) === null) {
            Entities.callEntityServerMethod(entityID, "status", []);
        }
        if (!("error" in this.display)) {
            this.loadScore(properties.userData);
        }
        this.refresh();
        this.refreshTimer = Script.setInterval(() => this.refresh(), REFRESH_MS);
    }

    unload() {
        this.closeDisplay();
        this.picker.close();
        Script.clearInterval(this.refreshTimer);
        Entities.deleteEntity(this.labelID);
    }

    /**
     * Reads the score's mesh index from the prop's manifest. Without one (a Box, or no
     * manifestURL) a click anywhere on the piano opens the picker.
     * @param {string | undefined} userData
     */
    async loadScore(userData) {
        const manifest = parseManifestURL(userData);
        if ("error" in manifest) {
            print("Piano " + this.entityID + ": a click anywhere opens the song picker, " + manifest.error);
            return;
        }
        try {
            const response = await fetch(manifest.manifestURL);
            if (!response.ok) {
                throw new Error("HTTP " + response.status);
            }
            this.scoreMesh = meshIndexOf(await response.json(), "score");
        } catch (error) {
            print("Piano " + this.entityID + ": a click anywhere opens the song picker, the manifest failed: " + error);
            return;
        }
        if (this.scoreMesh === null) {
            print("Piano " + this.entityID + ": a click anywhere opens the song picker, the manifest has no score mesh");
        }
    }

    // Called with Entities.callEntityMethod by the bench's client script in this Interface.

    openDisplay() {
        if (this.desk.isOpen) {
            return;
        }
        if ("error" in this.display) {
            print("Piano " + this.entityID + ": no desk display, " + this.display.error);
            return;
        }
        // The joint exists only once Interface has loaded the piano's model
        const joint = Entities.getJointIndex(this.entityID, DISPLAY_JOINT);
        if (joint < 0) {
            print("Piano " + this.entityID + ": no desk display, the model has no " + DISPLAY_JOINT + " joint (yet)");
            return;
        }
        this.picker.close();
        const geometry = this.displayGeometry(
            Entities.getEntityProperties(this.entityID, ["userData", "dimensions"]), true);
        // The anchor's +Z points out of the desk at the player, and a Web entity's face is its
        // +Z, so an identity rotation lays the page flat on the desk, facing the player.
        this.desk.open({
            name: DISPLAY_NAME,
            parentID: this.entityID,
            parentJointIndex: joint,
            localRotation: { x: 0, y: 0, z: 0, w: 1 },
            ...geometry,
            sourceUrl: withView(this.display.displayURL, "desk")
        });
        this.deskState = { tab: "queue", page: 0, notice: null };
        Entities.editEntity(this.labelID, { visible: false });
        this.render();
    }

    closeDisplay() {
        if (!this.desk.isOpen) {
            return;
        }
        this.desk.close();
        Entities.editEntity(this.labelID, { visible: true });
    }

    /** @param {Uuid} _id @param {PointerEvent} event */
    clickDownOnEntity(_id, event) {
        if (!event.isPrimaryButton) {
            return;
        }
        if ("error" in this.display) {
            // No page to pick from: the song list shows on the label instead
            Entities.callEntityServerMethod(this.entityID, "list", []);
            return;
        }
        const hit = this.scoreHit(event);
        if (hit !== null) {
            this.openPicker(hit, event.direction);
        }
    }

    /**
     * @param {PointerEvent} event
     * @returns {Vec3 | null} where the click met the score, or null when it missed it
     */
    scoreHit(event) {
        const d = event.direction;
        const origin = { x: event.pos3D.x - d.x * PICK_BACK, y: event.pos3D.y - d.y * PICK_BACK, z: event.pos3D.z - d.z * PICK_BACK };
        const result = Entities.findRayIntersection({ origin, direction: d, unmodifiedDirection: d },
            Picks.PICK_DOMAIN_ENTITIES | Picks.PICK_AVATAR_ENTITIES | Picks.PICK_PRECISE, [this.entityID]);
        if (this.scoreMesh === null) {
            return result.intersects ? result.intersection : event.pos3D;
        }
        const extra = /** @type {{ subMeshIndex?: number } | undefined} */ (result.extraInfo);
        const mesh = result.intersects && extra ? extra.subMeshIndex : undefined;
        return mesh === this.scoreMesh ? result.intersection : null;
    }

    /** @param {Vec3} hit @param {Vec3} direction */
    openPicker(hit, direction) {
        if ("error" in this.display) {
            return;
        }
        const { position, rotation } = pickerPlacement(hit, direction, PICKER);
        this.picker.open({
            name: PICKER_NAME,
            position,
            rotation,
            dimensions: { x: PICKER.width, y: PICKER.height, z: 0.01 },
            dpi: PICKER.pagePixelsWide / (PICKER.width * METERS_TO_INCHES),
            sourceUrl: withView(this.display.displayURL, "picker")
        });
        this.pickerState = { page: 0, notice: null, touched: Date.now() };
        print("Piano " + this.entityID + ": song picker opened"
            + (this.scoreMesh !== null ? " from the score, mesh " + this.scoreMesh : ""));
        this.askSongs();
        this.render();
    }

    /** Asks the piano for its songs, unless a fresh list is here or a request is on its way. */
    askSongs() {
        const now = Date.now();
        if (Array.isArray(this.songs) && now - this.songsAt < SONGS_FRESH_MS) {
            return;
        }
        if (now - this.songsAsked >= SONGS_ASK_MS) {
            this.songsAsked = now;
            Entities.callEntityServerMethod(this.entityID, "list", []);
        }
    }

    /** @param {ScreenEvent} event */
    onPickerEvent(event) {
        this.pickerState.touched = Date.now();
        if (event.action === "close") {
            this.picker.close();
            return;
        }
        if (event.action === "page") {
            this.pickerState.page = event.page;
        } else if (event.action === "request") {
            this.pickerState.notice = this.request(event.songId);
        }
        this.render();
    }

    /** @param {ScreenEvent} event */
    onDeskEvent(event) {
        if (event.action === "page") {
            this.deskState.page = event.page;
        } else if (event.action === "tab") {
            this.deskState.tab = event.tab;
            this.deskState.page = 0;
            if (event.tab === "songs") {
                this.askSongs();
            }
        } else if (event.action === "request") {
            this.deskState.notice = this.request(event.songId);
        } else if (TRANSPORT_ACTIONS.includes(event.action)) {
            Entities.callEntityServerMethod(this.entityID, event.action, []);
            this.deskState.notice = waitingNotice(event.action + "...", Date.now());
        }
        this.render();
    }

    /**
     * @param {number} songId
     * @returns {Notice} shown until the piano answers
     */
    request(songId) {
        Entities.callEntityServerMethod(this.entityID, "request", [String(songId)]);
        const song = Array.isArray(this.songs) ? this.songs.find(entry => entry.id === songId) : undefined;
        return waitingNotice("Asking for " + (song ? song.title : "song " + songId) + "...", Date.now());
    }

    // Called by the piano's server script in answer to `status`, `list` and the calls that
    // request songs or run the transport.

    /** @param {Uuid} _id @param {string[]} params [status JSON] */
    showStatus(_id, params) {
        try {
            const status = JSON.parse(params[0]);
            this.reported = status !== null && typeof status === "object" ? status : null;
        } catch (e) {
            this.reported = null;
        }
        this.refresh();
    }

    /** @param {Uuid} _id @param {string[]} params [song list text, songs JSON (for a picker)] */
    showSongs(_id, params) {
        this.songs = parseSongs(params[1]);
        this.songsAt = Date.now();
        if (this.picker.isOpen || this.desk.isOpen) {
            this.render();
        } else {
            this.notice(String(params[0]));
        }
    }

    /** @param {Uuid} _id @param {string[]} params [CallResult JSON] */
    showResult(_id, params) {
        let result;
        try {
            result = JSON.parse(params[0]);
        } catch (e) {
            return;
        }
        if (result === null || typeof result !== "object" || typeof result.action !== "string") {
            return;
        }
        const text = formatResult(result);
        print("Piano " + this.entityID + ": " + text.replace("\n", ": "));
        if (this.picker.isOpen) {
            this.pickerState.notice = resultNotice(result, Date.now());
            this.pickerState.touched = Date.now();
        } else if (this.desk.isOpen) {
            this.deskState.notice = resultNotice(result, Date.now());
        } else {
            this.notice(text);
        }
        this.render();
    }

    /** @param {string} text shown for a few seconds over the status */
    notice(text) {
        this.noticeUntil = Date.now() + NOTICE_SHOWN_MS;
        this.show(text);
    }

    refresh() {
        const properties = Entities.getEntityProperties(this.entityID, ["userData", "dimensions"]);
        this.status = statusOf(properties.userData) || this.reported;
        if (this.desk.isOpen) {
            const geometry = this.displayGeometry(properties, false);
            if (geometry !== null) {
                this.desk.edit(geometry);
            }
        }
        this.render();
        if (Date.now() < this.noticeUntil) {
            return;
        }
        this.show(formatStatus(this.status));
    }

    /** Sends the open screens their views; a picker left alone closes. */
    render() {
        const now = Date.now();
        if (this.picker.isOpen) {
            if (now - this.pickerState.touched >= PICKER_IDLE_MS) {
                this.picker.close();
            } else {
                this.picker.show(pickerView({ ...this.pickerState, songs: this.songs }, now));
            }
        }
        if (this.desk.isOpen) {
            this.desk.show(deskView(this.status, { ...this.deskState, songs: this.songs }, now));
        }
    }

    /**
     * @param {Partial<Entities.EntityProperties>} properties the piano's userData and dimensions
     * @param {boolean} always false: null unless piano.display changed since the display took it
     * @returns {import("./piano-core.mjs").DisplayGeometry | null}
     */
    displayGeometry(properties, always) {
        const dimensions = /** @type {Partial<Entities.EntityProperties_Model>} */ (properties).dimensions;
        const { geometry, problems, source } = parseDisplayGeometry(properties.userData, dimensions);
        if (!always && source === this.displaySource) {
            return null;
        }
        this.displaySource = source;
        problems.forEach(problem => print("Piano " + this.entityID + ": " + problem));
        return geometry;
    }

    /** @param {string} text */
    show(text) {
        if (text !== this.shown) {
            Entities.editEntity(this.labelID, { text });
            this.shown = text;
        }
    }
}
