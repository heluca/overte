//
//  layout-connect4.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Connect Four board, for the referee: an upright Box with the disc grid across its front
//  face, the column targets in a strip above it, the status Text above those and the seat
//  buttons beside its bottom corners. The referee entry owns the child sweep and the status
//  text; a layout says which children there are, where they go, and draws the pieces.
//

import { COLUMNS, ROWS, EMPTY } from "./connect4.mjs";

export const STATUS_NAME = "GT.Status";
const SEAT_NAMES = ["GT.Seat0", "GT.Seat1"];
const COLUMN_NAMES = Array.from({ length: COLUMNS }, (_, col) => "GT.Col" + col);
const DISC_PREFIX = "GT.Disc ";
const DISC_FILL = 0.8;
const WIN_SCALE = 1.2;
// A disc is thicker than the backplate, so it shows through both faces like a real one.
const DISC_DEPTH = 1.5;
// The checker model lies flat (thin along y); a quarter turn about x stands it up facing z.
const DISC_ROTATION = { x: Math.SQRT1_2, y: 0, z: 0, w: Math.SQRT1_2 };
const STATUS_SIZE = { x: 1.0, y: 0.4, z: 0.01 };
const STATUS_GAP = 0.05;
const STATUS_LINE_HEIGHT = 0.03;
// Column targets: a strip of slots just above the board, under the status display.
const SLOT_GAP = 0.01;
const SLOT_FILL = 0.8;
const SLOT_HEIGHT = 0.5;
const SEAT_SIZE = { x: 0.16, y: 0.06, z: 0.01 };
const SEAT_GAP = 0.03;
const SEAT_LINE_HEIGHT = 0.03;
const SEAT_LOOK = [
    { text: "Play Red", backgroundColor: { red: 190, green: 30, blue: 30 } },
    { text: "Play Black", backgroundColor: { red: 15, green: 15, blue: 15 } }
];

export class Connect4Layout {
    // The children there is exactly one of, created by the referee if none turns up.
    fixedNames = [STATUS_NAME, ...COLUMN_NAMES, ...SEAT_NAMES];
    /** @type {Map<string, { id: Uuid, look: string }>} cell "r<row>c<col>" -> disc */ discs = new Map();

    /**
     * @param {Uuid} boardID
     * @param {Vec3} dimensions the board's
     * @param {string} base the URL of the directory the referee was loaded from
     */
    constructor(boardID, dimensions, base) {
        this.boardID = boardID;
        this.dimensions = dimensions;
        this.clientURL = base + "gametable-client.mjs";
        this.models = {
            R: base + "assets/checkers/red_checker.fbx",
            B: base + "assets/checkers/black_checker.fbx"
        };
    }

    /**
     * @param {string} name
     * @returns {boolean} whether a child of this name is one of this layout's pieces
     */
    isPiece(name) {
        return name.startsWith(DISC_PREFIX);
    }

    /** @returns {Set<Uuid>} the pieces this instance created */
    pieceIDs() {
        return new Set([...this.discs.values()].map(disc => disc.id));
    }

    /** Deletes the pieces this instance created. */
    clear() {
        this.discs.forEach(disc => Entities.deleteEntity(disc.id));
        this.discs.clear();
    }

    /**
     * @param {string} name one of fixedNames
     * @returns {Entities.EntityProperties} the child to create
     */
    fixedProperties(name) {
        const { x: width, y: height, z: depth } = this.dimensions;
        const pitch = width / COLUMNS;
        const slotHeight = SLOT_HEIGHT * pitch;
        const column = COLUMN_NAMES.indexOf(name);
        const seat = SEAT_NAMES.indexOf(name);
        // Humans click these: each runs the client script, which knows its role by its name
        const clickable = {
            name,
            parentID: this.boardID,
            script: this.clientURL,
            collisionless: true,
            grab: { grabbable: false, triggerable: true }
        };
        if (column >= 0) {
            return {
                ...clickable,
                type: "Box",
                localPosition: { x: (column - (COLUMNS - 1) / 2) * pitch, y: height / 2 + SLOT_GAP + slotHeight / 2, z: 0 },
                dimensions: { x: SLOT_FILL * pitch, y: slotHeight, z: depth },
                color: { red: 200, green: 200, blue: 210 },
                alpha: 0.6
            };
        }
        if (seat >= 0) {
            return {
                ...clickable,
                type: "Text",
                localPosition: {
                    x: (seat === 0 ? -1 : 1) * (width / 2 + SEAT_GAP + SEAT_SIZE.x / 2),
                    y: SEAT_SIZE.y / 2 - height / 2,
                    z: 0
                },
                dimensions: SEAT_SIZE,
                lineHeight: SEAT_LINE_HEIGHT,
                textColor: { red: 240, green: 240, blue: 240 },
                backgroundAlpha: 1,
                unlit: true,
                ...SEAT_LOOK[seat]
            };
        }
        return {
            type: "Text",
            name: STATUS_NAME,
            parentID: this.boardID,
            localPosition: {
                x: 0,
                y: height / 2 + SLOT_GAP + slotHeight + STATUS_GAP + STATUS_SIZE.y / 2,
                z: 0
            },
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
     * Brings the disc entities in line with the board.
     * @param {{ board: string[], line: number[][] | null }} view
     */
    draw({ board, line }) {
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
            parentID: this.boardID,
            modelURL: this.models[/** @type {"R" | "B"} */ (piece)],
            localPosition: { x: (col - (COLUMNS - 1) / 2) * pitchX, y: ((ROWS - 1) / 2 - row) * pitchY, z: 0 },
            localRotation: DISC_ROTATION,
            dimensions: { x: diameter, y: this.dimensions.z * DISC_DEPTH, z: diameter },
            collisionless: true,
            dynamic: false,
            grab: { grabbable: false }
        });
    }
}
