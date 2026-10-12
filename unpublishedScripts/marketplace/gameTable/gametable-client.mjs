//
//  gametable-client.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Game Table client entity script, run by Interface for each visitor. One class, with a
//  role taken from the entity's name:
//
//      GT.Col<n>   a column target: a click asks the referee to move into column n
//      GT.Seat<n>  a seat button: a click joins seat n, or leaves it when already seated there
//                  (during a game, which resigns it, only on a second click within 5 s)
//      (other)     the board itself: listens on the board's channel, announces what concerns
//                  this user, and highlights the legal columns while it is their turn
//
//  The client holds no game state of its own: it reads the board's userData and the channel,
//  never writes either, and edits only the local entities it creates for the highlights. Channel
//  messages are self-asserted (any client can send one), so they only drive what this user is
//  shown; every action goes to the referee, which checks the caller.
//

import { channelFor } from "./referee-core.mjs";
import { gameEntry } from "./game-registry.mjs";

const COLUMN = /^GT\.Col([0-6])$/;
const SEAT = /^GT\.Seat([01])$/;
const HIGHLIGHT_PREFIX = "GT.Highlight ";
const HIGHLIGHT_GROW = 1.15;
const HIGHLIGHT_COLOR = { red: 60, green: 220, blue: 90 };
const HIGHLIGHT_ALPHA = 0.45;
const HIGHLIGHT_RETRY_MS = 1000;
const RESIGN_CONFIRM_MS = 5000;
const RESIGN_CONFIRM_TEXT = "Click your seat again within 5 s to resign the game";
const NOT_SEATED_TEXT = "You are not seated at the game table";
const OFFLINE_TEXT = "Game table: the referee is not running";
/** @type {Record<string, string>} */
const METHOD_TEXT = {
    join: "Seat refused", move: "Move refused", leave: "Leave refused", newGame: "New game refused", draw: "Draw offer refused"
};
/** @type {Record<string, string>} */
const REASON_TEXT = {
    four: "four in a row", timeout: "on time", resign: "by resignation", checkmate: "checkmate", "no moves": "no moves left",
    agreement: "by agreement", repetition: "threefold repetition", "forty-move": "forty-move rule",
    "fifty-move": "fifty-move rule", stalemate: "stalemate", material: "insufficient material"
};

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

/**
 * Session IDs come braced from the engine; compare them without braces or case.
 * @param {unknown} a
 * @param {unknown} b
 * @returns {boolean}
 */
function sameSession(a, b) {
    const bare = (/** @type {unknown} */ id) => typeof id === "string" ? id.replace(/[{}]/g, "").toLowerCase() : "";
    return bare(a) !== "" && bare(a) === bare(b);
}

/**
 * What the table means for this user.
 * @param {Record<string, any>} state a `state` message or the userData projection
 * @param {number} mine this user's seat, or -1
 * @returns {string}
 */
function statusFor(state, mine) {
    const SEAT_NAMES = gameEntry(state.game).entry.rules.SEAT_NAMES;
    if (state.phase === "offline") {
        return OFFLINE_TEXT;
    }
    if (mine < 0) {
        return NOT_SEATED_TEXT;
    }
    const seats = /** @type {({ name: string } | null)[]} */ (state.seats);
    if (state.phase === "playing") {
        const offer = state.drawOffer === 1 - mine ? "; " + SEAT_NAMES[1 - mine] + " offers a draw"
            : state.drawOffer === mine ? "; your draw offer stands" : "";
        if (state.toMove === mine) {
            return "Your turn (" + SEAT_NAMES[mine] + ")" + offer;
        }
        const opponent = seats[state.toMove];
        return "Waiting for " + (opponent ? opponent.name : SEAT_NAMES[state.toMove]) + " to move" + offer;
    }
    if (state.phase === "over" && state.result) {
        const { winner, reason } = state.result;
        if (winner === null) {
            if (reason === "abandoned") {
                return "Game abandoned";
            }
            return reason === "draw" ? "Draw" : "Draw (" + (REASON_TEXT[reason] || reason) + ")";
        }
        return (winner === mine ? "You win" : "You lose") + " (" + (REASON_TEXT[reason] || reason) + ")";
    }
    return "Waiting for an opponent";
}

export default class GameTableClient {
    // Set in preload, which the engine calls before any other method.
    /** @type {Uuid} */ entityID;
    /** @type {Uuid} */ boardID;
    /** @type {"board" | "column" | "seat"} */ role;
    index = -1;
    channel = "";
    /** @type {((channel: string, message: string) => void) | null} */ onMessage = null;
    shown = "";
    seated = false;
    /** @type {Map<number, { id: Uuid, target: Uuid }>} column -> local highlight and its target */
    highlights = new Map();
    /** @type {number[]} */ wanted = [];
    /** @type {number | null} */ retryTimer = null;
    resignUntil = 0;

    /** @param {Uuid} entityID */
    preload(entityID) {
        this.entityID = entityID;
        const properties = Entities.getEntityProperties(entityID, ["name", "parentID"]);
        const name = properties.name || "";
        const column = COLUMN.exec(name);
        const seat = SEAT.exec(name);
        if (column || seat) {
            this.role = column ? "column" : "seat";
            this.index = Number((column || seat || [])[1]);
            this.boardID = /** @type {Uuid} */ (properties.parentID);
        } else {
            this.role = "board";
            this.boardID = entityID;
            this.listen();
        }
    }

    unload() {
        if (this.onMessage) {
            Messages.messageReceived.disconnect(this.onMessage);
            Messages.unsubscribe(this.channel);
            this.onMessage = null;
        }
        if (this.retryTimer !== null) {
            Script.clearTimeout(this.retryTimer);
            this.retryTimer = null;
        }
        this.highlights.forEach(shown => Entities.deleteEntity(shown.id));
        this.highlights.clear();
    }

    /** @param {Uuid} _id @param {PointerEvent} event */
    clickDownOnEntity(_id, event) {
        if (event.isPrimaryButton) {
            this.press();
        }
    }

    startNearTrigger() {
        this.press();
    }

    startFarTrigger() {
        this.press();
    }

    /** Asks the referee to act. It checks who is asking, so nothing here is trusted by it. */
    press() {
        if (this.role === "column") {
            Entities.callEntityServerMethod(this.boardID, "move", [String(this.index)]);
        } else if (this.role === "seat") {
            const state = this.boardState();
            const holder = state && Array.isArray(state.seats) ? state.seats[this.index] : null;
            if (!holder || !sameSession(holder.session, MyAvatar.sessionUUID)) {
                Entities.callEntityServerMethod(this.boardID, "join",
                    [MyAvatar.sessionDisplayName || MyAvatar.displayName, String(this.index)]);
            } else if (state && state.phase === "playing" && Date.now() > this.resignUntil) {
                this.resignUntil = Date.now() + RESIGN_CONFIRM_MS;
                Window.displayAnnouncement(RESIGN_CONFIRM_TEXT);
            } else {
                this.resignUntil = 0;
                Entities.callEntityServerMethod(this.boardID, "leave", []);
            }
        }
    }

    /** @returns {Record<string, any> | null} the referee's projection in the board's userData */
    boardState() {
        const state = parseObject(Entities.getEntityProperties(this.boardID, ["userData"]).userData).gameTable;
        return state !== null && typeof state === "object" ? state : null;
    }

    /**
     * Subscribes to the board's channel. The referee writes its name into userData; a board it
     * cannot write (locked, or never refereed yet) uses the same name. The state is asked for
     * once when userData has none, or one that looks stale: offline, or a turn already past its
     * deadline (the referee may have restarted on a board it cannot write).
     */
    listen() {
        const state = this.boardState();
        this.channel = state && typeof state.channel === "string" ? state.channel : channelFor(this.boardID);
        this.onMessage = (channel, message) => {
            if (channel === this.channel) {
                this.receive(message);
            }
        };
        Messages.messageReceived.connect(this.onMessage);
        Messages.subscribe(this.channel);
        if (state) {
            this.update(state);
        }
        const overdue = state && state.phase === "playing" && typeof state.turnDeadline === "number"
            && state.turnDeadline < Date.now();
        if (!state || state.phase === "offline" || overdue) {
            Entities.callEntityServerMethod(this.boardID, "state", []);
        }
    }

    /** @param {string} text */
    receive(text) {
        const message = parseObject(text);
        if (message.type === "state") {
            this.update(message);
        } else if (message.type === "rejected" && sameSession(message.to, MyAvatar.sessionUUID)) {
            Window.displayAnnouncement((METHOD_TEXT[message.method] || message.method + " refused") + ": " + message.reason);
        }
    }

    /**
     * Announces a change of what the table means for this user, while they are seated or just
     * after they lose their seat; spectators are not told about every move.
     * @param {Record<string, any>} state
     */
    update(state) {
        const seats = Array.isArray(state.seats) ? state.seats : [];
        const mine = seats.findIndex(seat => seat && sameSession(seat.session, MyAvatar.sessionUUID));
        const status = statusFor({ ...state, seats }, mine);
        if (status !== this.shown && (mine >= 0 || this.seated)) {
            Window.displayAnnouncement(status);
        }
        this.shown = status;
        this.seated = mine >= 0;
        const myTurn = state.phase === "playing" && mine >= 0 && state.toMove === mine;
        this.highlight(myTurn && Array.isArray(state.legal) ? state.legal : []);
    }

    /**
     * Shows a local box around each column target in `columns` and removes the others. Local
     * entities are drawn for this user only and need no edit rights; the column targets are
     * domain entities, so editing their colour would change it for everyone, if allowed at all.
     * @param {number[]} columns
     */
    highlight(columns) {
        this.wanted = columns;
        const wanted = new Set(columns);
        this.highlights.forEach((shown, col) => {
            // A target the referee deleted (a duplicate it swept) takes the highlight's anchor with it
            const targetGone = !Entities.getEntityProperties(shown.target, ["name"]).name;
            if (!wanted.has(col) || targetGone) {
                Entities.deleteEntity(shown.id);
                this.highlights.delete(col);
            }
        });
        if (!columns.every(col => this.highlights.has(col))) {
            this.addHighlights(wanted);
        }
        if (this.retryTimer !== null) {
            Script.clearTimeout(this.retryTimer);
            this.retryTimer = null;
        }
        // The column targets can reach this client after the board's state does: look again
        if (!columns.every(col => this.highlights.has(col))) {
            this.retryTimer = Script.setTimeout(() => {
                this.retryTimer = null;
                this.highlight(this.wanted);
            }, HIGHLIGHT_RETRY_MS);
        }
    }

    /** @param {Set<number>} wanted */
    addHighlights(wanted) {
        Entities.getChildrenIDs(this.boardID).forEach(id => {
            const target = /** @type {Partial<Entities.EntityProperties_Box>} */ (
                Entities.getEntityProperties(id, ["name", "dimensions"]));
            const column = COLUMN.exec(target.name || "");
            const col = column ? Number(column[1]) : -1;
            if (!wanted.has(col) || this.highlights.has(col)) {
                return;
            }
            const size = /** @type {Vec3} */ (target.dimensions);
            this.highlights.set(col, { target: id, id: Entities.addEntity({
                type: "Box",
                name: HIGHLIGHT_PREFIX + col,
                parentID: id,
                localPosition: { x: 0, y: 0, z: 0 },
                dimensions: { x: size.x * HIGHLIGHT_GROW, y: size.y * HIGHLIGHT_GROW, z: size.z * HIGHLIGHT_GROW },
                color: HIGHLIGHT_COLOR,
                alpha: HIGHLIGHT_ALPHA,
                collisionless: true,
                // The click must reach the column target inside, not the highlight
                ignorePickIntersection: true,
                grab: { grabbable: false }
            }, "local") });
        });
    }
}
