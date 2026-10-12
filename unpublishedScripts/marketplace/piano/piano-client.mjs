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
//  it writes nothing to the piano and has no controls.
//

import { statusOf, formatStatus } from "./piano-core.mjs";

const LABEL_NAME = "Piano.Status";
const LABEL_SIZE = { x: 1.2, y: 0.5, z: 0.01 };
const LABEL_GAP = 0.1;
const LABEL_LINE_HEIGHT = 0.06;
const REFRESH_MS = 1000;
const SONGS_SHOWN_MS = 8000;

export default class PianoClient {
    remotelyCallable = ["showStatus", "showSongs"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {Uuid} */ labelID;
    /** @type {number} */ refreshTimer;
    shown = "";
    songsUntil = 0;
    /** @type {import("./piano-core.mjs").PianoStatus | null} from status(), for a piano that cannot publish */
    reported = null;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = /** @type {Partial<Entities.EntityProperties_Box>} */ (
            Entities.getEntityProperties(entityID, ["dimensions", "userData"]));
        const height = properties.dimensions ? properties.dimensions.y : 1;
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
        Script.clearInterval(this.refreshTimer);
        Entities.deleteEntity(this.labelID);
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
