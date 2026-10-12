//
//  layout-controls.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The status display and the seat buttons on both layouts: their size rule, how their parts
//  stack and take clicks, and how the referee shows the table on them.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { BOARD, fakeEngine } from "./fake-engine.mjs";
import { Connect4Layout } from "../layout-connect4.mjs";
import { GridLayout } from "../layout-grid.mjs";
import { GAMES } from "../game-registry.mjs";

const { default: GameTableReferee } = await import("../gametable-referee.mjs");

const BASE_URL = "https://host/gameTable/";
const README = { x: 0.75, y: 0.65, z: 0.04 };
const LARGE = { x: 1.6, y: 1.4, z: 0.1 };
const FLAT = { x: 0.6, y: 0.04, z: 0.6 };
const FLAT_LARGE = { x: 1.2, y: 0.06, z: 1.2 };
const EPSILON = 1e-9;
// A Text draws its glyphs this far in front of its centre
const GLYPHS = 0.005;
const STATUS_PARTS = ["GT.Status", "GT.StatusFrame", "GT.StatusScreen", "GT.StatusTitle", "GT.StatusLamp", "GT.StatusTurn"];
const LIVE = ["GT.StatusTurn", "GT.Seat0", "GT.Seat1", "GT.Seat0Plate", "GT.Seat1Plate"];
const ALICE = "{aaaaaaaa-0000-4000-8000-000000000001}";
const BOB = "{bbbbbbbb-0000-4000-8000-000000000002}";

const BOARDS = [
    { label: "the README Connect Four board", layout: () => new Connect4Layout(BOARD, README, BASE_URL), lines: 11 },
    { label: "the acceptance Connect Four board", layout: () => new Connect4Layout(BOARD, LARGE, BASE_URL), lines: 11 },
    { label: "the README checkers board", layout: () => new GridLayout(BOARD, FLAT, BASE_URL, GAMES.checkers), lines: 13 },
    { label: "a large checkers board", layout: () => new GridLayout(BOARD, FLAT_LARGE, BASE_URL, GAMES.checkers), lines: 13 }
];

function close(actual, expected, message) {
    assert.ok(Math.abs(actual - expected) < 1e-6, `${message}: ${actual} is not ${expected}`);
}

// Loads a referee and lets its child search run out, so it creates the controls.
function loaded(engine) {
    const referee = new GameTableReferee();
    referee.preload(BOARD);
    engine.Script.clearTimeout(referee.searchTimer);
    referee.findChildren(0);
    return referee;
}

function idOf(engine, name) {
    const found = [...engine.entities].filter(([, entity]) => entity.name === name);
    assert.equal(found.length, 1, name);
    return found[0][0];
}

test("the status display is as wide as the board under it, but no narrower than 0.8 m", () => {
    const widths = BOARDS.map(({ layout }) => layout().fixedProperties("GT.StatusFrame").dimensions.x);
    // As wide as the Connect Four base, rails and overhang included
    BOARDS.slice(0, 2).forEach(({ layout }, i) => close(widths[i], layout().frame.baseWidth, "the base's width"));
    assert.ok(widths[1] < 1.9, "about 1.8 m over the 1.6 m board");
    close(widths[2], 0.8, "no narrower than 0.8 m");
    close(widths[3], 1.2, "as wide as a large 8x8 board");
    BOARDS.forEach(({ layout }, i) => {
        const status = layout().fixedProperties("GT.Status");
        close(status.lineHeight, widths[i] / 28, "the text scales with the display");
    });
});

for (const { label, layout: make, lines } of BOARDS) {
    test(`${label}: the display's parts turn together and stack in depth behind the text`, () => {
        const layout = make();
        const [status, frame, screen, title, lamp, turn] = STATUS_PARTS.map(name => layout.fixedProperties(name));
        [status, frame, screen, title, lamp, turn].forEach(part => {
            assert.equal(part.parentID, BOARD);
            assert.equal(part.billboardMode, "yaw");
            // Each turns about its own centre: all on one vertical line, unrotated
            assert.equal(part.localPosition.x, status.localPosition.x);
            assert.equal(part.localPosition.z, status.localPosition.z);
            assert.equal(part.localRotation, undefined);
            assert.equal(part.collisionless, true);
            assert.equal(part.script, undefined);
        });
        [frame, screen, title, lamp, turn].forEach(part => assert.equal(part.ignorePickIntersection, true));
        // The frame behind the lamp and the screen, the screen behind the glyphs, the disc proud of the lamp
        assert.equal(lamp.shape, "Cylinder");
        assert.ok(frame.dimensions.z < lamp.dimensions.y && lamp.dimensions.y < screen.dimensions.z);
        assert.ok(screen.dimensions.z / 2 < GLYPHS && turn.dimensions.y > lamp.dimensions.y);
        assert.ok(turn.dimensions.x < lamp.dimensions.x, "the disc sits in the lamp's ring");
        // Title band over the screen, lamp under it, all inside the frame
        const top = frame.localPosition.y + frame.dimensions.y / 2;
        const bottom = frame.localPosition.y - frame.dimensions.y / 2;
        const screenTop = screen.localPosition.y + screen.dimensions.y / 2;
        const screenBottom = screen.localPosition.y - screen.dimensions.y / 2;
        assert.ok(title.localPosition.y - title.dimensions.y / 2 >= screenTop - EPSILON);
        assert.ok(title.localPosition.y + title.dimensions.y / 2 <= top + EPSILON);
        assert.ok(lamp.localPosition.y + lamp.dimensions.x / 2 < screenBottom);
        assert.ok(lamp.localPosition.y - lamp.dimensions.x / 2 > bottom);
        assert.ok(screen.dimensions.x < frame.dimensions.x);
        // GT.Status is the screen's text, on the screen's colour, and holds the lines it must
        assert.equal(status.type, "Text");
        assert.equal(status.font, "Inconsolata");
        assert.deepEqual([status.localPosition.y, status.dimensions.x, status.dimensions.y],
            [screen.localPosition.y, screen.dimensions.x, screen.dimensions.y]);
        assert.deepEqual(status.backgroundColor, screen.color);
        assert.equal(status.backgroundAlpha, 1);
        assert.ok((status.dimensions.y - status.topMargin - status.bottomMargin) / status.lineHeight >= lines - EPSILON);
        assert.ok(status.leftMargin > 0 && status.rightMargin > 0);
        assert.equal(title.text, layout instanceof Connect4Layout ? "CONNECT FOUR" : "CHECKERS");
        assert.equal(title.alignment, "center");
        assert.equal(turn.visible, false, "no turn until a game starts");
    });

    test(`${label}: a seat button's label covers it and takes the clicks; cap, token and glyphs stack towards the player`, () => {
        const layout = make();
        [0, 1].forEach(seat => {
            const [label, plate, cap, token] = ["", "Plate", "Cap", "Token"].map(part => layout.fixedProperties("GT.Seat" + seat + part));
            assert.equal(label.type, "Text");
            assert.match(label.script, /^https:\/\/host\/gameTable\/gametable-client\.mjs$/);
            assert.deepEqual(label.grab, { grabbable: false, triggerable: true });
            assert.equal(label.ignorePickIntersection, undefined, "clickable");
            assert.deepEqual([label.dimensions.x, label.dimensions.y], [plate.dimensions.x, plate.dimensions.y]);
            assert.deepEqual([label.localPosition.x, label.localPosition.y], [plate.localPosition.x, plate.localPosition.y]);
            assert.deepEqual(label.localRotation, plate.localRotation);
            [plate, cap, token].forEach(part => {
                assert.equal(part.ignorePickIntersection, true);
                assert.equal(part.script, undefined);
                assert.equal(part.collisionless, true);
            });
            const facing = Math.sign(cap.localPosition.z - plate.localPosition.z);
            const out = part => facing * (part.localPosition.z - plate.localPosition.z);
            const capFront = out(cap) + cap.dimensions.z / 2;
            close(out(cap) - cap.dimensions.z / 2, plate.dimensions.z / 2, "the cap sits on the plate");
            assert.ok(cap.dimensions.x < plate.dimensions.x && cap.dimensions.y < plate.dimensions.y, "a rim shows round the cap");
            close(out(token) - token.dimensions.y / 2, capFront, "the token sits on the cap");
            const glyphs = out(label) + GLYPHS;
            assert.ok(glyphs > capFront && glyphs < capFront + 0.002, "the label's glyphs just in front of the cap");
            // The token at the player's left, inside the cap; the label's text clear of it
            const across = facing * (token.localPosition.x - plate.localPosition.x);
            assert.ok(across < 0 && -across + token.dimensions.x / 2 < cap.dimensions.x / 2);
            assert.ok(label.leftMargin > plate.dimensions.x / 2 + across + token.dimensions.x / 2);
            assert.equal(label.alignment, "center");
            assert.equal(label.verticalAlignment, "center");
        });
    });
}

test("8x8 seat buttons stand on the table at each player's edge, to their right, scaled with the squares", () => {
    for (const size of [FLAT, FLAT_LARGE]) {
        const layout = new GridLayout(BOARD, size, BASE_URL, GAMES.checkers);
        const scale = size.x / 8 / 0.075;
        const [black, red] = ["GT.Seat0Plate", "GT.Seat1Plate"].map(name => layout.fixedProperties(name));
        [black, red].forEach(plate => {
            close(plate.dimensions.x, 0.24 * scale, "plate width");
            close(plate.localPosition.y - plate.dimensions.y / 2, -size.y / 2, "on the table");
            assert.ok(Math.abs(plate.localPosition.x) + plate.dimensions.x / 2 <= size.x / 2 + EPSILON, "along the edge");
        });
        // Seat 0 plays from +z, where its right is +x; seat 1 from -z, facing it
        assert.ok(black.localPosition.z - black.dimensions.z / 2 > size.z / 2 && black.localPosition.x > 0);
        assert.ok(red.localPosition.z + red.dimensions.z / 2 < -size.z / 2 && red.localPosition.x < 0);
        assert.deepEqual(black.localRotation, { x: 0, y: 0, z: 0, w: 1 });
        assert.deepEqual(red.localRotation, { x: 0, y: 1, z: 0, w: 0 });
        assert.equal(layout.fixedProperties("GT.Seat0").text, "Play Black");
        assert.equal(layout.fixedProperties("GT.Seat1").text, "Play Red");
        const [blackToken, redToken] = ["GT.Seat0Token", "GT.Seat1Token"].map(name => layout.fixedProperties(name).color);
        assert.ok(blackToken.red < 60 && redToken.red > 150 && redToken.green < 60);
    }
});

test("the controls follow the table, and report only the children whose look changed", () => {
    const layout = new Connect4Layout(BOARD, README, BASE_URL);
    const controls = layout.controls;
    const alice = { name: "Alice" };
    const bob = { name: "Bob" };
    assert.deepEqual(controls.update([null, null], null), []);
    assert.deepEqual(controls.update([alice, null], null), ["GT.Seat0"]);
    assert.equal(layout.fixedProperties("GT.Seat0").text, "Alice", "a new child takes the current look");
    assert.deepEqual(controls.update([alice, bob], 0).sort(), ["GT.Seat0Plate", "GT.Seat1", "GT.StatusTurn"]);
    const red = layout.discProperties(0, 0, "R").color;
    assert.deepEqual(controls.feedback("GT.StatusTurn"), { visible: true, color: red });
    const lit = controls.feedback("GT.Seat0Plate");
    assert.equal(lit.unlit, true);
    assert.deepEqual(lit.color, layout.ringProperties(0, 0).color, "the mover's plate glows gold");
    assert.equal(controls.feedback("GT.Seat1Plate").unlit, false);
    assert.deepEqual(controls.update([alice, bob], 1).sort(), ["GT.Seat0Plate", "GT.Seat1Plate", "GT.StatusTurn"]);
    assert.deepEqual(controls.update([alice, bob], 1), [], "nothing changed");
    assert.deepEqual(controls.update([alice, bob], null).sort(), ["GT.Seat1Plate", "GT.StatusTurn"]);
    assert.deepEqual(controls.feedback("GT.StatusTurn"), { visible: false });
    controls.update([{ name: "Bartholomew the Great" }, null], null);
    assert.equal(controls.feedback("GT.Seat0").text, "Bartholome..");
    assert.equal(controls.feedback("GT.Seat1").text, "Play Black");
});

test("the referee edits a seat button or the lamp only when its look changes", () => {
    const engine = fakeEngine();
    const referee = loaded(engine);
    const ids = new Map(LIVE.map(name => [idOf(engine, name), name]));
    let seen = engine.edits.length;
    const liveEdits = () => {
        const edits = engine.edits.slice(seen).filter(([id]) => ids.has(id)).map(([id, properties]) => [ids.get(id), properties]);
        seen = engine.edits.length;
        return edits;
    };
    engine.call(referee, ALICE, "join", ["Alice"]);
    assert.deepEqual(liveEdits(), [["GT.Seat0", { text: "Alice" }]]);
    engine.call(referee, BOB, "join", ["Bob"]);
    assert.deepEqual(liveEdits().map(([name]) => name).sort(), ["GT.Seat0Plate", "GT.Seat1", "GT.StatusTurn"]);
    assert.equal(engine.entities.get(idOf(engine, "GT.Seat0Plate")).unlit, true, "Red to move: Red's plate glows");
    assert.equal(engine.entities.get(idOf(engine, "GT.StatusTurn")).visible, true);
    // The countdown redraws the status text, not the controls
    engine.runTimer(referee.timer);
    assert.deepEqual(liveEdits(), []);
    engine.call(referee, BOB, "move", ["3"]);
    assert.deepEqual(liveEdits(), [], "a refused move changes nothing");
    engine.call(referee, ALICE, "move", ["3"]);
    assert.deepEqual(liveEdits().map(([name]) => name).sort(), ["GT.Seat0Plate", "GT.Seat1Plate", "GT.StatusTurn"]);
    assert.equal(engine.entities.get(idOf(engine, "GT.Seat1Plate")).unlit, true);
    assert.equal(engine.entities.get(idOf(engine, "GT.Seat0Plate")).unlit, false);
    referee.unload();
    assert.deepEqual(liveEdits().map(([name]) => name).sort(), ["GT.Seat1Plate", "GT.StatusTurn"], "offline: no turn");
});

test("a locked board's controls keep the look they were laid out with", () => {
    const engine = fakeEngine("", true);
    const referee = loaded(engine);
    const ids = new Set(LIVE.map(name => idOf(engine, name)));
    const seen = engine.edits.length;
    engine.call(referee, ALICE, "join", ["Alice"]);
    engine.call(referee, BOB, "join", ["Bob"]);
    assert.deepEqual(engine.edits.slice(seen).filter(([id]) => ids.has(id)), []);
    assert.equal(engine.named("GT.Seat0")[0].text, "Play Red");
});

test("a board with the old plain status and seat Texts is restyled on load and gains its decoration once", () => {
    const engine = fakeEngine();
    const old = name => engine.Entities.addEntity({ name, parentID: BOARD, type: "Text", backgroundColor: { red: 20, green: 24, blue: 32 },
        backgroundAlpha: 0.85, dimensions: { x: 0.16, y: 0.06, z: 0.01 }, localPosition: { x: 0, y: 0, z: 0 } });
    const [status, seat] = ["GT.Status", "GT.Seat0"].map(old);
    loaded(engine);
    const count = engine.entities.size;
    assert.equal(idOf(engine, "GT.Status"), status, "the old Text is kept");
    assert.equal(idOf(engine, "GT.Seat0"), seat);
    const screen = engine.named("GT.StatusScreen")[0];
    assert.deepEqual(engine.entities.get(status).backgroundColor, screen.color);
    assert.equal(engine.entities.get(status).backgroundAlpha, 1);
    assert.equal(engine.entities.get(status).billboardMode, "yaw");
    assert.equal(engine.entities.get(seat).backgroundAlpha, 0, "the button's colours are its plate and cap now");
    const plate = engine.named("GT.Seat0Plate")[0].dimensions;
    assert.deepEqual([engine.entities.get(seat).dimensions.x, engine.entities.get(seat).dimensions.y], [plate.x, plate.y]);
    loaded(engine);
    assert.equal(engine.entities.size, count, "a reload adds nothing");
    [...STATUS_PARTS, ...LIVE, "GT.Seat0Cap", "GT.Seat1Token"].forEach(name => assert.equal(engine.named(name).length, 1, name));
});
