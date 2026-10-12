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
//  channel "gametable:<board id>", and draws the pieces on the board. It also creates the
//  children humans click, which run gametable-client.mjs. The game is userData's
//  gameTableConfig.game, read once at load (game-registry.mjs); its layout module places the
//  children and draws the pieces.
//
//  The engine shares one module instance per URL between every board that uses it, so all
//  state lives on the class instance; module scope holds constants only.
//

import { RefereeCore, channelFor } from "./referee-core.mjs";
import { gameEntry } from "./game-registry.mjs";
import { Connect4Layout, STATUS_NAME } from "./layout-connect4.mjs";
import { GridLayout } from "./layout-grid.mjs";

const HERE = import.meta.url.replace(/[^/]*$/, "");
const TICK_MS = 1000;
const CHILD_SEARCH_MS = 3000;
const CHILD_POLL_MS = 500;
// Every child the referee makes is named so; one it does not recognise is left from another game.
const CHILD_PREFIX = "GT.";
const IDENTITY = { x: 0, y: 0, z: 0, w: 1 };
const OFFLINE_TEXT = "Game table\nReferee not running";
const LOCKED_TEXT = "BOARD LOCKED: the game is not saved in userData. Unlock the board and reload its script.";

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
    remotelyCallable = ["join", "move", "leave", "state", "newGame", "draw"];

    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {string} */ channel;
    /** @type {Vec3} */ dimensions;
    /** @type {RefereeCore} */ core;
    /** @type {Connect4Layout | GridLayout} */ layout;
    /** @type {number} */ timer;
    /** @type {number | null} */ searchTimer = null;
    /** @type {Map<string, Uuid>} child name -> the one child of that name */ fixed = new Map();
    /** @type {{ text: string, board: string[], line: number[][] | null, move: unknown } | null} */ view = null;
    shownText = "";
    locked = false;

    /** @returns {Uuid | null} the status Text child, once found or created */
    get statusID() {
        return this.fixed.get(STATUS_NAME) ?? null;
    }

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        this.channel = channelFor(entityID);
        const properties = /** @type {Partial<Entities.EntityProperties_Box>} */ (
            Entities.getEntityProperties(entityID, ["userData", "dimensions", "locked"]));
        this.dimensions = /** @type {Vec3} */ (properties.dimensions);
        // A locked entity takes no edit but to `locked`, so the state could not be stored.
        this.locked = properties.locked === true;
        if (this.locked) {
            print("Game table " + entityID + ": " + LOCKED_TEXT);
        }
        const userData = parseObject(properties.userData);
        const config = userData.gameTableConfig || {};
        const game = gameEntry(config.game);
        if (!game.known) {
            print("Game table " + entityID + ": unknown game " + JSON.stringify(config.game) + ", playing " + game.name);
        }
        const entry = game.entry;
        this.layout = entry.layout === "grid" ? new GridLayout(entityID, this.dimensions, HERE, entry)
            : new Connect4Layout(entityID, this.dimensions, HERE);
        this.core = new RefereeCore({
            now: () => Date.now(),
            publish: state => this.publish(state),
            render: view => {
                this.view = view;
                this.redraw();
            },
            announce: event => this.announce(event)
        }, { tableId: entityID, rules: entry.rules, turnTimeoutS: config.turnTimeoutS, waitTimeoutS: config.waitTimeoutS });
        this.core.start(userData.gameTable);
        this.timer = Script.setInterval(() => this.core.tick(), TICK_MS);
        this.findChildren(Date.now() + CHILD_SEARCH_MS);
    }

    unload() {
        this.core.stop();
        Script.clearInterval(this.timer);
        if (this.searchTimer !== null) {
            Script.clearTimeout(this.searchTimer);
        }
        this.layout.clear();
        if (this.statusID) {
            Entities.editEntity(this.statusID, { text: OFFLINE_TEXT });
        }
    }

    // Remotely callable. The engine passes (entityID, params: string[]) and stamps the
    // caller's authenticated session id in Script.remoteCallerID for the synchronous part only.

    /** @param {Uuid} _id @param {string[]} params [name, seat?] */
    join(_id, params) {
        const caller = Script.remoteCallerID;
        this.core.join(caller, params[0], params[1]);
    }

    /** @param {Uuid} _id @param {string[]} params [move] in the game's notation */
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

    draw() {
        const caller = Script.remoteCallerID;
        this.core.draw(caller);
    }

    /** @param {object} state */
    publish(state) {
        if (!this.locked) {
            const userData = parseObject(Entities.getEntityProperties(this.entityID, ["userData"]).userData);
            userData.gameTable = state;
            Entities.editEntity(this.entityID, { userData: JSON.stringify(userData) });
        }
        Messages.sendMessage(this.channel, JSON.stringify({ type: "state", ...state }));
    }

    /** @param {{ type: string }} event */
    announce(event) {
        const message = JSON.stringify(event);
        Messages.sendMessage(this.channel, message);
        if (event.type !== "state") {
            print("Game table " + this.entityID + ": " + message);
        }
    }

    /**
     * Children persist with the board, and the entity server may still be sending them when
     * preload runs, so sweep them for a while (as Shortbow does): adopt the layout's fixed
     * children (the status display, the click targets and the seat buttons), delete any
     * duplicate of them, any piece this instance did not create (left by one that died without
     * unloading) and any other "GT." child (left by another game). Each fixed child is created
     * only if none turned up. An adopted child is brought back to the current layout, look and
     * client script URL, so a resized board, a moved asset host or a change of game leaves
     * nothing stale.
     * @param {number} deadline
     */
    findChildren(deadline) {
        this.searchTimer = null;
        const ours = this.layout.pieceIDs();
        const fixedNames = new Set(this.layout.fixedNames);
        Entities.getChildrenIDs(this.entityID).forEach(id => {
            const name = Entities.getEntityProperties(id, ["name"]).name || "";
            if (fixedNames.has(name)) {
                const adopted = this.fixed.get(name);
                if (adopted === undefined) {
                    this.fixed.set(name, id);
                    const { type, name: _name, parentID, ...current } =
                        /** @type {Partial<Entities.EntityProperties_Box>} */ (this.layout.fixedProperties(name));
                    // A layout that sets no rotation means none, whatever another game's layout set
                    Entities.editEntity(id, { localRotation: IDENTITY, ...current });
                } else if (adopted !== id) {
                    Entities.deleteEntity(id);
                }
            } else if (name.startsWith(CHILD_PREFIX) && !ours.has(id)) {
                Entities.deleteEntity(id);
            }
        });
        if (Date.now() < deadline) {
            this.searchTimer = Script.setTimeout(() => this.findChildren(deadline), CHILD_POLL_MS);
        } else {
            this.layout.fixedNames.forEach(name => {
                if (!this.fixed.has(name)) {
                    this.fixed.set(name, Entities.addEntity(this.layout.fixedProperties(name)));
                }
            });
        }
        this.redraw();
    }

    /** Brings the Text child and the pieces in line with the latest view. */
    redraw() {
        if (!this.view) {
            return;
        }
        const text = this.locked ? LOCKED_TEXT + "\n" + this.view.text : this.view.text;
        if (this.statusID && text !== this.shownText) {
            Entities.editEntity(this.statusID, { text });
            this.shownText = text;
        }
        this.layout.draw(this.view);
    }
}
