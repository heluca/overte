//
//  piano-client.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The piano's client entity script, run by Interface for each visitor. It floats a local Text
//  entity above the piano showing what Orpheus is playing, read from userData.piano.status, and
//  on a click asks the piano for Orpheus's song list and shows it for a few seconds. Read-only:
//  it writes nothing to the piano.
//
//  For whoever sits at the piano, the bench's client script (piano-bench-client.mjs) calls
//  openDisplay and closeDisplay here: a local Web entity on the music desk shows the page at
//  userData.piano.displayURL, and the floating label hides meanwhile. The page's buttons arrive
//  as web events; for now they are only printed and acknowledged back to the page.
//

import { statusOf, formatStatus, parseDisplayURL, displayGeometry, parseDisplayEvent } from "./piano-core.mjs";

const LABEL_NAME = "Piano.Status";
const LABEL_SIZE = { x: 1.2, y: 0.5, z: 0.01 };
const LABEL_GAP = 0.1;
const LABEL_LINE_HEIGHT = 0.06;
const REFRESH_MS = 1000;
const SONGS_SHOWN_MS = 8000;
const DISPLAY_NAME = "Piano.Display";
const DISPLAY_JOINT = "anchor_desk_screen";

export default class PianoClient {
    // openDisplay and closeDisplay are left out on purpose: only scripts in this Interface
    // (the bench's) may call them, never the server.
    remotelyCallable = ["showStatus", "showSongs"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {Uuid} */ labelID;
    /** @type {number} */ refreshTimer;
    shown = "";
    songsUntil = 0;
    /** @type {import("./piano-core.mjs").PianoStatus | null} from status(), for a piano that cannot publish */
    reported = null;
    /** @type {{ displayURL: string } | { error: string }} */ display;
    /** @type {Uuid | null} */ displayID = null;
    /** @type {((entityID: Uuid, message: string) => void) | null} */ webEventHandler = null;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = /** @type {Partial<Entities.EntityProperties_Box>} */ (
            Entities.getEntityProperties(entityID, ["dimensions", "userData"]));
        const height = properties.dimensions ? properties.dimensions.y : 1;
        this.display = parseDisplayURL(properties.userData);
        if ("error" in this.display) {
            print("Piano " + entityID + ": no desk display, " + this.display.error);
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
        this.refresh();
        this.refreshTimer = Script.setInterval(() => this.refresh(), REFRESH_MS);
    }

    unload() {
        this.closeDisplay();
        Script.clearInterval(this.refreshTimer);
        Entities.deleteEntity(this.labelID);
    }

    // Called with Entities.callEntityMethod by the bench's client script in this Interface.

    openDisplay() {
        if (this.displayID !== null) {
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
        const properties = /** @type {Partial<Entities.EntityProperties_Model>} */ (
            Entities.getEntityProperties(this.entityID, ["dimensions"]));
        const { dimensions, dpi } = displayGeometry(properties.dimensions);
        // The anchor's +Z points out of the desk at the player, and a Web entity's face is its
        // +Z, so zero local offset and rotation lay the page on the glass.
        this.displayID = Entities.addEntity({
            type: "Web",
            name: DISPLAY_NAME,
            parentID: this.entityID,
            parentJointIndex: joint,
            localPosition: { x: 0, y: 0, z: 0 },
            localRotation: { x: 0, y: 0, z: 0, w: 1 },
            dimensions,
            dpi,
            sourceUrl: this.display.displayURL,
            // Keyboard focus on the page would swallow the movement keys that stand the avatar up
            wantsKeyboardFocus: false,
            showKeyboardFocusHighlight: false,
            collisionless: true,
            grab: { grabbable: false }
        }, "local");
        const displayID = this.displayID;
        this.webEventHandler = (id, message) => {
            if (id === displayID) {
                this.onDisplayEvent(message);
            }
        };
        Entities.webEventReceived.connect(this.webEventHandler);
        Entities.editEntity(this.labelID, { visible: false });
    }

    closeDisplay() {
        if (this.displayID === null) {
            return;
        }
        if (this.webEventHandler) {
            Entities.webEventReceived.disconnect(this.webEventHandler);
            this.webEventHandler = null;
        }
        Entities.deleteEntity(this.displayID);
        this.displayID = null;
        Entities.editEntity(this.labelID, { visible: true });
    }

    /** @param {string} message */
    onDisplayEvent(message) {
        const event = parseDisplayEvent(message);
        if (event === null) {
            print("Piano " + this.entityID + ": ignored display event " + JSON.stringify(message));
            return;
        }
        // Not wired to Orpheus yet: printed, and acknowledged so the page shows the round trip
        print("Piano " + this.entityID + ": display " + event.action + (event.songId !== null ? " " + event.songId : ""));
        if (this.displayID !== null) {
            Entities.emitScriptEvent(this.displayID, JSON.stringify({ type: "piano-display-ack", ...event }));
        }
    }

    /** @param {Uuid} _id @param {PointerEvent} event */
    clickDownOnEntity(_id, event) {
        if (event.isPrimaryButton) {
            Entities.callEntityServerMethod(this.entityID, "list", []);
        }
    }

    // Called by the piano's server script in answer to `status` and `list`.

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

    /** @param {Uuid} _id @param {string[]} params [song list text] */
    showSongs(_id, params) {
        this.songsUntil = Date.now() + SONGS_SHOWN_MS;
        this.show(String(params[0]));
    }

    refresh() {
        if (Date.now() < this.songsUntil) {
            return;
        }
        const published = statusOf(Entities.getEntityProperties(this.entityID, ["userData"]).userData);
        this.show(formatStatus(published || this.reported));
    }

    /** @param {string} text */
    show(text) {
        if (text !== this.shown) {
            Entities.editEntity(this.labelID, { text });
            this.shown = text;
        }
    }
}
