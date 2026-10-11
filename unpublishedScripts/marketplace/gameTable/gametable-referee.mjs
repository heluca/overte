//
//  gametable-referee.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Game Table referee, run by the entity script server as the board's `serverScripts`.
//  Players call the remotely callable methods with Entities.callEntityServerMethod; the
//  referee publishes the game in the board's userData, on a Text child and on the Messages
//  channel "gametable:<board id>", and drops disc models on the board.
//
//  The engine shares one module instance per URL between every board that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import { COLUMNS, ROWS, EMPTY } from "./connect4.mjs";
import { RefereeCore, channelFor } from "./referee-core.mjs";

const HERE = import.meta.url.replace(/[^/]*$/, "");
const DISC_MODELS = {
    R: HERE + "assets/checkers/red_checker.fbx",
    B: HERE + "assets/checkers/black_checker.fbx"
};
const STATUS_NAME = "GT.Status";
const DISC_PREFIX = "GT.Disc ";
const TICK_MS = 1000;
const CHILD_SEARCH_MS = 3000;
const CHILD_POLL_MS = 500;
const DISC_FILL = 0.8;
const WIN_SCALE = 1.2;
// A disc is thicker than the backplate, so it shows through both faces like a real one.
const DISC_DEPTH = 1.5;
// The checker model lies flat (thin along y); a quarter turn about x stands it up facing z.
const DISC_ROTATION = { x: Math.SQRT1_2, y: 0, z: 0, w: Math.SQRT1_2 };
const STATUS_SIZE = { x: 1.0, y: 0.4, z: 0.01 };
const STATUS_GAP = 0.05;
const STATUS_LINE_HEIGHT = 0.03;
const OFFLINE_TEXT = "Game table\nReferee not running";

/**
 * @param {unknown} text
 * @returns {Record<string, any>} the parsed object, or an empty one for anything else
 */
function parseObject(text) {
    try {
        const value = JSON.parse(typeof text === "string" ? text : "");
        return value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
    } catch (e) {
        return {};
    }
}

export default class GameTableReferee {
    remotelyCallable = ["join", "move", "leave", "state", "newGame"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {string} */ channel;
    /** @type {Vec3} */ dimensions;
    /** @type {RefereeCore} */ core;
    /** @type {number} */ timer;
    /** @type {number | null} */ searchTimer = null;
    /** @type {Uuid | null} */ statusID = null;
    /** @type {Map<string, { id: Uuid, look: string }>} cell "r<row>c<col>" -> disc */ discs = new Map();
    /** @type {{ text: string, board: string[], line: number[][] | null } | null} */ view = null;
    shownText = "";
    ready = false;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        this.channel = channelFor(entityID);
        const properties = /** @type {Partial<Entities.EntityProperties_Box>} */ (
            Entities.getEntityProperties(entityID, ["userData", "dimensions"]));
        this.dimensions = /** @type {Vec3} */ (properties.dimensions);
        const userData = parseObject(properties.userData);
        const config = userData.gameTableConfig || {};
        this.core = new RefereeCore({
            now: () => Date.now(),
            publish: state => this.publish(state),
            render: view => {
                this.view = view;
                this.draw();
            },
            announce: event => this.announce(event)
        }, { tableId: entityID, turnTimeoutS: config.turnTimeoutS, waitTimeoutS: config.waitTimeoutS });
        this.core.start(userData.gameTable);
        this.timer = Script.setInterval(() => this.core.tick(), TICK_MS);
        this.findChildren(Date.now() + CHILD_SEARCH_MS);
    }

    unload() {
        Script.clearInterval(this.timer);
        if (this.searchTimer !== null) {
            Script.clearTimeout(this.searchTimer);
        }
        this.discs.forEach(disc => Entities.deleteEntity(disc.id));
        this.discs.clear();
        if (this.statusID) {
            Entities.editEntity(this.statusID, { text: OFFLINE_TEXT });
        }
    }

    // Remotely callable. The engine passes (entityID, params: string[]) and stamps the
    // caller's authenticated session id in Script.remoteCallerID for the synchronous part only.

    /** @param {Uuid} _id @param {string[]} params [name] */
    join(_id, params) {
        const caller = Script.remoteCallerID;
        this.core.join(caller, params[0]);
    }

    /** @param {Uuid} _id @param {string[]} params [column] */
    move(_id, params) {
        const caller = Script.remoteCallerID;
        this.core.move(caller, params[0]);
    }

    leave() {
        const caller = Script.remoteCallerID;
        this.core.leave(caller);
    }

    state() {
        this.core.state();
    }

    newGame() {
        const caller = Script.remoteCallerID;
        this.core.newGame(caller);
    }

    /** @param {object} state */
    publish(state) {
        const userData = parseObject(Entities.getEntityProperties(this.entityID, ["userData"]).userData);
        userData.gameTable = state;
        Entities.editEntity(this.entityID, { userData: JSON.stringify(userData) });
        Messages.sendMessage(this.channel, JSON.stringify({ type: "state", ...state }));
    }

    /** @param {object} event */
    announce(event) {
        const message = JSON.stringify(event);
        Messages.sendMessage(this.channel, message);
        print("Game table " + this.entityID + ": " + message);
    }

    /**
     * Children persist with the board, and the entity server may still be sending them when
     * preload runs, so look for a while before creating the status display (as Shortbow does).
     * Discs left by an instance that died without unloading are deleted.
     * @param {number} deadline
     */
    findChildren(deadline) {
        this.searchTimer = null;
        Entities.getChildrenIDs(this.entityID).forEach(id => {
            const name = Entities.getEntityProperties(id, ["name"]).name || "";
            if (name === STATUS_NAME && (this.statusID === null || this.statusID === id)) {
                this.statusID = id;
            } else if (name === STATUS_NAME || name.startsWith(DISC_PREFIX)) {
                Entities.deleteEntity(id);
            }
        });
        if (this.statusID === null && Date.now() < deadline) {
            this.searchTimer = Script.setTimeout(() => this.findChildren(deadline), CHILD_POLL_MS);
            return;
        }
        if (this.statusID === null) {
            this.statusID = Entities.addEntity({
                type: "Text",
                name: STATUS_NAME,
                parentID: this.entityID,
                localPosition: { x: 0, y: this.dimensions.y / 2 + STATUS_GAP + STATUS_SIZE.y / 2, z: 0 },
                dimensions: STATUS_SIZE,
                font: "Inconsolata",
                lineHeight: STATUS_LINE_HEIGHT,
                textColor: { red: 240, green: 240, blue: 240 },
                backgroundColor: { red: 20, green: 24, blue: 32 },
                backgroundAlpha: 0.85,
                unlit: true,
                billboardMode: "yaw",
                collisionless: true,
                grab: { grabbable: false }
            });
        }
        this.ready = true;
        this.draw();
    }

    /** Brings the Text child and the disc entities in line with the latest view. */
    draw() {
        if (!this.ready || !this.view || !this.statusID) {
            return;
        }
        const { text, board, line } = this.view;
        if (text !== this.shownText) {
            Entities.editEntity(this.statusID, { text });
            this.shownText = text;
        }
        const winning = new Set((line || []).map(([row, col]) => "r" + row + "c" + col));
        for (let row = 0; row < ROWS; row++) {
            for (let col = 0; col < COLUMNS; col++) {
                const cell = "r" + row + "c" + col;
                const piece = board[row][col];
                const look = piece === EMPTY ? "" : piece + (winning.has(cell) ? "!" : "");
                const disc = this.discs.get(cell);
                if ((disc ? disc.look : "") === look) {
                    continue;
                }
                if (disc) {
                    Entities.deleteEntity(disc.id);
                    this.discs.delete(cell);
                }
                if (look) {
                    this.discs.set(cell, { id: this.addDisc(row, col, piece, winning.has(cell)), look });
                }
            }
        }
    }

    /**
     * @param {number} row 0 is the top row
     * @param {number} col
     * @param {string} piece
     * @param {boolean} winning
     * @returns {Uuid}
     */
    addDisc(row, col, piece, winning) {
        const pitchX = this.dimensions.x / COLUMNS;
        const pitchY = this.dimensions.y / ROWS;
        const diameter = DISC_FILL * Math.min(pitchX, pitchY) * (winning ? WIN_SCALE : 1);
        return Entities.addEntity({
            type: "Model",
            name: DISC_PREFIX + "r" + row + "c" + col,
            parentID: this.entityID,
            modelURL: DISC_MODELS[/** @type {"R" | "B"} */ (piece)],
            localPosition: { x: (col - (COLUMNS - 1) / 2) * pitchX, y: ((ROWS - 1) / 2 - row) * pitchY, z: 0 },
            localRotation: DISC_ROTATION,
            dimensions: { x: diameter, y: this.dimensions.z * DISC_DEPTH, z: diameter },
            collisionless: true,
            dynamic: false,
            grab: { grabbable: false }
        });
    }
}
