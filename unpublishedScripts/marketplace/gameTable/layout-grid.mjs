//
//  layout-grid.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The 8x8 board, for the referee: a Box lying flat, its local +y face the playing surface,
//  with 64 square tiles across local x and z, the pieces standing on them, the status Text
//  above the far edge and a seat button at each player's edge. Seat 0 plays from the +z edge
//  (the rezzer's side when the board is rezzed with the avatar's orientation), seat 1 from -z.
//
//  The tiles carry no script: the board's own client instance takes their clicks from the
//  Entities.mousePressOnEntity signal (see gametable-client.mjs). Pieces ignore picks, so a
//  click on a piece lands on its tile.
//

import { STATUS_NAME } from "./layout-connect4.mjs";

export const SIZE = 8;
export const SQUARE_PREFIX = "GT.Sq ";
export const PIECE_PREFIX = "GT.Pc ";
const SEAT_NAMES = ["GT.Seat0", "GT.Seat1"];
const EMPTY = ".";
const TILE_HEIGHT = 0.004;
const LIGHT = { red: 226, green: 210, blue: 176 };
const DARK = { red: 96, green: 60, blue: 38 };
const STATUS_SIZE = { x: 1.0, y: 0.45, z: 0.01 };
// Clear of the board, so a player at either edge sees the pieces under it.
const STATUS_RISE = 0.25;
const STATUS_LINE_HEIGHT = 0.03;
const SEAT_SIZE = { x: 0.16, y: 0.06, z: 0.01 };
const SEAT_GAP = 0.02;
const SEAT_LINE_HEIGHT = 0.03;
// A half turn about y: seat 1's button reads from the -z edge.
const HALF_TURN = { x: 0, y: 1, z: 0, w: 0 };
/** @type {Record<string, { backgroundColor: Color, textColor: Color }>} */
const SEAT_COLORS = {
    Black: { backgroundColor: { red: 15, green: 15, blue: 15 }, textColor: { red: 240, green: 240, blue: 240 } },
    Red: { backgroundColor: { red: 190, green: 30, blue: 30 }, textColor: { red: 240, green: 240, blue: 240 } },
    White: { backgroundColor: { red: 235, green: 235, blue: 228 }, textColor: { red: 20, green: 20, blue: 20 } }
};

/**
 * @typedef {{ model: string, size: Vec3 }} PieceLook model relative to the referee's directory;
 *     size in square widths
 * @typedef {{ rules: { SEAT_NAMES: string[] }, rowZeroSide: number, pieces: Record<string, PieceLook> }} GridGame
 * @typedef {{ from: number[], to: number[], captured?: number[][],
 *     castle?: { rookFrom: number[], rookTo: number[] } }} MoveCells the move just played, as
 *     the rules report it (checkers and chess); cells are [row, col]
 * @typedef {{ op: "move", from: string, to: string, piece: string } | { op: "add", to: string, piece: string }
 *     | { op: "remove", from: string }} PieceOp cells as "r<row>c<col>"
 */

/**
 * @param {number} row
 * @param {number} col
 * @returns {string}
 */
export function cellKey(row, col) {
    return "r" + row + "c" + col;
}

/**
 * Where a cell of `rows` lies on the surface, in square widths from the centre. Row 0 is at
 * the edge of seat `rowZeroSide`; the board reads the right way round from either seat (each
 * player's near-left corner is the one their own pieces' text puts there).
 * @param {number} row
 * @param {number} col
 * @param {number} rowZeroSide
 * @returns {{ x: number, z: number }}
 */
export function cellOffset(row, col, rowZeroSide) {
    const half = (SIZE - 1) / 2;
    return rowZeroSide === 0 ? { x: half - col, z: half - row } : { x: col - half, z: row - half };
}

/**
 * The entity changes that turn the pieces shown into `board`. When `move` (the move just
 * played) fits what is shown, its piece's entity is the one that moves, onto its landing square
 * (a promotion changes that entity's model), its captures are the entities removed, and a
 * castling rook moves as its own entity. Whatever else differs (a new game, a reload, a move
 * this instance did not see) is reconciled with the fewest changes: a vacated entity of the
 * same piece is moved, then one on the same cell is edited, then any is reused, before a new
 * one is added.
 * @param {Map<string, string>} shown cell -> piece character
 * @param {string[]} board
 * @param {MoveCells | null} [move]
 * @returns {PieceOp[]}
 */
export function planPieces(shown, board, move = null) {
    /** @type {Map<string, string>} */
    const wanted = new Map();
    board.forEach((line, row) => line.split("").forEach((piece, col) => {
        if (piece !== EMPTY) {
            wanted.set(cellKey(row, col), piece);
        }
    }));
    const vacated = [...shown.keys()].filter(cell => wanted.get(cell) !== shown.get(cell));
    const needed = [...wanted.keys()].filter(cell => shown.get(cell) !== wanted.get(cell));
    /** @type {PieceOp[]} */
    const ops = [];
    /**
     * @param {string} from
     * @param {string} to
     */
    const take = (from, to) => {
        ops.push({ op: "move", from, to, piece: /** @type {string} */ (wanted.get(to)) });
        vacated.splice(vacated.indexOf(from), 1);
        needed.splice(needed.indexOf(to), 1);
    };
    if (move) {
        const pairs = [[move.from, move.to]];
        if (move.castle) {
            pairs.push([move.castle.rookFrom, move.castle.rookTo]);
        }
        pairs.forEach(([from, to]) => {
            const [fromCell, toCell] = [cellKey(from[0], from[1]), cellKey(to[0], to[1])];
            // Only while the board shown is the one before the move: a later redraw sees it done
            if (vacated.includes(fromCell) && needed.includes(toCell)) {
                take(fromCell, toCell);
            }
        });
        (move.captured || []).forEach(([row, col]) => {
            const cell = cellKey(row, col);
            if (vacated.includes(cell)) {
                ops.push({ op: "remove", from: cell });
                vacated.splice(vacated.indexOf(cell), 1);
            }
        });
    }
    /** @param {(from: string, to: string) => boolean} fits */
    const reuse = fits => {
        for (const to of needed.slice()) {
            const from = vacated.find(cell => fits(cell, to));
            if (from !== undefined) {
                take(from, to);
            }
        }
    };
    reuse((from, to) => shown.get(from) === wanted.get(to));
    reuse((from, to) => from === to);
    reuse(() => true);
    needed.forEach(to => ops.push({ op: "add", to, piece: /** @type {string} */ (wanted.get(to)) }));
    vacated.forEach(from => ops.push({ op: "remove", from }));
    return ops;
}

/**
 * @param {unknown} move a move object from the rules, or anything else
 * @returns {MoveCells | null} it, when it has the cells planPieces follows
 */
function moveCells(move) {
    const cell = (/** @type {unknown} */ value) => Array.isArray(value) && value.length === 2;
    const candidate = /** @type {Record<string, any> | null} */ (move !== null && typeof move === "object" ? move : null);
    return candidate && cell(candidate.from) && cell(candidate.to) ? /** @type {MoveCells} */ (candidate) : null;
}

export class GridLayout {
    /** @type {Map<string, { id: Uuid, piece: string }>} cell -> piece entity */ pieces = new Map();

    /**
     * @param {Uuid} boardID
     * @param {Vec3} dimensions the board's
     * @param {string} base the URL of the directory the referee was loaded from
     * @param {GridGame} game
     */
    constructor(boardID, dimensions, base, game) {
        this.boardID = boardID;
        this.dimensions = dimensions;
        this.base = base;
        this.game = game;
        this.clientURL = base + "gametable-client.mjs";
        this.tileNames = [];
        for (let row = 0; row < SIZE; row++) {
            for (let col = 0; col < SIZE; col++) {
                this.tileNames.push(SQUARE_PREFIX + cellKey(row, col));
            }
        }
        // The children there is exactly one of, created by the referee if none turns up.
        this.fixedNames = [STATUS_NAME, ...this.tileNames, ...SEAT_NAMES];
    }

    /**
     * @param {string} name
     * @returns {boolean} whether a child of this name is one of this layout's pieces
     */
    isPiece(name) {
        return name.startsWith(PIECE_PREFIX);
    }

    /** @returns {Set<Uuid>} the pieces this instance created */
    pieceIDs() {
        return new Set([...this.pieces.values()].map(piece => piece.id));
    }

    /** Deletes the pieces this instance created. */
    clear() {
        this.pieces.forEach(piece => Entities.deleteEntity(piece.id));
        this.pieces.clear();
    }

    /** @returns {number} the width of a square */
    get pitch() {
        return Math.min(this.dimensions.x, this.dimensions.z) / SIZE;
    }

    /**
     * @param {number} row
     * @param {number} col
     * @param {number} lift height of the entity's centre above the surface
     * @returns {Vec3}
     */
    position(row, col, lift) {
        const { x, z } = cellOffset(row, col, this.game.rowZeroSide);
        return { x: x * this.dimensions.x / SIZE, y: this.dimensions.y / 2 + lift, z: z * this.dimensions.z / SIZE };
    }

    /**
     * @param {string} name one of fixedNames
     * @returns {Entities.EntityProperties} the child to create
     */
    fixedProperties(name) {
        const { x: width, y: height, z: depth } = this.dimensions;
        const tile = this.tileNames.indexOf(name);
        if (tile >= 0) {
            const row = Math.floor(tile / SIZE);
            const col = tile % SIZE;
            return {
                type: "Box",
                name,
                parentID: this.boardID,
                localPosition: this.position(row, col, TILE_HEIGHT / 2),
                dimensions: { x: width / SIZE, y: TILE_HEIGHT, z: depth / SIZE },
                color: (row + col) % 2 === 0 ? LIGHT : DARK,
                collisionless: true,
                grab: { grabbable: false }
            };
        }
        const seat = SEAT_NAMES.indexOf(name);
        if (seat >= 0) {
            const label = this.game.rules.SEAT_NAMES[seat];
            const side = seat === 0 ? 1 : -1;
            return {
                type: "Text",
                name,
                parentID: this.boardID,
                script: this.clientURL,
                collisionless: true,
                grab: { grabbable: false, triggerable: true },
                // At the player's edge, to their right, reading towards them
                localPosition: {
                    x: side * (width / 2 - SEAT_SIZE.x / 2),
                    y: height / 2 + SEAT_SIZE.y / 2,
                    z: side * (depth / 2 + SEAT_GAP + SEAT_SIZE.z / 2)
                },
                localRotation: seat === 0 ? { x: 0, y: 0, z: 0, w: 1 } : HALF_TURN,
                dimensions: SEAT_SIZE,
                lineHeight: SEAT_LINE_HEIGHT,
                backgroundAlpha: 1,
                unlit: true,
                text: "Play " + label,
                ...(SEAT_COLORS[label] || SEAT_COLORS.Black)
            };
        }
        return {
            type: "Text",
            name: STATUS_NAME,
            parentID: this.boardID,
            localPosition: { x: 0, y: height / 2 + STATUS_RISE + STATUS_SIZE.y / 2, z: -depth / 2 },
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
        };
    }

    /**
     * Brings the piece entities in line with the board, editing rather than replacing them.
     * @param {{ board: string[], move?: unknown }} view `move` is the last move played, if any
     */
    draw({ board, move }) {
        const shown = new Map([...this.pieces].map(([cell, piece]) => [cell, piece.piece]));
        const ops = planPieces(shown, board, moveCells(move));
        // Take every reused entity off its old cell before placing any, so moves can chain
        /** @type {Map<string, { id: Uuid, piece: string }>} */
        const lifted = new Map();
        ops.forEach(op => {
            if (op.op !== "add") {
                lifted.set(op.from, /** @type {{ id: Uuid, piece: string }} */ (this.pieces.get(op.from)));
                this.pieces.delete(op.from);
            }
        });
        ops.forEach(op => {
            if (op.op === "remove") {
                Entities.deleteEntity(/** @type {{ id: Uuid }} */ (lifted.get(op.from)).id);
                return;
            }
            const properties = this.pieceProperties(op.to, op.piece);
            if (!properties) {
                if (op.op === "move") {
                    Entities.deleteEntity(/** @type {{ id: Uuid }} */ (lifted.get(op.from)).id);
                }
                return;
            }
            if (op.op === "move") {
                const { id, piece } = /** @type {{ id: Uuid, piece: string }} */ (lifted.get(op.from));
                const { name, localPosition, dimensions, modelURL } = properties;
                Entities.editEntity(id, piece === op.piece ? { name, localPosition } : { name, localPosition, dimensions, modelURL });
                this.pieces.set(op.to, { id, piece: op.piece });
            } else {
                this.pieces.set(op.to, { id: Entities.addEntity(properties), piece: op.piece });
            }
        });
    }

    /**
     * @param {string} cell "r<row>c<col>"
     * @param {string} piece a character of `rows`
     * @returns {Entities.EntityProperties_Model | null} null for a piece with no model
     */
    pieceProperties(cell, piece) {
        const look = this.game.pieces[piece];
        if (!look) {
            return null;
        }
        const [row, col] = (cell.match(/\d+/g) || []).map(Number);
        const pitch = this.pitch;
        const size = { x: look.size.x * pitch, y: look.size.y * pitch, z: look.size.z * pitch };
        return {
            type: "Model",
            name: PIECE_PREFIX + cell,
            parentID: this.boardID,
            modelURL: this.base + look.model,
            localPosition: this.position(row, col, TILE_HEIGHT + size.y / 2),
            dimensions: size,
            collisionless: true,
            dynamic: false,
            grab: { grabbable: false },
            // Clicks go through to the tile underneath
            ignorePickIntersection: true
        };
    }
}
