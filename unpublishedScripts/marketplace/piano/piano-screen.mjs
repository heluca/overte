//
//  piano-screen.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  One of the piano's screens in Interface: a local Web entity showing piano-display.html, seen
//  by this visitor only. The page draws whatever view it is sent and reports its buttons as
//  web events; it says "ready" once its EventBridge is up, since a script event sent before
//  that is lost. Used by piano-client.mjs for the song picker and the desk screen.
//

import { parseScreenEvent } from "./piano-screen-core.mjs";

export class PianoScreen {
    /** @type {Uuid | null} */ id = null;
    ready = false;
    /** the view last sent to the page, as JSON */
    sent = "";
    /** the view to show, as JSON */
    wanted = "";
    /** @type {((entityID: Uuid, message: string) => void) | null} */ handler = null;

    /**
     * @param {string} owner names the piano in log lines
     * @param {(event: import("./piano-screen-core.mjs").ScreenEvent) => void} onEvent the page's buttons
     */
    constructor(owner, onEvent) {
        this.owner = owner;
        this.onEvent = onEvent;
    }

    get isOpen() {
        return this.id !== null;
    }

    /** @param {Partial<Entities.EntityProperties_Web>} properties where it goes and its sourceUrl */
    open(properties) {
        this.close();
        this.id = Entities.addEntity({
            type: "Web",
            ...properties,
            // Keyboard focus on the page would swallow the movement keys that stand the avatar up
            wantsKeyboardFocus: false,
            showKeyboardFocusHighlight: false,
            collisionless: true,
            grab: { grabbable: false }
        }, "local");
        const id = this.id;
        this.handler = (from, message) => {
            if (from === id) {
                this.received(message);
            }
        };
        Entities.webEventReceived.connect(this.handler);
    }

    /** @param {Partial<Entities.EntityProperties_Web>} properties */
    edit(properties) {
        if (this.id !== null) {
            Entities.editEntity(this.id, properties);
        }
    }

    close() {
        if (this.handler) {
            Entities.webEventReceived.disconnect(this.handler);
            this.handler = null;
        }
        if (this.id !== null) {
            Entities.deleteEntity(this.id);
            this.id = null;
        }
        this.ready = false;
        this.sent = this.wanted = "";
    }

    /** @param {object} view sent when the page is ready and only when it changed */
    show(view) {
        this.wanted = JSON.stringify(view);
        this.flush();
    }

    flush() {
        if (this.id !== null && this.ready && this.wanted !== this.sent) {
            Entities.emitScriptEvent(this.id, this.wanted);
            this.sent = this.wanted;
        }
    }

    /** @param {string} message */
    received(message) {
        const event = parseScreenEvent(message);
        if (event === null) {
            print(this.owner + ": ignored screen event " + JSON.stringify(message));
            return;
        }
        if (event.action === "ready") {
            // A page that loaded again has lost what it was sent
            this.ready = true;
            this.sent = "";
            this.flush();
            return;
        }
        this.onEvent(event);
    }
}
