//
//  layout-controls.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The status display and the two seat buttons, for both layouts: a layout says where they go
//  and in what colours, this module builds them, and the referee shows on them who sits where
//  and whose turn it is.
//
//  The status display turns to face the viewer (billboardMode "yaw"). The engine turns each
//  such entity about its own centre, by an angle taken from its horizontal offset to the camera
//  (Application_Setup.cpp), so every part of the display has its centre on the vertical line
//  through GT.Status's: they then all turn by the same angle about the same line, as one panel.
//  Depth is what stacks them, so it is in metres, not scaled: a Text's depth is fixed at 1 cm
//  and it draws its glyphs 5 mm in front of its centre (its background at the centre), so the
//  frame and screen behind the text are thinner boxes round the same centre.
//

export const STATUS_NAME = "GT.Status";
export const SEAT_NAMES = ["GT.Seat0", "GT.Seat1"];
const FRAME_NAME = "GT.StatusFrame";
const SCREEN_NAME = "GT.StatusScreen";
const TITLE_NAME = "GT.StatusTitle";
const LAMP_NAME = "GT.StatusLamp";
const TURN_NAME = "GT.StatusTurn";
const PLATE = "Plate";
const CAP = "Cap";
const TOKEN = "Token";

// The display is as wide as the board it serves, but no narrower than this, so its text stays
// legible over a small board.
const STATUS_MIN_WIDTH = 0.8;
// In display widths: a body line (some 56 Inconsolata characters to a line), the bezel at the
// sides, the band over the screen with the game's name and the band under it with the lamp.
const LINE = 1 / 28;
const SIDE_BEZEL = 0.025;
const TITLE_BAND = 0.08;
const TITLE_LINE = 0.045;
const LAMP_BAND = 0.08;
const LAMP_SIZE = 0.055;
const TURN_SIZE = 0.036;
// The screen's margin round the text, in lines
const MARGIN = 0.5;
// Depths, in metres: frame < lamp < screen < turn disc, and the screen behind the glyphs
const TEXT_DEPTH = 0.01;
const FRAME_DEPTH = 0.002;
const LAMP_DEPTH = 0.005;
const SCREEN_DEPTH = 0.006;
const TURN_DEPTH = 0.008;

// The seat buttons, in metres at scale 1 (each layout's reference board): a plate, a raised
// cap on it smaller by the rim, and on the cap a token of the seat's pieces and the label.
const SEAT_SIZE = { x: 0.24, y: 0.08 };
const PLATE_DEPTH = 0.014;
const RIM = 0.008;
const CAP_DEPTH = 0.01;
const TOKEN_FILL = 0.78;
const TOKEN_DEPTH = 0.01;
const LABEL_LINE = 0.026;
const LABEL_CHARS = 12;
// The label's glyphs this far in front of the cap, in metres
const GLYPH_LIFT = 0.001;
const IDENTITY = { x: 0, y: 0, z: 0, w: 1 };
const HALF_TURN_Y = { x: 0, y: 1, z: 0, w: 0 };
// A quarter turn about x takes a Cylinder's axis (its y) to z
const QUARTER_TURN_X = { x: Math.SQRT1_2, y: 0, z: 0, w: Math.SQRT1_2 };

/**
 * @typedef {{ bezel: Color, screen: Color, text: Color, title: Color, titleFont: string, ring: Color,
 *     plate: Color, glow: Color, cap: Color, label: Color, labelFont: string }} ControlsLook
 * @typedef {{ name: string, color: Color }} Side a seat's side, e.g. "Red", and its pieces' colour
 * @typedef {{ centre: Vec3, width: number, lines: number }} StatusPlace the display's centre and
 *     width (from statusSize) and the body lines it holds, board-local
 * @typedef {{ position: Vec3, facing: number }} SeatPlace the plate's centre, board-local; the
 *     button faces +z (facing 1) or -z (facing -1)
 * @typedef {{ status: StatusPlace, seats: SeatPlace[], scale: number }} Placement
 */

/**
 * @param {number} span the width of the board under the display
 * @param {number} lines the body lines it must hold
 * @returns {{ width: number, height: number }} the display's outer size
 */
export function statusSize(span, lines) {
    const width = Math.max(span, STATUS_MIN_WIDTH);
    return { width, height: (TITLE_BAND + LAMP_BAND) * width + (lines + 2 * MARGIN) * LINE * width };
}

/**
 * @param {number} scale
 * @returns {Vec3} a seat button's plate
 */
export function seatSize(scale) {
    return { x: SEAT_SIZE.x * scale, y: SEAT_SIZE.y * scale, z: PLATE_DEPTH * scale };
}

export class TableControls {
    /** @type {(string | null)[]} each seat's player */ players = [null, null];
    /** @type {number | null} the seat to move */ turn = null;

    /**
     * @param {Uuid} parentID the board
     * @param {string} clientURL the client script the seat buttons run
     * @param {ControlsLook} look
     * @param {Side[]} sides seat 0's, then seat 1's
     * @param {string} title the game's name, over the screen
     */
    constructor(parentID, clientURL, look, sides, title) {
        this.parentID = parentID;
        this.clientURL = clientURL;
        this.look = look;
        this.sides = sides;
        this.title = title;
        this.names = [STATUS_NAME, FRAME_NAME, SCREEN_NAME, TITLE_NAME, LAMP_NAME, TURN_NAME,
            ...SEAT_NAMES.flatMap(seat => [seat, seat + PLATE, seat + CAP, seat + TOKEN])];
        // The children whose look follows the table
        this.live = [TURN_NAME, ...SEAT_NAMES.flatMap(seat => [seat, seat + PLATE])];
    }

    /**
     * @param {string} name
     * @returns {boolean} whether the child of this name is one of the controls
     */
    has(name) {
        return this.names.includes(name);
    }

    /**
     * Takes the table as published.
     * @param {({ name: string } | null)[]} seats
     * @param {number | null} turn the seat to move, or null when no game is being played
     * @returns {string[]} the children whose look changed, to edit with feedback()
     */
    update(seats, turn) {
        const before = this.live.map(name => JSON.stringify(this.feedback(name)));
        this.players = seats.map(seat => seat ? seat.name : null);
        this.turn = turn;
        return this.live.filter((name, i) => JSON.stringify(this.feedback(name)) !== before[i]);
    }

    /**
     * @param {number} seat
     * @returns {string} what the seat's button says: what a click does, or who sits there
     */
    label(seat) {
        const player = this.players[seat];
        if (!player) {
            return "Play " + this.sides[seat].name;
        }
        return player.length > LABEL_CHARS ? player.slice(0, LABEL_CHARS - 2) + ".." : player;
    }

    /**
     * @param {string} name
     * @returns {Partial<Entities.EntityProperties>} the part of the child's look that follows the table
     */
    feedback(name) {
        if (name === TURN_NAME) {
            return this.turn === null ? { visible: false } : { visible: true, color: this.sides[this.turn].color };
        }
        const seat = SEAT_NAMES.indexOf(name);
        if (seat >= 0) {
            return { text: this.label(seat) };
        }
        const plate = SEAT_NAMES.map(seatName => seatName + PLATE).indexOf(name);
        if (plate >= 0) {
            // The seat to move lights up
            return this.turn === plate ? { color: this.look.glow, unlit: true } : { color: this.look.plate, unlit: false };
        }
        return {};
    }

    /**
     * @param {string} name one of `names`
     * @param {Placement} place
     * @returns {Entities.EntityProperties} the child to create
     */
    properties(name, place) {
        const seat = SEAT_NAMES.findIndex(seatName => name.startsWith(seatName));
        const properties = seat >= 0
            ? this.seatPart(seat, name.slice(SEAT_NAMES[seat].length), place.seats[seat], place.scale)
            : this.statusPart(name, place.status);
        return /** @type {Entities.EntityProperties} */ ({ ...properties, ...this.feedback(name) });
    }

    /**
     * @param {string} name
     * @param {StatusPlace} place
     * @returns {Entities.EntityProperties}
     */
    statusPart(name, { centre, width, lines }) {
        const look = this.look;
        const line = LINE * width;
        const screen = { x: (1 - 2 * SIDE_BEZEL) * width, y: (lines + 2 * MARGIN) * line };
        const height = statusSize(width, lines).height;
        const at = (/** @type {number} */ y) => ({ x: centre.x, y: centre.y + y, z: centre.z });
        const screenY = height / 2 - TITLE_BAND * width - screen.y / 2;
        const lampY = LAMP_BAND * width / 2 - height / 2;
        const common = {
            name,
            parentID: this.parentID,
            billboardMode: /** @type {const} */ ("yaw"),
            collisionless: true,
            grab: { grabbable: false }
        };
        // Decoration: clicks and agents' picks go through it
        const part = { ...common, ignorePickIntersection: true };
        if (name === STATUS_NAME) {
            // The whole status as text, as agents read it; its own background is the screen's colour
            const margin = MARGIN * line;
            return {
                ...common,
                type: "Text",
                localPosition: at(screenY),
                dimensions: { x: screen.x, y: screen.y, z: TEXT_DEPTH },
                font: "Inconsolata",
                lineHeight: line,
                textColor: look.text,
                backgroundColor: look.screen,
                backgroundAlpha: 1,
                leftMargin: margin,
                rightMargin: margin,
                topMargin: margin,
                bottomMargin: margin,
                unlit: true
            };
        }
        if (name === FRAME_NAME) {
            return { ...part, type: "Box", localPosition: at(0), dimensions: { x: width, y: height, z: FRAME_DEPTH }, color: look.bezel };
        }
        if (name === SCREEN_NAME) {
            return {
                ...part,
                type: "Box",
                localPosition: at(screenY),
                dimensions: { x: screen.x, y: screen.y, z: SCREEN_DEPTH },
                color: look.screen,
                unlit: true
            };
        }
        if (name === TITLE_NAME) {
            return {
                ...part,
                type: "Text",
                localPosition: at(height / 2 - TITLE_BAND * width / 2),
                dimensions: { x: screen.x, y: TITLE_BAND * width, z: TEXT_DEPTH },
                text: this.title,
                font: look.titleFont,
                lineHeight: TITLE_LINE * width,
                textColor: look.title,
                backgroundAlpha: 0,
                alignment: "center",
                verticalAlignment: "center",
                unlit: true
            };
        }
        // The lamp: a ring on the frame, and in it a disc of the colour of the side to move
        const lamp = name === LAMP_NAME;
        const size = (lamp ? LAMP_SIZE : TURN_SIZE) * width;
        return {
            ...part,
            type: "Shape",
            shape: "Cylinder",
            localPosition: at(lampY),
            dimensions: { x: size, y: lamp ? LAMP_DEPTH : TURN_DEPTH, z: size },
            color: lamp ? look.ring : this.sides[0].color
        };
    }

    /**
     * @param {number} seat
     * @param {string} part "", "Plate", "Cap" or "Token"
     * @param {SeatPlace} place
     * @param {number} scale
     * @returns {Entities.EntityProperties}
     */
    seatPart(seat, part, { position, facing }, scale) {
        const look = this.look;
        const plate = seatSize(scale);
        const cap = { x: plate.x - 2 * RIM * scale, y: plate.y - 2 * RIM * scale, z: CAP_DEPTH * scale };
        const capFront = plate.z / 2 + cap.z;
        const token = TOKEN_FILL * cap.y;
        const pad = (cap.y - token) / 2;
        // A point `across` to the right and `out` in front of the plate's centre, as its player sees it
        const at = (/** @type {number} */ across, /** @type {number} */ out) =>
            ({ x: position.x + facing * across, y: position.y, z: position.z + facing * out });
        const rotation = facing > 0 ? IDENTITY : HALF_TURN_Y;
        const common = {
            name: SEAT_NAMES[seat] + part,
            parentID: this.parentID,
            localRotation: rotation,
            collisionless: true
        };
        // Clicks go through the decoration to the label, which covers the whole button
        const solid = { ...common, grab: { grabbable: false }, ignorePickIntersection: true };
        if (part === PLATE) {
            return { ...solid, type: "Box", localPosition: at(0, 0), dimensions: plate, color: look.plate };
        }
        if (part === CAP) {
            return { ...solid, type: "Box", localPosition: at(0, plate.z / 2 + cap.z / 2), dimensions: cap, color: look.cap };
        }
        if (part === TOKEN) {
            return {
                ...solid,
                type: "Shape",
                shape: "Cylinder",
                localPosition: at(pad + token / 2 - cap.x / 2, capFront + TOKEN_DEPTH * scale / 2),
                localRotation: QUARTER_TURN_X,
                dimensions: { x: token, y: TOKEN_DEPTH * scale, z: token },
                color: this.sides[seat].color
            };
        }
        // The label is the clickable: the client knows the seat by its name. It is the plate's
        // size, in front of everything else, so a click anywhere on the button lands on it.
        return {
            ...common,
            type: "Text",
            script: this.clientURL,
            grab: { grabbable: false, triggerable: true },
            localPosition: at(0, capFront + GLYPH_LIFT - TEXT_DEPTH / 2),
            dimensions: { x: plate.x, y: plate.y, z: TEXT_DEPTH },
            font: look.labelFont,
            lineHeight: LABEL_LINE * scale,
            textColor: look.label,
            backgroundAlpha: 0,
            leftMargin: RIM * scale + 2 * pad + token,
            rightMargin: RIM * scale + pad,
            topMargin: RIM * scale,
            bottomMargin: RIM * scale,
            alignment: "center",
            verticalAlignment: "center",
            unlit: true
        };
    }
}
