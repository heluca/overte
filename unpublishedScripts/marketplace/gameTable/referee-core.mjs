//
//  referee-core.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The referee without the engine: seats, turns, clock and the published projection, for any
//  game whose rules module follows the contract connect4.mjs documents (the core never looks
//  at a board; the rules decide legality and the result). Every side effect goes through an injected world adapter, so the core runs
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

import * as connect4 from "./connect4.mjs";
import { statusText } from "./status-text.mjs";

export const PROTOCOL_VERSION = 1;
export const DEFAULT_TIMEOUTS = { turnTimeoutS: 180, waitTimeoutS: 600, overIdleS: 300 };
export const NAME_LIMIT = 32;
export const MOVE_TEXT_LIMIT = 64;

const MIN_TIMEOUT_S = 5;
const MAX_TIMEOUT_S = 86400;
const NULL_ID = /^\{?0{8}-0{4}-0{4}-0{4}-0{12}\}?$/;
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
 * @param {Move} move
 * @returns {{ seat: number, col: number, row: number }} Connect Four's `last`
 */
function lastDisc(move) {
    const [row, col] = /** @type {number[]} */ (move.to);
    return { seat: move.side, col, row };
}

/**
 * @param {Move} move
 * @returns {{ seat: number, move: string, san?: string }} the `last` of every other game
 */
function lastMove(move) {
    return move.san === undefined ? { seat: move.side, move: move.notation }
        : { seat: move.side, move: move.notation, san: move.san };
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
 * @typedef {{ winner: number | null, reason: string, line: number[][] | null }} Result
 * @typedef {{ side: number, notation: string, san?: string, from?: number[], to?: number[],
 *     path?: number[][], captured: unknown, promoted?: unknown }} Move
 * @typedef {{ GAME: string, SEAT_NAMES: string[], REMATCH: string, MOVE_HINT: string,
 *     newGame(firstSide?: number): any, sideToMove(state: any): number, legalMoves(state: any): string[],
 *     play(state: any, text: unknown): { state: any, move: Move } | { error: string },
 *     outcome(state: any): { winner: number | null, reason: string, line: number[][] | null } | null,
 *     rows(state: any): string[], render(state: any): string, extra(state: any): object }} Rules
 * @typedef {"waiting" | "playing" | "over" | "offline"} Phase
 * @typedef {{ type: string } & Record<string, unknown>} Announcement
 * @typedef {{ now(): number, publish(state: object): void,
 *     render(view: { text: string, board: string[], line: number[][] | null }): void,
 *     announce(event: Announcement): void }} World
 */

export class RefereeCore {
    // Game fields; resetTable() puts them back to these values.
    /** @type {any} the rules module's state, never mutated */ game;
    moveNo = 0;
    /** @type {Move[]} */ history = [];
    /** @type {Result | null} */ result = null;
    /** @type {string | null} */ gameId = null;
    /** @type {number | null} the seat whose draw offer stands */ drawOffer = null;
    /** @type {number | null} */ turnDeadline = null;
    /** @type {number | null} */ seatDeadline = null;
    /** @type {number | null} */ idleDeadline = null;

    /**
     * @param {World} world
     * @param {{ tableId?: string, rules?: Rules, turnTimeoutS?: unknown, waitTimeoutS?: unknown, overIdleS?: unknown }} [options]
     */
    constructor(world, options = {}) {
        this.world = world;
        /** @type {Rules} */
        this.rules = options.rules || connect4;
        this.game = this.rules.newGame(0);
        this.tableId = options.tableId || "";
        this.channel = channelFor(this.tableId);
        this.turnTimeoutMs = seconds(options.turnTimeoutS, DEFAULT_TIMEOUTS.turnTimeoutS) * 1000;
        this.waitTimeoutMs = seconds(options.waitTimeoutS, DEFAULT_TIMEOUTS.waitTimeoutS) * 1000;
        this.overIdleMs = seconds(options.overIdleS, DEFAULT_TIMEOUTS.overIdleS) * 1000;

        /** @type {Phase} */
        this.phase = "waiting";
        /** @type {(Seat | null)[]} */
        this.seats = [null, null];
        // Alternating games: the seat that moves first in the next game. Swapping games: whether
        // the next game swaps the seats, which it does once a game has been played at the table.
        this.firstSeat = 0;
        this.swapSeats = false;
        this.lastText = "";
    }

    /** Clears the board and the last result; seats are left to the caller. */
    resetTable() {
        this.game = this.rules.newGame(0);
        this.moveNo = 0;
        this.history = [];
        this.result = null;
        this.gameId = null;
        this.drawOffer = null;
        this.turnDeadline = null;
        this.seatDeadline = null;
        this.idleDeadline = null;
    }

    /** @returns {number} the side (and seat) to move */
    get toMove() {
        return this.rules.sideToMove(this.game);
    }

    /** @private Empties the table: no seats, a clear board, and seat 0 moves first in the next game. */
    clearTable() {
        this.seats = [null, null];
        this.phase = "waiting";
        this.firstSeat = 0;
        this.swapSeats = false;
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
     * @param {string} [seat] "0" or "1" for that seat; absent or "" for the first free one
     * @returns {boolean} whether the call was accepted
     */
    join(caller, name, seat) {
        this.expire();
        if (!isCaller(caller)) {
            return this.reject(caller, "join", "no caller id");
        }
        if (this.seatOf(caller) >= 0) {
            this.world.announce({ type: "state", ...this.projection() });
            return true;
        }
        if (seat !== undefined && seat !== "" && seat !== "0" && seat !== "1") {
            return this.reject(caller, "join", "seat must be 0 or 1");
        }
        if (this.phase !== "waiting") {
            return this.reject(caller, "join", "table full");
        }
        const chosen = seat === "0" || seat === "1" ? Number(seat) : this.seats[0] === null ? 0 : 1;
        if (this.seats[chosen] !== null) {
            return this.reject(caller, "join", "seat taken");
        }
        this.seats[chosen] = { session: caller, name: cleanName(name, caller), joinedAt: this.world.now() };
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
     * @param {string | undefined} text the move in the game's notation, untrusted
     * @returns {boolean}
     */
    move(caller, text) {
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
        const capped = typeof text === "string" ? text.slice(0, MOVE_TEXT_LIMIT) : "";
        const played = this.rules.play(this.game, capped);
        if ("error" in played) {
            return this.reject(caller, "move", played.error);
        }
        this.game = played.state;
        this.moveNo++;
        this.history.push(played.move);
        if (this.drawOffer === 1 - seat) {
            // Moving declines the opponent's offer
            this.drawOffer = null;
        }
        const over = this.rules.outcome(this.game);
        if (over) {
            this.end(over.winner, over.reason, over.line);
        } else {
            this.turnDeadline = this.world.now() + this.turnTimeoutMs;
            this.changed();
        }
        return true;
    }

    /**
     * Offers a draw, or accepts the opponent's standing offer, which ends the game drawn. An
     * offer stands until the game ends or the opponent moves; offering again is a no-op.
     * @param {string | undefined} caller
     * @returns {boolean}
     */
    draw(caller) {
        this.expire();
        if (!isCaller(caller)) {
            return this.reject(caller, "draw", "no caller id");
        }
        if (this.phase !== "playing") {
            return this.reject(caller, "draw", this.phase === "over" ? "game over" : "game not started");
        }
        const seat = this.seatOf(caller);
        if (seat < 0) {
            return this.reject(caller, "draw", "not seated");
        }
        if (this.drawOffer === 1 - seat) {
            this.end(null, "agreement", null);
        } else if (this.drawOffer === null) {
            this.drawOffer = seat;
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
     * A rematch with the same players: the first move alternates between games, or (REMATCH
     * "swap") the players swap seats.
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
            this.redraw();
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
        const legal = playing ? this.rules.legalMoves(this.game) : [];
        const last = this.history.length ? this.history[this.history.length - 1] : null;
        // Connect Four keeps the shape it was published with: columns as numbers, the last disc's cell
        const legacy = this.rules.GAME === connect4.GAME;
        return {
            v: PROTOCOL_VERSION,
            game: this.rules.GAME,
            gameId: this.gameId,
            phase: this.phase,
            seats: this.seatList(),
            toMove: playing ? this.toMove : null,
            moveNo: this.moveNo,
            rows: this.rules.rows(this.game),
            legal: legacy ? legal.map(Number) : legal,
            last: last && (legacy ? lastDisc(last) : lastMove(last)),
            result: copyResult(this.result),
            drawOffer: playing ? this.drawOffer : null,
            turnDeadline: playing ? this.turnDeadline : null,
            channel: this.channel,
            ...this.rules.extra(this.game)
        };
    }

    /** @returns {string} the status board, monospace, as shown on the table's Text child */
    statusText() {
        return statusText({
            names: this.rules.SEAT_NAMES,
            seats: this.seats,
            phase: this.phase,
            toMove: this.toMove,
            moveNo: this.moveNo,
            secondsLeft: Math.max(0, Math.ceil(((this.turnDeadline ?? 0) - this.world.now()) / 1000)),
            drawOffer: this.drawOffer,
            result: this.result,
            board: this.rules.render(this.game)
        });
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
        if (this.rules.REMATCH === "swap") {
            if (this.swapSeats) {
                this.seats.reverse();
            }
            this.swapSeats = true;
            this.game = this.rules.newGame(0);
        } else {
            this.game = this.rules.newGame(this.firstSeat);
            this.firstSeat = 1 - this.firstSeat;
        }
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
        this.drawOffer = null;
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
        this.redraw();
    }

    /** @private */
    redraw() {
        this.lastText = this.statusText();
        this.world.render({ text: this.lastText, board: this.rules.rows(this.game), line: copyResult(this.result)?.line ?? null });
    }
}
