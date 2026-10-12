//
//  layout-connect4.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Connect Four board, for the referee, drawn like the toy: the upright Box is the blue
//  grid, with a dark hole through it at every cell, a rail up each side, a base under it, a
//  down-pointing marker over each column (the column targets), the status Text above those
//  and the seat buttons beside the base. The referee entry owns the child sweep and the status
//  text; a layout says which children there are, where they go, and draws the pieces.
//
//  Local axes: the grid faces +z and -z, columns run across x and rows down y. Sizes are in
//  cells (the smaller of the column and row pitch) or in board thicknesses, so the board can
//  be rezzed at any size. Shape Cylinders and Cones have their axis along their local y.
//

import { COLUMNS, ROWS, EMPTY } from "./connect4.mjs";

export const STATUS_NAME = "GT.Status";
const SEAT_NAMES = ["GT.Seat0", "GT.Seat1"];
const COLUMN_NAMES = Array.from({ length: COLUMNS }, (_, col) => "GT.Col" + col);
const HOLE_PREFIX = "GT.Hole ";
const FRAME_NAMES = ["GT.Frame L", "GT.Frame R"];
const BASE_NAME = "GT.Base";
const DISC_PREFIX = "GT.Disc ";
const WIN_PREFIX = "GT.Win ";

/**
 * @param {Color} color
 * @param {number} factor 0 is black
 * @returns {Color}
 */
function shade(color, factor) {
    return { red: Math.round(color.red * factor), green: Math.round(color.green * factor), blue: Math.round(color.blue * factor) };
}

/**
 * @param {Color} color
 * @param {number} factor 1 is white
 * @returns {Color}
 */
function tint(color, factor) {
    const mix = (/** @type {number} */ value) => Math.round(value + (255 - value) * factor);
    return { red: mix(color.red), green: mix(color.green), blue: mix(color.blue) };
}

// The referee paints the board itself this blue; the rest of the toy is derived from it.
export const BOARD_COLOR = { red: 20, green: 80, blue: 190 };
const FRAME_COLOR = shade(BOARD_COLOR, 0.7);
const BASE_COLOR = shade(BOARD_COLOR, 0.5);
const HOLE_COLOR = shade(BOARD_COLOR, 0.18);
const MARKER_COLOR = tint(BOARD_COLOR, 0.88);
const WIN_COLOR = { red: 255, green: 205, blue: 40 };
/** @type {Record<string, Color>} */
const DISC_COLORS = {
    R: { red: 200, green: 30, blue: 35 },
    // Charcoal, not black: it must not read as an empty hole
    B: { red: 58, green: 60, blue: 68 }
};

// Diameters, in cells. A disc covers its hole; a winning ring shows round the disc.
const HOLE_FILL = 0.76;
const DISC_FILL = 0.8;
const WIN_FILL = 0.96;
// Depths, in board thicknesses: each stands proud of the one before on both faces, so the
// hole shows on the board, the ring on the hole and the disc on the ring.
const HOLE_DEPTH = 1.12;
const WIN_DEPTH = 1.24;
const DISC_DEPTH = 1.4;
// The side rails and base, in cells (the rails' extra depth is added to the board's).
const RAIL_WIDTH = 0.3;
const RAIL_CAP = 0.15;
const RAIL_EXTRA_DEPTH = 0.3;
const BASE_HEIGHT = 0.3;
const BASE_OVERHANG = 0.2;
const BASE_FOOT = 0.8;
// The column markers: cones pointing down at their columns, in cells.
const MARKER_SIZE = { x: 0.64, y: 0.5 };
const MARKER_GAP = 0.12;
// A quarter turn about x takes a Shape's y axis to z, to face the viewer.
const QUARTER_TURN_X = { x: Math.SQRT1_2, y: 0, z: 0, w: Math.SQRT1_2 };
// A half turn about x points a Cone's apex (+y) down.
const HALF_TURN_X = { x: 1, y: 0, z: 0, w: 0 };
// The status display and seat buttons are the 8x8 boards' sizes on a board with cells this
// wide (the README's), and scale with the cells.
const REFERENCE_CELL = 0.75 / COLUMNS;
const STATUS_SIZE = { x: 1.0, y: 0.4, z: 0.01 };
const STATUS_GAP = 0.05;
const STATUS_LINE_HEIGHT = 0.03;
const SEAT_SIZE = { x: 0.16, y: 0.06, z: 0.01 };
const SEAT_GAP = 0.03;
const SEAT_LINE_HEIGHT = 0.03;
const SEAT_LOOK = [
    { text: "Play Red", backgroundColor: { red: 190, green: 30, blue: 30 } },
    { text: "Play Black", backgroundColor: { red: 15, green: 15, blue: 15 } }
];

/**
 * @param {number} row
 * @param {number} col
 * @returns {string}
 */
function cellKey(row, col) {
    return "r" + row + "c" + col;
}

/**
 * @param {Vec3} size
 * @param {number} scale
 * @returns {Vec3}
 */
function scaled(size, scale) {
    return { x: size.x * scale, y: size.y * scale, z: size.z * scale };
}

export class Connect4Layout {
    /** @type {Map<string, { id: Uuid, piece: string }>} cell "r<row>c<col>" -> disc */ discs = new Map();
    /** @type {Map<string, Uuid>} cell -> the ring round a winning disc */ rings = new Map();
    boardColor = BOARD_COLOR;

    /**
     * @param {Uuid} boardID
     * @param {Vec3} dimensions the board's
     * @param {string} base the URL of the directory the referee was loaded from
     */
    constructor(boardID, dimensions, base) {
        this.boardID = boardID;
        this.dimensions = dimensions;
        this.clientURL = base + "gametable-client.mjs";
        this.holeNames = [];
        for (let row = 0; row < ROWS; row++) {
            for (let col = 0; col < COLUMNS; col++) {
                this.holeNames.push(HOLE_PREFIX + cellKey(row, col));
            }
        }
        // The children there is exactly one of, created by the referee if none turns up.
        this.fixedNames = [STATUS_NAME, ...COLUMN_NAMES, ...SEAT_NAMES, ...this.holeNames, ...FRAME_NAMES, BASE_NAME];
    }

    /**
     * @param {string} name
     * @returns {boolean} whether a child of this name is one of this layout's pieces
     */
    isPiece(name) {
        return name.startsWith(DISC_PREFIX) || name.startsWith(WIN_PREFIX);
    }

    /** @returns {Set<Uuid>} the pieces this instance created */
    pieceIDs() {
        return new Set([...[...this.discs.values()].map(disc => disc.id), ...this.rings.values()]);
    }

    /** Deletes the pieces this instance created. */
    clear() {
        this.discs.forEach(disc => Entities.deleteEntity(disc.id));
        this.discs.clear();
        this.rings.forEach(id => Entities.deleteEntity(id));
        this.rings.clear();
    }

    /** @returns {number} the size of a cell: the smaller of the column and row pitch */
    get cell() {
        return Math.min(this.dimensions.x / COLUMNS, this.dimensions.y / ROWS);
    }

    /** @returns {number} the status display's and seat buttons' scale */
    get textScale() {
        return this.cell / REFERENCE_CELL;
    }

    /** @returns {{ railWidth: number, baseHeight: number, baseWidth: number }} the frame's sizes */
    get frame() {
        const railWidth = RAIL_WIDTH * this.cell;
        return {
            railWidth,
            baseHeight: BASE_HEIGHT * this.cell,
            baseWidth: this.dimensions.x + 2 * railWidth + 2 * BASE_OVERHANG * this.cell
        };
    }

    /**
     * @param {number} row 0 is the top row
     * @param {number} col
     * @returns {Vec3} the centre of the cell, in the board's middle plane
     */
    cellPosition(row, col) {
        return {
            x: (col - (COLUMNS - 1) / 2) * this.dimensions.x / COLUMNS,
            y: ((ROWS - 1) / 2 - row) * this.dimensions.y / ROWS,
            z: 0
        };
    }

    /**
     * A flat cylinder through the board at a cell, facing z.
     * @param {string} name
     * @param {number} row
     * @param {number} col
     * @param {number} fill diameter, in cells
     * @param {number} depth in board thicknesses
     * @returns {Entities.EntityProperties_Shape}
     */
    cellCylinder(name, row, col, fill, depth) {
        const diameter = fill * this.cell;
        return {
            type: "Shape",
            shape: "Cylinder",
            name,
            parentID: this.boardID,
            localPosition: this.cellPosition(row, col),
            localRotation: QUARTER_TURN_X,
            dimensions: { x: diameter, y: depth * this.dimensions.z, z: diameter },
            collisionless: true,
            grab: { grabbable: false },
            ignorePickIntersection: true
        };
    }

    /**
     * @param {string} name one of fixedNames
     * @returns {Entities.EntityProperties} the child to create
     */
    fixedProperties(name) {
        const { x: width, y: height, z: depth } = this.dimensions;
        const cell = this.cell;
        const scale = this.textScale;
        const { railWidth, baseHeight, baseWidth } = this.frame;
        const markerHeight = MARKER_SIZE.y * cell;
        const hole = this.holeNames.indexOf(name);
        if (hole >= 0) {
            return {
                ...this.cellCylinder(name, Math.floor(hole / COLUMNS), hole % COLUMNS, HOLE_FILL, HOLE_DEPTH),
                color: HOLE_COLOR,
                unlit: true
            };
        }
        const solid = {
            name,
            parentID: this.boardID,
            collisionless: true,
            grab: { grabbable: false },
            ignorePickIntersection: true
        };
        const rail = FRAME_NAMES.indexOf(name);
        if (rail >= 0) {
            // From the base up past the grid's top edge, covering the grid's side
            return {
                ...solid,
                type: "Box",
                localPosition: { x: (rail === 0 ? -1 : 1) * (width + railWidth) / 2, y: RAIL_CAP * cell / 2, z: 0 },
                dimensions: { x: railWidth, y: height + RAIL_CAP * cell, z: depth + RAIL_EXTRA_DEPTH * cell },
                color: FRAME_COLOR
            };
        }
        if (name === BASE_NAME) {
            return {
                ...solid,
                type: "Box",
                localPosition: { x: 0, y: -(height + baseHeight) / 2, z: 0 },
                dimensions: { x: baseWidth, y: baseHeight, z: depth + 2 * BASE_FOOT * cell },
                color: BASE_COLOR
            };
        }
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
            // A Shape other than a Sphere is picked by its bounding box, so all of it is the target
            return {
                ...clickable,
                type: "Shape",
                shape: "Cone",
                localPosition: { x: this.cellPosition(0, column).x, y: height / 2 + MARKER_GAP * cell + markerHeight / 2, z: 0 },
                localRotation: HALF_TURN_X,
                dimensions: { x: MARKER_SIZE.x * cell, y: markerHeight, z: MARKER_SIZE.x * cell },
                color: MARKER_COLOR
            };
        }
        if (seat >= 0) {
            const size = scaled(SEAT_SIZE, scale);
            // Beside the base, standing on the same floor
            return {
                ...clickable,
                type: "Text",
                localPosition: {
                    x: (seat === 0 ? -1 : 1) * (baseWidth / 2 + SEAT_GAP * scale + size.x / 2),
                    y: size.y / 2 - height / 2 - baseHeight,
                    z: 0
                },
                dimensions: size,
                lineHeight: SEAT_LINE_HEIGHT * scale,
                textColor: { red: 240, green: 240, blue: 240 },
                backgroundAlpha: 1,
                unlit: true,
                ...SEAT_LOOK[seat]
            };
        }
        const size = scaled(STATUS_SIZE, scale);
        return {
            type: "Text",
            name: STATUS_NAME,
            parentID: this.boardID,
            localPosition: {
                x: 0,
                y: height / 2 + MARKER_GAP * cell + markerHeight + STATUS_GAP * scale + size.y / 2,
                z: 0
            },
            dimensions: size,
            font: "Inconsolata",
            lineHeight: STATUS_LINE_HEIGHT * scale,
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
     * @param {number} row
     * @param {number} col
     * @param {string} piece "R" or "B"
     * @returns {Entities.EntityProperties_Shape}
     */
    discProperties(row, col, piece) {
        return { ...this.cellCylinder(DISC_PREFIX + cellKey(row, col), row, col, DISC_FILL, DISC_DEPTH), color: DISC_COLORS[piece] };
    }

    /**
     * @param {number} row
     * @param {number} col
     * @returns {Entities.EntityProperties_Shape} a bright ring showing round a winning disc
     */
    ringProperties(row, col) {
        return { ...this.cellCylinder(WIN_PREFIX + cellKey(row, col), row, col, WIN_FILL, WIN_DEPTH), color: WIN_COLOR, unlit: true };
    }

    /**
     * Brings the disc entities in line with the board: a move adds one disc, a win adds a ring
     * behind each disc of the line.
     * @param {{ board: string[], line: number[][] | null }} view
     */
    draw({ board, line }) {
        const winning = new Set((line || []).map(([row, col]) => cellKey(row, col)));
        for (let row = 0; row < ROWS; row++) {
            for (let col = 0; col < COLUMNS; col++) {
                const cell = cellKey(row, col);
                const piece = board[row][col];
                const disc = this.discs.get(cell);
                if ((disc ? disc.piece : EMPTY) !== piece) {
                    if (disc) {
                        Entities.deleteEntity(disc.id);
                        this.discs.delete(cell);
                    }
                    if (piece !== EMPTY) {
                        this.discs.set(cell, { id: Entities.addEntity(this.discProperties(row, col, piece)), piece });
                    }
                }
                const ring = this.rings.get(cell);
                const ringed = piece !== EMPTY && winning.has(cell);
                if (ring && !ringed) {
                    Entities.deleteEntity(ring);
                    this.rings.delete(cell);
                } else if (!ring && ringed) {
                    this.rings.set(cell, Entities.addEntity(this.ringProperties(row, col)));
                }
            }
        }
    }
}
