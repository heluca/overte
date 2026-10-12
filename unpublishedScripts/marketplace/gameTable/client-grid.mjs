//
//  client-grid.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The 8x8 board's part of the client, run by the board's own gametable-client.mjs instance:
//  it takes clicks on the square tiles, turns them into a move with MovePicker, and shows the
//  selection and the squares it can go to on local highlight entities.
//
//  The tiles carry no script. The board's instance listens to the global
//  Entities.mousePressOnEntity signal, which a desktop mouse press emits
//  (EntityTreeRenderer::mousePressEvent) and so does a hand-controller laser trigger, through
//  PointerManager::triggerBeginEntity (EntityTreeRenderer's constructor connects the two). The
//  clickDownOnEntity entity method the Connect Four targets use comes from the mouse path only.
//

import { MovePicker } from "./grid-select.mjs";
import { SQUARE_PREFIX, PIECE_PREFIX } from "./layout-grid.mjs";

const CELL = /^r([0-7])c([0-7])$/;
const HIGHLIGHT_PREFIX = "GT.Highlight ";
const HIGHLIGHT_FILL = 0.94;
const HIGHLIGHT_HEIGHT = 0.003;
/** @type {Record<string, Color>} */
const HIGHLIGHT_COLORS = { selected: { red: 240, green: 200, blue: 40 }, target: { red: 60, green: 220, blue: 90 } };
const HIGHLIGHT_ALPHA = 0.6;
const HIGHLIGHT_RETRY_MS = 1000;

/**
 * @param {string} name a child's name
 * @returns {number[] | null} the [row, col] of a tile or a piece
 */
function cellOf(name) {
    const prefix = name.startsWith(SQUARE_PREFIX) ? SQUARE_PREFIX : name.startsWith(PIECE_PREFIX) ? PIECE_PREFIX : "";
    const match = prefix ? CELL.exec(name.slice(prefix.length)) : null;
    return match ? [Number(match[1]), Number(match[2])] : null;
}

/**
 * @param {number[]} cell
 * @returns {string}
 */
function key(cell) {
    return "r" + cell[0] + "c" + cell[1];
}

export class GridClient {
    picker = new MovePicker();
    turn = "";
    /** @type {Map<string, { id: Uuid, target: Uuid, kind: string }>} cell -> local highlight */
    highlights = new Map();
    /** @type {number | null} */ retryTimer = null;

    /**
     * @param {Uuid} boardID
     * @param {(text: string) => number[][] | null} cells the game's notation -> cells
     */
    constructor(boardID, cells) {
        this.boardID = boardID;
        this.cells = cells;
        this.onPress = (/** @type {Uuid} */ id, /** @type {PointerEvent} */ event) => this.press(id, event);
        Entities.mousePressOnEntity.connect(this.onPress);
    }

    stop() {
        Entities.mousePressOnEntity.disconnect(this.onPress);
        this.picker.setChoices([]);
        this.show();
    }

    /**
     * @param {Record<string, any>} state the published projection
     * @param {boolean} myTurn
     */
    update(state, myTurn) {
        const legal = myTurn && Array.isArray(state.legal) ? state.legal.filter(move => typeof move === "string") : [];
        const turn = myTurn ? state.gameId + "/" + state.moveNo + "/" + legal.join(" ") : "";
        // The same state can arrive twice; only a new turn drops the selection
        if (turn !== this.turn) {
            this.turn = turn;
            this.picker.setChoices(legal.flatMap(notation => {
                const cells = this.cells(notation);
                return cells ? [{ notation, cells }] : [];
            }));
        }
        this.show();
    }

    /**
     * Any click in the domain arrives here; only this board's squares and pieces count.
     * @param {Uuid} id
     * @param {PointerEvent} event
     */
    press(id, event) {
        if (!event.isPrimaryButton || this.picker.choices.length === 0) {
            return;
        }
        const properties = Entities.getEntityProperties(id, ["name", "parentID"]);
        const cell = properties.parentID === this.boardID ? cellOf(properties.name || "") : null;
        if (!cell) {
            return;
        }
        const move = this.picker.click(cell);
        if (move) {
            Entities.callEntityServerMethod(this.boardID, "move", [move]);
        }
        this.show();
    }

    /**
     * Shows a local highlight on each tile the picker offers and removes the rest. Local
     * entities are drawn for this user only and need no edit rights.
     */
    show() {
        const { selected, targets } = this.picker.view();
        /** @type {Map<string, string>} */
        const wanted = new Map();
        targets.forEach(cell => wanted.set(key(cell), "target"));
        selected.forEach(cell => wanted.set(key(cell), "selected"));
        this.highlights.forEach((shown, cell) => {
            // A tile the referee deleted (a duplicate it swept) takes the highlight with it
            const tileGone = !Entities.getEntityProperties(shown.target, ["name"]).name;
            if (wanted.get(cell) !== shown.kind || tileGone) {
                Entities.deleteEntity(shown.id);
                this.highlights.delete(cell);
            }
        });
        const missing = () => [...wanted.keys()].some(cell => !this.highlights.has(cell));
        if (missing()) {
            this.addHighlights(wanted);
        }
        if (this.retryTimer !== null) {
            Script.clearTimeout(this.retryTimer);
            this.retryTimer = null;
        }
        // The tiles can reach this client after the board's state does: look again
        if (missing()) {
            this.retryTimer = Script.setTimeout(() => {
                this.retryTimer = null;
                this.show();
            }, HIGHLIGHT_RETRY_MS);
        }
    }

    /** @param {Map<string, string>} wanted cell -> kind */
    addHighlights(wanted) {
        Entities.getChildrenIDs(this.boardID).forEach(id => {
            const tile = /** @type {Partial<Entities.EntityProperties_Box>} */ (
                Entities.getEntityProperties(id, ["name", "dimensions"]));
            const name = tile.name || "";
            const cell = name.startsWith(SQUARE_PREFIX) ? cellOf(name) : null;
            const kind = cell ? wanted.get(key(cell)) : undefined;
            if (!cell || !kind || this.highlights.has(key(cell))) {
                return;
            }
            const size = /** @type {Vec3} */ (tile.dimensions);
            this.highlights.set(key(cell), { target: id, kind, id: Entities.addEntity({
                type: "Box",
                name: HIGHLIGHT_PREFIX + key(cell),
                parentID: id,
                localPosition: { x: 0, y: (size.y + HIGHLIGHT_HEIGHT) / 2, z: 0 },
                dimensions: { x: size.x * HIGHLIGHT_FILL, y: HIGHLIGHT_HEIGHT, z: size.z * HIGHLIGHT_FILL },
                color: HIGHLIGHT_COLORS[kind],
                alpha: HIGHLIGHT_ALPHA,
                collisionless: true,
                // The click must reach the tile underneath, not the highlight
                ignorePickIntersection: true,
                grab: { grabbable: false }
            }, "local") });
        });
    }
}
