//
//  referee-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Connect Four referee without the engine: seats, turns, clock and the published
//  projection. Every side effect goes through an injected world adapter, so the core runs
//  under Node with a fake world and a fake clock:
//
//      now()             -> epoch milliseconds
//      publish(state)    -> the projection changed: store it where readers look and send it
//      render(view)      -> { text, board, line }: redraw the status text and the discs
//      announce(event)   -> { type: "state" | "rejected" | "over" | "expired", ... }: send it to the
//                           players only; nothing is stored
//
//  Callers are identified only by the `caller` argument, which the serverScripts entry takes
//  from Script.remoteCallerID (stamped by the entity script server from the authenticated
//  node). Nothing in a method's string arguments can name or impersonate a seat.
//

import { PIECES, newBoard, legalMoves, parseColumn, applyMove, winner, isDraw, render } from "./connect4.mjs";

export const PROTOCOL_VERSION = 1;
export const GAME = "connect4";
export const SEAT_NAMES = ["Red", "Black"];
export const DEFAULT_TIMEOUTS = { turnTimeoutS: 180, waitTimeoutS: 600, overIdleS: 300 };
export const NAME_LIMIT = 32;

const MIN_TIMEOUT_S = 5;
const MAX_TIMEOUT_S = 86400;
const COUNTDOWN_STEP_S = 5;
const NULL_ID = /^\{?0{8}-0{4}-0{4}-0{4}-0{12}\}?$/;
/** @type {Record<string, string>} */
const REASON_TEXT = {
    four: "four in a row",
    timeout: "out of time",
    resign: "resigned"
};

/**
 * @param {string} tableId
 * @returns {string} the Messages channel a table publishes on
 */
export function channelFor(tableId) {
    return "gametable:" + tableId;
}

/**
 * @param {unknown} value
 * @param {number} fallback
 * @returns {number}
 */
function seconds(value, fallback) {
    return typeof value === "number" && Number.isFinite(value) && value >= MIN_TIMEOUT_S && value <= MAX_TIMEOUT_S
        ? value : fallback;
}

/**
 * @param {Result | null} result
 * @returns {Result | null} a copy that shares no arrays with the referee's state
 */
function copyResult(result) {
    return result && { ...result, line: result.line && result.line.map(cell => cell.slice()) };
}

/**
 * @param {unknown} caller
 * @returns {caller is string}
 */
function isCaller(caller) {
    return typeof caller === "string" && caller !== "" && !NULL_ID.test(caller);
}

/**
 * @param {unknown} name
 * @param {string} caller
 * @returns {string}
 */
function cleanName(name, caller) {
    const cleaned = (typeof name === "string" ? name : "").replace(/[\u0000-\u001f\u007f]/g, "").trim();
    return cleaned.slice(0, NAME_LIMIT) || "Player " + caller.replace(/[{}]/g, "").slice(0, 8);
}

/**
 * @typedef {{ session: string, name: string, joinedAt: number }} Seat
 * @typedef {{ winner: number | null, reason: "four" | "draw" | "timeout" | "resign" | "abandoned",
 *     line: number[][] | null }} Result
 * @typedef {{ seat: number, col: number, row: number }} Move
 * @typedef {"waiting" | "playing" | "over" | "offline"} Phase
 * @typedef {{ type: string } & Record<string, unknown>} Announcement
 * @typedef {{ now(): number, publish(state: object): void,
 *     render(view: { text: string, board: string[], line: number[][] | null }): void,
 *     announce(event: Announcement): void }} World
 */

export class RefereeCore {
    // Game fields; resetTable() puts them back to these values.
    board = newBoard();
    toMove = 0;
    moveNo = 0;
    /** @type {Move[]} */ history = [];
    /** @type {Result | null} */ result = null;
    /** @type {string | null} */ gameId = null;
    /** @type {number | null} */ turnDeadline = null;
    /** @type {number | null} */ seatDeadline = null;
    /** @type {number | null} */ idleDeadline = null;

    /**
     * @param {World} world
     * @param {{ tableId?: string, turnTimeoutS?: unknown, waitTimeoutS?: unknown, overIdleS?: unknown }} [options]
     */
    constructor(world, options = {}) {
        this.world = world;
        this.tableId = options.tableId || "";
        this.channel = channelFor(this.tableId);
        this.turnTimeoutMs = seconds(options.turnTimeoutS, DEFAULT_TIMEOUTS.turnTimeoutS) * 1000;
        this.waitTimeoutMs = seconds(options.waitTimeoutS, DEFAULT_TIMEOUTS.waitTimeoutS) * 1000;
        this.overIdleMs = seconds(options.overIdleS, DEFAULT_TIMEOUTS.overIdleS) * 1000;

        /** @type {Phase} */
        this.phase = "waiting";
        /** @type {(Seat | null)[]} */
        this.seats = [null, null];
        this.firstSeat = 0;
        this.lastText = "";
    }

    /** Clears the board and the last result; seats are left to the caller. */
    resetTable() {
        this.board = newBoard();
        this.toMove = 0;
        this.moveNo = 0;
        this.history = [];
        this.result = null;
        this.gameId = null;
        this.turnDeadline = null;
        this.seatDeadline = null;
        this.idleDeadline = null;
    }

    /** @private Empties the table: no seats, a clear board, and Red moves first in the next game. */
    clearTable() {
        this.seats = [null, null];
        this.phase = "waiting";
        this.firstSeat = 0;
        this.resetTable();
    }

    /**
     * First publication after the script (re)loads. `previous` is the projection found in the
     * board's userData, untrusted: a game it shows in progress died with the old instance.
     * @param {any} [previous]
     */
    start(previous) {
        if (previous && previous.phase === "playing") {
            this.world.announce({
                type: "over",
                gameId: typeof previous.gameId === "string" ? previous.gameId : null,
                result: { winner: null, reason: "abandoned", line: null },
                seats: [null, null]
            });
        }
        this.changed();
    }

    /**
     * @param {string | undefined} caller
     * @param {string} [name] cosmetic; the entity script server cannot look display names up
     * @returns {boolean} whether the call was accepted
     */
    join(caller, name) {
        this.expire();
        if (!isCaller(caller)) {
            return this.reject(caller, "join", "no caller id");
        }
        if (this.seatOf(caller) >= 0) {
            this.world.announce({ type: "state", ...this.projection() });
            return true;
        }
        if (this.phase !== "waiting") {
            return this.reject(caller, "join", "table full");
        }
        const seat = this.seats[0] === null ? 0 : 1;
        this.seats[seat] = { session: caller, name: cleanName(name, caller), joinedAt: this.world.now() };
        if (this.seats.every(Boolean)) {
            this.startGame();
        } else {
            this.seatDeadline = this.world.now() + this.waitTimeoutMs;
        }
        this.changed();
        return true;
    }

    /**
     * @param {string | undefined} caller
     * @param {string | undefined} column one digit 0-6
     * @returns {boolean}
     */
    move(caller, column) {
        this.expire();
        if (!isCaller(caller)) {
            return this.reject(caller, "move", "no caller id");
        }
        if (this.phase !== "playing") {
            return this.reject(caller, "move", this.phase === "over" ? "game over" : "game not started");
        }
        const seat = this.seatOf(caller);
        if (seat < 0) {
            return this.reject(caller, "move", "not seated");
        }
        if (seat !== this.toMove) {
            return this.reject(caller, "move", "not your turn");
        }
        const col = parseColumn(column);
        if (col === null) {
            return this.reject(caller, "move", "column must be one digit 0-6");
        }
        const played = applyMove(this.board, col, PIECES[seat]);
        if ("error" in played) {
            return this.reject(caller, "move", played.error);
        }
        this.board = played.board;
        this.moveNo++;
        this.history.push({ seat, col, row: played.row });
        const won = winner(this.board);
        if (won) {
            this.end(seat, "four", won.line);
        } else if (isDraw(this.board)) {
            this.end(null, "draw", null);
        } else {
            this.toMove = 1 - seat;
            this.turnDeadline = this.world.now() + this.turnTimeoutMs;
            this.changed();
        }
        return true;
    }

    /**
     * Resigns a game in progress, then frees the caller's seat. The final board and result stay
     * on show while the table waits for a new opponent.
     * @param {string | undefined} caller
     * @returns {boolean}
     */
    leave(caller) {
        this.expire();
        if (!isCaller(caller)) {
            return this.reject(caller, "leave", "no caller id");
        }
        const seat = this.seatOf(caller);
        if (seat < 0) {
            return this.reject(caller, "leave", "not seated");
        }
        // One transition and one publication: the result is announced with the seats as played
        const over = this.phase === "playing" ? this.finish(1 - seat, "resign", null) : null;
        this.seats[seat] = null;
        if (this.seats.every(s => s === null)) {
            this.clearTable();
        } else {
            this.phase = "waiting";
            this.idleDeadline = null;
            this.seatDeadline = this.world.now() + this.waitTimeoutMs;
        }
        this.changed();
        if (over) {
            this.world.announce(over);
        }
        return true;
    }

    /**
     * Sends the current state on the channel, for a player that has just subscribed. Nothing is
     * stored, so a caller cannot turn this into a stream of entity edits.
     * @returns {boolean}
     */
    state() {
        this.expire();
        this.world.announce({ type: "state", ...this.projection() });
        return true;
    }

    /**
     * A rematch with the same seats; the first move alternates between games.
     * @param {string | undefined} caller
     * @returns {boolean}
     */
    newGame(caller) {
        this.expire();
        if (!isCaller(caller)) {
            return this.reject(caller, "newGame", "no caller id");
        }
        if (this.phase !== "over") {
            return this.reject(caller, "newGame", "game not over");
        }
        if (this.seatOf(caller) < 0) {
            return this.reject(caller, "newGame", "not seated");
        }
        this.startGame();
        this.changed();
        return true;
    }

    /** Called about once a second: the deadlines, then the countdown. */
    tick() {
        if (!this.expire() && this.statusText() !== this.lastText) {
            this.draw();
        }
    }

    /**
     * The script is unloading. A game in progress is abandoned; the table is published as
     * offline so readers do not take it for a live game.
     */
    stop() {
        const over = this.phase === "playing" ? this.finish(null, "abandoned", null) : null;
        this.phase = "offline";
        this.world.publish(this.projection());
        if (over) {
            this.world.announce(over);
        }
    }

    /**
     * Applies a deadline that has passed. Every call runs this first, so a move that arrives
     * after the deadline but before the next tick is refused.
     * @private
     * @returns {boolean} whether anything changed
     */
    expire() {
        const now = this.world.now();
        if (this.phase === "playing" && this.turnDeadline !== null && now >= this.turnDeadline) {
            this.end(1 - this.toMove, "timeout", null);
        } else if (this.phase === "waiting" && this.seatDeadline !== null && now >= this.seatDeadline) {
            const seat = this.seats.findIndex(Boolean);
            const lone = this.seats[seat];
            this.clearTable();
            this.changed();
            if (lone) {
                this.world.announce({ type: "expired", seat, session: lone.session, name: lone.name });
            }
        } else if (this.phase === "over" && this.idleDeadline !== null && now >= this.idleDeadline) {
            this.clearTable();
            this.changed();
        } else {
            return false;
        }
        return true;
    }

    /** @returns {object} the state readers see, in userData and on the channel */
    projection() {
        const playing = this.phase === "playing";
        return {
            v: PROTOCOL_VERSION,
            game: GAME,
            gameId: this.gameId,
            phase: this.phase,
            seats: this.seatList(),
            toMove: playing ? this.toMove : null,
            moveNo: this.moveNo,
            rows: this.board.slice(),
            legal: playing ? legalMoves(this.board) : [],
            last: this.history.length ? { ...this.history[this.history.length - 1] } : null,
            result: copyResult(this.result),
            turnDeadline: playing ? this.turnDeadline : null,
            channel: this.channel
        };
    }

    /** @returns {string} the status board, monospace, as shown on the table's Text child */
    statusText() {
        const names = this.seats.map((seat, i) => SEAT_NAMES[i] + ": " + (seat ? seat.name : "(empty)"));
        return [names.join("   "), this.statusLine(), render(this.board)].join("\n");
    }

    /** @private */
    statusLine() {
        if (this.phase === "playing") {
            const left = Math.max(0, Math.ceil(((this.turnDeadline ?? 0) - this.world.now()) / 1000));
            const shown = Math.ceil(left / COUNTDOWN_STEP_S) * COUNTDOWN_STEP_S;
            return `${SEAT_NAMES[this.toMove]} to move (move ${this.moveNo + 1}, ${shown} s)`;
        }
        if (this.phase === "over" && this.result) {
            return this.resultLine(this.result) + "; newGame to play again";
        }
        const seated = this.seats.filter(Boolean).length;
        const waiting = seated === 0 ? "Waiting for players" : "Waiting for an opponent";
        return this.result ? this.resultLine(this.result) + ". " + waiting : waiting;
    }

    /**
     * @private
     * @param {Result} result
     */
    resultLine(result) {
        if (result.winner === null) {
            return result.reason === "draw" ? "Draw" : "Game abandoned";
        }
        const loser = SEAT_NAMES[1 - result.winner];
        const why = result.reason === "four" ? REASON_TEXT.four : loser + " " + REASON_TEXT[result.reason];
        return `${SEAT_NAMES[result.winner]} wins (${why})`;
    }

    /**
     * @private
     * @param {string} caller
     * @returns {number} the caller's seat, or -1
     */
    seatOf(caller) {
        return this.seats.findIndex(seat => seat !== null && seat.session === caller);
    }

    /** @private */
    startGame() {
        const now = this.world.now();
        this.resetTable();
        this.phase = "playing";
        this.gameId = this.tableId + ":" + now;
        this.toMove = this.firstSeat;
        this.firstSeat = 1 - this.firstSeat;
        this.turnDeadline = now + this.turnTimeoutMs;
    }

    /**
     * Ends the game and publishes it.
     * @private
     * @param {number | null} winnerSeat
     * @param {Result["reason"]} reason
     * @param {number[][] | null} line
     */
    end(winnerSeat, reason, line) {
        const over = this.finish(winnerSeat, reason, line);
        this.changed();
        this.world.announce(over);
    }

    /**
     * Ends the game without publishing it.
     * @private
     * @param {number | null} winnerSeat
     * @param {Result["reason"]} reason
     * @param {number[][] | null} line
     * @returns {Announcement} the "over" announcement, naming the seats as they played
     */
    finish(winnerSeat, reason, line) {
        this.phase = "over";
        this.result = { winner: winnerSeat, reason, line };
        this.turnDeadline = null;
        this.idleDeadline = this.world.now() + this.overIdleMs;
        return { type: "over", gameId: this.gameId, result: copyResult(this.result), seats: this.seatList() };
    }

    /** @private */
    seatList() {
        return this.seats.map(seat => seat && { session: seat.session, name: seat.name });
    }

    /**
     * @private
     * @param {unknown} caller
     * @param {string} method
     * @param {string} reason
     * @returns {false}
     */
    reject(caller, method, reason) {
        this.world.announce({ type: "rejected", to: typeof caller === "string" ? caller : "", method, reason });
        return false;
    }

    /** @private */
    changed() {
        this.world.publish(this.projection());
        this.draw();
    }

    /** @private */
    draw() {
        this.lastText = this.statusText();
        this.world.render({ text: this.lastText, board: this.board.slice(), line: copyResult(this.result)?.line ?? null });
    }
}
