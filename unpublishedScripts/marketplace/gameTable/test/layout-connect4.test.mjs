//
//  layout-connect4.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The Connect Four board's geometry and look: holes, rails and base, discs, markers and the
//  winning rings, on the README's board and on a larger one; and the board's own colour.
//

import { test } from "node:test";
import assert from "node:assert/strict";

import { BOARD, fakeEngine } from "./fake-engine.mjs";
import { Connect4Layout, BOARD_COLOR } from "../layout-connect4.mjs";

const { default: GameTableReferee } = await import("../gametable-referee.mjs");

const README = { x: 0.75, y: 0.65, z: 0.04 };
const LARGE = { x: 1.6, y: 1.4, z: 0.1 };
const EPSILON = 1e-9;
const EMPTY_BOARD = Array.from({ length: 6 }, () => ".......");

/**
 * @param {{ x: number, y: number, z: number, w: number }} q
 * @param {{ x: number, y: number, z: number }} v
 * @returns {{ x: number, y: number, z: number }} v rotated by q
 */
function rotate(q, v) {
    // v + 2w(u x v) + 2u x (u x v), u the vector part of q
    const cross = (a, b) => ({ x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x });
    const u = { x: q.x, y: q.y, z: q.z };
    const t = cross(u, v);
    const tt = cross(u, t);
    return { x: v.x + 2 * q.w * t.x + 2 * tt.x, y: v.y + 2 * q.w * t.y + 2 * tt.y, z: v.z + 2 * q.w * t.z + 2 * tt.z };
}

function near(actual, expected, message) {
    ["x", "y", "z"].forEach(axis => assert.ok(Math.abs(actual[axis] - expected[axis]) < 1e-6,
        `${message}: ${JSON.stringify(actual)} is not ${JSON.stringify(expected)}`));
}

function brightness(color) {
    return color.red + color.green + color.blue;
}

function layoutFor(dimensions) {
    return new Connect4Layout(BOARD, dimensions, "https://host/gameTable/");
}

for (const [label, size] of [["the README board", README], ["a board twice the size", LARGE]]) {
    const cell = Math.min(size.x / 7, size.y / 6);

    test(`${label}: 42 dark holes through the board, one per cell, facing z`, () => {
        const layout = layoutFor(size);
        const holes = layout.fixedNames.filter(name => name.startsWith("GT.Hole "));
        assert.equal(holes.length, 42);
        holes.forEach(name => {
            const hole = layout.fixedProperties(name);
            assert.equal(hole.type, "Shape");
            assert.equal(hole.shape, "Cylinder");
            assert.equal(hole.parentID, BOARD);
            near(rotate(hole.localRotation, { x: 0, y: 1, z: 0 }), { x: 0, y: 0, z: 1 }, "the axis faces z");
            assert.ok(hole.dimensions.y > size.z, "proud of both faces");
            assert.equal(hole.dimensions.x, hole.dimensions.z, "round");
            assert.ok(hole.dimensions.x < cell, "board shows between holes");
            assert.equal(hole.localPosition.z, 0);
            assert.ok(Math.abs(hole.localPosition.x) + hole.dimensions.x / 2 < size.x / 2, "inside the board");
            assert.ok(Math.abs(hole.localPosition.y) + hole.dimensions.x / 2 < size.y / 2, "inside the board");
            assert.equal(hole.unlit, true);
            assert.equal(hole.collisionless, true);
            assert.equal(hole.ignorePickIntersection, true);
            assert.deepEqual(hole.grab, { grabbable: false });
            assert.ok(brightness(hole.color) < brightness(BOARD_COLOR) / 4, "much darker than the board");
        });
        // Row 0 is the top row, column 0 the left (-x)
        const topLeft = layout.fixedProperties("GT.Hole r0c0").localPosition;
        const bottomRight = layout.fixedProperties("GT.Hole r5c6").localPosition;
        near(topLeft, { x: -3 * size.x / 7, y: 2.5 * size.y / 6, z: 0 }, "r0c0");
        near(bottomRight, { x: 3 * size.x / 7, y: -2.5 * size.y / 6, z: 0 }, "r5c6");
    });

    test(`${label}: a disc covers its hole and stands proud of it; a winning ring shows round it`, () => {
        const layout = layoutFor(size);
        const hole = layout.fixedProperties("GT.Hole r3c2");
        const red = layout.discProperties(3, 2, "R");
        const black = layout.discProperties(3, 2, "B");
        const ring = layout.ringProperties(3, 2);
        [red, black, ring].forEach(shape => {
            assert.equal(shape.type, "Shape");
            assert.equal(shape.shape, "Cylinder");
            assert.deepEqual(shape.localPosition, hole.localPosition);
            assert.deepEqual(shape.localRotation, hole.localRotation);
            assert.equal(shape.collisionless, true);
            assert.equal(shape.ignorePickIntersection, true);
        });
        assert.equal(red.name, "GT.Disc r3c2");
        assert.equal(ring.name, "GT.Win r3c2");
        // Diameters: hole < disc < ring < cell (rings of a line do not overlap)
        assert.ok(hole.dimensions.x < red.dimensions.x && red.dimensions.x < ring.dimensions.x && ring.dimensions.x < cell);
        // Depths: board < hole < ring < disc, each face in front of the one behind it
        assert.ok(size.z < hole.dimensions.y && hole.dimensions.y < ring.dimensions.y && ring.dimensions.y < red.dimensions.y);
        assert.deepEqual(black.dimensions, red.dimensions);
        // The black disc is charcoal: clearly lighter than an empty hole
        assert.ok(brightness(black.color) > brightness(hole.color) + 90);
        assert.ok(red.color.red > 150 && red.color.green < 80 && red.color.blue < 80);
        assert.equal(ring.unlit, true, "glows whatever the light");
        assert.ok(brightness(ring.color) > 450);
        assert.equal(red.unlit, undefined, "discs are lit, so they have shading");
    });

    test(`${label}: side rails and a base frame the grid like the toy`, () => {
        const layout = layoutFor(size);
        const [left, right, base] = ["GT.Frame L", "GT.Frame R", "GT.Base"].map(name => layout.fixedProperties(name));
        [left, right, base].forEach(part => {
            assert.equal(part.type, "Box");
            assert.equal(part.parentID, BOARD);
            assert.equal(part.collisionless, true);
            assert.deepEqual(part.grab, { grabbable: false });
            assert.equal(part.ignorePickIntersection, true);
            assert.ok(brightness(part.color) < brightness(BOARD_COLOR), "darker than the board");
        });
        // Each rail's inner face is on the board's side; the rails reach from the base up past the top edge
        assert.ok(Math.abs(left.localPosition.x + left.dimensions.x / 2 + size.x / 2) < EPSILON);
        assert.ok(Math.abs(right.localPosition.x - right.dimensions.x / 2 - size.x / 2) < EPSILON);
        assert.deepEqual(left.dimensions, right.dimensions);
        assert.equal(left.localPosition.y, right.localPosition.y);
        assert.ok(Math.abs(left.localPosition.y - left.dimensions.y / 2 + size.y / 2) < EPSILON, "standing on the base");
        assert.ok(left.localPosition.y + left.dimensions.y / 2 > size.y / 2, "above the top edge");
        assert.ok(left.dimensions.z > size.z, "thicker than the grid");
        assert.ok(left.dimensions.x < cell / 2, "slim");
        // The base: right under the grid, wider than the rails, deeper than everything above it
        assert.ok(Math.abs(base.localPosition.y + base.dimensions.y / 2 + size.y / 2) < EPSILON);
        assert.ok(base.dimensions.x / 2 > right.localPosition.x + right.dimensions.x / 2);
        assert.ok(base.dimensions.z > left.dimensions.z);
        assert.deepEqual([base.localPosition.x, base.localPosition.z], [0, 0]);
    });

    test(`${label}: column markers point down over each column, under the status display`, () => {
        const layout = layoutFor(size);
        const status = layout.fixedProperties("GT.Status");
        for (let col = 0; col < 7; col++) {
            const marker = layout.fixedProperties("GT.Col" + col);
            const hole = layout.fixedProperties("GT.Hole r0c" + col);
            assert.equal(marker.type, "Shape");
            assert.equal(marker.shape, "Cone");
            near(rotate(marker.localRotation, { x: 0, y: 1, z: 0 }), { x: 0, y: -1, z: 0 }, "the apex points down");
            assert.equal(marker.localPosition.x, hole.localPosition.x);
            assert.equal(marker.localPosition.z, 0);
            assert.ok(marker.localPosition.y - marker.dimensions.y / 2 > size.y / 2, "above the top edge");
            assert.ok(marker.dimensions.x < cell, "clear of its neighbours");
            assert.ok(status.localPosition.y - status.dimensions.y / 2 > marker.localPosition.y + marker.dimensions.y / 2);
            assert.match(marker.script, /^https:\/\/host\/gameTable\/gametable-client\.mjs$/);
            assert.deepEqual(marker.grab, { grabbable: false, triggerable: true });
            assert.equal(marker.ignorePickIntersection, undefined, "clickable");
            assert.ok(Math.min(marker.color.red, marker.color.green, marker.color.blue) > 180, "a light neutral");
        }
    });

    test(`${label}: seat buttons stand beside the base, sized like the 8x8 boards' at the README's size`, () => {
        const layout = layoutFor(size);
        const scale = cell / (0.75 / 7);
        const base = layout.fixedProperties("GT.Base");
        const [red, black] = ["GT.Seat0", "GT.Seat1"].map(name => layout.fixedProperties(name));
        near(red.dimensions, { x: 0.16 * scale, y: 0.06 * scale, z: 0.01 * scale }, "seat size");
        assert.ok(Math.abs(red.lineHeight - 0.03 * scale) < EPSILON);
        assert.ok(red.localPosition.x + red.dimensions.x / 2 < -base.dimensions.x / 2, "left of the base");
        assert.ok(black.localPosition.x - black.dimensions.x / 2 > base.dimensions.x / 2, "right of the base");
        const floor = base.localPosition.y - base.dimensions.y / 2;
        assert.ok(Math.abs(red.localPosition.y - red.dimensions.y / 2 - floor) < EPSILON);
        assert.equal(red.text, "Play Red");
        assert.equal(black.text, "Play Black");
        const status = layout.fixedProperties("GT.Status");
        near(status.dimensions, { x: 1.0 * scale, y: 0.4 * scale, z: 0.01 * scale }, "status size");
        assert.equal(status.font, "Inconsolata");
    });
}

test("a move adds one disc; a win adds a ring per winning disc and changes no disc", () => {
    const engine = fakeEngine("", false, README);
    const layout = layoutFor(README);
    layout.draw({ board: EMPTY_BOARD, line: null });
    assert.equal(engine.entities.size, 1);
    const three = [".......", ".......", ".......", "R......", "R......", "R......"];
    layout.draw({ board: three, line: null });
    const discIDs = [...layout.discs.values()].map(disc => disc.id);
    const before = engine.entities.size;
    layout.draw({ board: [".......", ".......", "R......", "R......", "R......", "R......"], line: [[2, 0], [3, 0], [4, 0], [5, 0]] });
    assert.equal(engine.entities.size, before + 5, "one disc, four rings");
    discIDs.forEach(id => assert.ok(engine.entities.has(id), "the discs already there stay"));
    assert.equal(layout.rings.size, 4);
    assert.equal(layout.pieceIDs().size, 8);
    layout.clear();
    assert.equal(engine.entities.size, 1);
});

test("the referee paints a Connect Four board blue once, and leaves locked and 8x8 boards alone", () => {
    const engine = fakeEngine("", false, LARGE);
    const board = engine.entities.get(BOARD);
    board.color = { red: 255, green: 255, blue: 255 };
    new GameTableReferee().preload(BOARD);
    assert.deepEqual(board.color, BOARD_COLOR);
    const edits = engine.boardEdits;
    new GameTableReferee().preload(BOARD);
    // A reload publishes the table again, but the colour already matches
    assert.equal(engine.boardEdits, edits + 1);

    const locked = fakeEngine("", true, LARGE);
    locked.entities.get(BOARD).color = { red: 255, green: 255, blue: 255 };
    new GameTableReferee().preload(BOARD);
    assert.deepEqual(locked.entities.get(BOARD).color, { red: 255, green: 255, blue: 255 });
    assert.equal(locked.boardEdits, 0);

    const checkers = fakeEngine(JSON.stringify({ gameTableConfig: { game: "checkers" } }), false, { x: 0.6, y: 0.04, z: 0.6 });
    const brown = { red: 60, green: 40, blue: 25 };
    checkers.entities.get(BOARD).color = brown;
    new GameTableReferee().preload(BOARD);
    assert.deepEqual(checkers.entities.get(BOARD).color, brown);
});

test("a reload adopts the holes, rails and base without duplicating them", () => {
    const engine = fakeEngine("", false, LARGE);
    const first = new GameTableReferee();
    first.preload(BOARD);
    engine.Script.clearTimeout(first.searchTimer);
    first.findChildren(0);
    const count = engine.entities.size;
    const hole = engine.named("GT.Hole r2c4")[0];
    const second = new GameTableReferee();
    second.preload(BOARD);
    engine.Script.clearTimeout(second.searchTimer);
    second.findChildren(0);
    assert.equal(engine.entities.size, count);
    assert.equal(engine.named("GT.Hole r2c4")[0], hole);
    ["GT.Frame L", "GT.Frame R", "GT.Base"].forEach(name => assert.equal(engine.named(name).length, 1));
    assert.equal([...engine.entities.values()].filter(entity => (entity.name || "").startsWith("GT.Hole ")).length, 42);
});
