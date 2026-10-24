//
//  gametable.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Game Table web module: a phone's UI for a refereed board (Connect Four, checkers, chess),
//  framed by a host page (Morpheus) in a sandboxed iframe from the board's userData.webModule.
//  The host is its whole world, over postMessage: `init` (the board, who you are, the methods
//  you may call, the channel), `userData` and channel `message`s in, `call`s out, answered by
//  `result`. Like the Interface client it keeps no game of its own: the board's userData and the
//  channel are shown, every action is a call the referee checks. Channel messages are
//  self-asserted, so they only change what is shown.
//

import { SIDES, flipped, moveEnds, moveLabel, movesFrom, movesTo, pieceSide } from "./board.mjs";

const RESIGN_CONFIRM_MS = 5000;
const PHASE_RANK = { offline: 0, waiting: 1, playing: 2, over: 3 };
const REASON_TEXT = {
    four: "four in a row", draw: "a full board", timeout: "on time", resign: "by resignation", checkmate: "checkmate",
    "no moves": "no moves left", agreement: "by agreement", repetition: "threefold repetition",
    "forty-move": "forty-move rule", "fifty-move": "fifty-move rule", stalemate: "stalemate",
    material: "insufficient material", abandoned: "abandoned"
};
const $ = (id) => document.getElementById(id);

const host = { nonce: null, ids: 0, pending: new Map() };
let board = { id: "", name: "" };
let you = { sessionUUID: "", displayName: "" };
let state = null;
let selection = null;
let choices = null;
let notice = "";
let resignUntil = 0;

const key = (id) => String(id || "").replace(/[{}]/g, "").toLowerCase();

function mySeat() {
    if (!state || !Array.isArray(state.seats)) { return null; }
    const seat = state.seats.findIndex((s) => s && key(s.session) === key(you.sessionUUID));
    return seat < 0 ? null : seat;
}

function myTurn() {
    return Boolean(state) && state.phase === "playing" && state.toMove === mySeat();
}

function sides() {
    return SIDES[state && state.game] || SIDES.connect4;
}

// --- the host -----------------------------------------------------------------------------

function send(message) {
    window.parent.postMessage({ ...message, nonce: host.nonce }, "*");
}

function call(method, args = []) {
    host.ids += 1;
    const id = "m" + host.ids;
    return new Promise((resolve) => {
        host.pending.set(id, resolve);
        send({ type: "call", id, method, args });
    }).then((result) => {
        if (!result.ok) { say(result.error === "rate limited" ? "One move at a time." : result.error); }
        return result;
    });
}

window.addEventListener("message", (event) => {
    // The host page is the only sender; anything else (another frame, an extension) is ignored.
    if (event.source !== window.parent || !event.data || typeof event.data !== "object") { return; }
    const message = event.data;
    if (message.type === "init") {
        if (host.nonce) { return; }
        host.nonce = message.nonce;
        board = { id: message.entity.id, name: message.entity.name };
        you = message.you;
        $("title").textContent = board.name || "Game table";
        offer(message.entity.userData, true);
        if (!message.channel || !message.channel.subscribed) {
            say("Live updates are off here; the board refreshes every few seconds.");
        }
        // As the Interface client: ask for the state when there is none, or it looks stale.
        if (!state || state.phase === "offline" || (state.phase === "playing" && state.turnDeadline < Date.now())) {
            call("state");
        }
        render();
        return;
    }
    if (!host.nonce) { return; }
    if (message.type === "result") {
        const resolve = host.pending.get(message.id);
        if (resolve) { host.pending.delete(message.id); resolve(message); }
    } else if (message.type === "userData") {
        offer(message.userData, true);
    } else if (message.type === "message") {
        heard(message.message);
    }
});

// --- the game -----------------------------------------------------------------------------

/** Take `next` unless it is older than what is shown (userData can trail the channel). */
function accept(next) {
    if (!next || typeof next !== "object" || !Array.isArray(next.rows)) { return false; }
    if (state && next.gameId === state.gameId) {
        const older = (next.moveNo ?? 0) < (state.moveNo ?? 0)
            || ((next.moveNo ?? 0) === (state.moveNo ?? 0) && (PHASE_RANK[next.phase] ?? 0) < (PHASE_RANK[state.phase] ?? 0));
        if (older) { return false; }
    }
    const turnChanged = !state || next.gameId !== state.gameId || next.moveNo !== state.moveNo || next.phase !== state.phase;
    state = next;
    if (turnChanged) { selection = null; choices = null; }
    return true;
}

function offer(userData, render_) {
    if (userData && accept(userData.gameTable) && render_) { render(); }
}

function heard(text) {
    let message;
    try { message = JSON.parse(text); } catch (e) { return; }
    if (!message || typeof message !== "object") { return; }
    if (message.type === "state") {
        const { type, ...next } = message;
        if (accept(next)) { render(); }
    } else if (message.type === "rejected" && key(message.to) === key(you.sessionUUID)) {
        say(`${message.method} refused: ${message.reason}`);
    } else if (message.type === "over" && message.result) {
        say(resultText(message.result));
    } else if (message.type === "expired" && key(message.session) === key(you.sessionUUID)) {
        say("Your seat timed out waiting for an opponent.");
    }
}

function resultText(result) {
    const why = REASON_TEXT[result.reason] || result.reason;
    if (result.winner === null || result.winner === undefined) { return `Draw (${why}).`; }
    const winner = result.winner === mySeat() ? "You win" : `${sides()[result.winner]} wins`;
    return `${winner} (${why}).`;
}

function say(text) {
    notice = text || "";
    render();
}

async function play(move) {
    selection = null;
    choices = null;
    render();
    await call("move", [String(move)]);
}

function tapColumn(col) {
    if (!myTurn()) { say(mySeat() === null ? "Take a seat to play." : "Not your turn."); return; }
    if (!state.legal.includes(col)) { say("That column is full."); return; }
    play(col);
}

function tapSquare(cell) {
    if (!myTurn()) { say(mySeat() === null ? "Take a seat to play." : "Not your turn."); return; }
    const game = state.game;
    if (selection) {
        const targets = movesTo(game, selection.moves, cell);
        if (targets.length === 1) { play(targets[0]); return; }
        if (targets.length > 1) { choices = targets; render(); return; }
    }
    const moves = movesFrom(game, state.legal, cell);
    selection = moves.length ? { cell, moves } : null;
    choices = null;
    render();
}

function leave() {
    if (state && state.phase === "playing" && Date.now() > resignUntil) {
        resignUntil = Date.now() + RESIGN_CONFIRM_MS;
        say("Tap Resign again within 5 s to resign the game.");
        return;
    }
    resignUntil = 0;
    call("leave");
}

// --- drawing ------------------------------------------------------------------------------

function button(label, onClick, { disabled = false, className = "" } = {}) {
    const element = document.createElement("button");
    element.type = "button";
    element.textContent = label;
    element.disabled = disabled;
    if (className) { element.className = className; }
    element.addEventListener("click", onClick);
    return element;
}

function statusText() {
    if (!state) { return "Waiting for the referee."; }
    const seat = mySeat();
    if (state.phase === "offline") { return "The referee is not running."; }
    if (state.phase === "waiting") { return seat === null ? "Waiting for players. Take a seat." : "Waiting for an opponent."; }
    if (state.phase === "over") { return state.result ? resultText(state.result) : "Game over."; }
    const left = Math.max(0, Math.round((state.turnDeadline - Date.now()) / 1000));
    const clock = Number.isFinite(left) ? ` (${Math.floor(left / 60)}:${String(left % 60).padStart(2, "0")})` : "";
    const check = state.check ? " Check." : "";
    if (myTurn()) { return `Your move${clock}.${check}`; }
    const mover = state.seats[state.toMove];
    return `${mover ? mover.name : sides()[state.toMove]} to move${clock}.${check}`;
}

function drawSeats() {
    const seat = mySeat();
    const row = $("seats");
    row.replaceChildren(...sides().map((side, i) => {
        const holder = state && state.seats ? state.seats[i] : null;
        if (holder) {
            const label = i === seat ? `You (${side})` : `${holder.name} (${side})`;
            return button(label, () => {}, { disabled: true, className: `seat side${i}${i === state.toMove && state.phase === "playing" ? " to-move" : ""}` });
        }
        return button(`Play ${side}`, () => call("join", [you.displayName, String(i)]),
            { disabled: !state || seat !== null || state.phase === "offline", className: `seat side${i}` });
    }));
}

function drawConnect4() {
    const grid = $("board");
    grid.className = "board connect4";
    const legal = myTurn() ? new Set(state.legal) : new Set();
    const line = new Set(((state.result && state.result.line) || []).map(([r, c]) => `${r},${c}`));
    const cells = [];
    state.rows.forEach((text, r) => [...text].forEach((piece, c) => {
        const cell = button("", () => tapColumn(c), { className: "cell" + (r === 0 && legal.has(c) ? " legal" : "") });
        cell.setAttribute("aria-label", `Column ${c + 1}`);
        const side = pieceSide("connect4", piece);
        if (side !== null) {
            const disc = document.createElement("span");
            disc.className = `disc side${side}` + (line.has(`${r},${c}`) ? " win" : "")
                + (state.last && state.last.row === r && state.last.col === c ? " last" : "");
            cell.append(disc);
        }
        cells.push(cell);
    }));
    grid.replaceChildren(...cells);
}

function drawGrid() {
    const game = state.game;
    const grid = $("board");
    grid.className = `board grid ${game}`;
    const flip = flipped(game, mySeat());
    const targets = new Set((selection ? selection.moves : []).map((m) => moveEnds(game, m).to.join(",")));
    const movable = new Set((myTurn() ? state.legal : []).map((m) => moveEnds(game, m).from.join(",")));
    const last = state.last && state.last.move ? moveEnds(game, state.last.move) : null;
    const cells = [];
    for (let i = 0; i < 8; i++) {
        for (let j = 0; j < 8; j++) {
            const r = flip ? 7 - i : i;
            const c = flip ? 7 - j : j;
            const piece = state.rows[r][c];
            const here = `${r},${c}`;
            const classes = ["square", (r + c) % 2 ? "dark" : "light"];
            if (selection && selection.cell.join(",") === here) { classes.push("selected"); }
            if (targets.has(here)) { classes.push("target"); }
            if (movable.has(here)) { classes.push("movable"); }
            if (last && (last.from.join(",") === here || last.to.join(",") === here)) { classes.push("last"); }
            const cell = button("", () => tapSquare([r, c]), { className: classes.join(" ") });
            const side = pieceSide(game, piece);
            if (side !== null) {
                const token = document.createElement("span");
                const king = game === "checkers" && piece === piece.toUpperCase();
                token.className = `piece side${side}${king ? " king" : ""}`;
                token.textContent = game === "chess" ? piece.toUpperCase() : king ? "K" : "";
                cell.append(token);
            }
            cells.push(cell);
        }
    }
    grid.replaceChildren(...cells);
}

function drawActions() {
    const seat = mySeat();
    const actions = [];
    if (choices) {
        actions.push(...choices.map((move) => button(moveLabel(state.game, move), () => play(move), { className: "choice" })));
        actions.push(button("Cancel", () => { choices = null; selection = null; render(); }));
    } else if (seat !== null && state) {
        if (state.phase === "playing") {
            const offered = state.drawOffer;
            actions.push(button(offered === null ? "Offer draw" : offered === seat ? "Draw offered" : "Accept draw",
                () => call("draw"), { disabled: offered === seat }));
            actions.push(button(Date.now() < resignUntil ? "Resign (confirm)" : "Resign", leave, { className: "danger" }));
        } else {
            if (state.phase === "over") { actions.push(button("Rematch", () => call("newGame"))); }
            actions.push(button("Leave seat", leave));
        }
    }
    $("actions").replaceChildren(...actions);
}

function render() {
    $("status").textContent = statusText();
    $("status").dataset.turn = myTurn() ? "you" : "";
    $("notice").textContent = notice;
    if (state) {
        document.querySelector("main").dataset.game = state.game || "connect4";
        drawSeats();
        if (state.game === "connect4" || !state.game) { drawConnect4(); } else { drawGrid(); }
    }
    drawActions();
    if (host.nonce) { send({ type: "resize", height: document.documentElement.scrollHeight }); }
}

// The clock line ticks; nothing else changes between frames.
setInterval(() => {
    if (state && state.phase === "playing") { $("status").textContent = statusText(); }
}, 1000);
