//
//  sun-sky-shader.test.mjs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  The sky shader cannot be rendered here. These check that its numbers are the module's (the
//  sun's day, the phase elevations, the haze curve the sea is painted in) and, where
//  glslangValidator is installed, that it compiles in a stand-in for proceduralSkybox.slf for
//  desktop GL 4.1 and GLES 3.1.
//

import { test } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { DARK_ELEVATION, DEGREES_PER_HOUR, LIGHT_KEYS, SUNSET_ELEVATION } from "../sun-sky.mjs";

const SOURCE = readFileSync(new URL("../sun-sky.fs", import.meta.url), "utf8");

/** @param {string} name */
function constant(name) {
    const match = SOURCE.match(new RegExp(`const (?:float|int) ${name} = ([-0-9.]+);`));
    assert.ok(match, `${name} not found`);
    return Number(match[1]);
}

/** @param {string} name the body of a `const T NAME[KEY_COUNT] = T[KEY_COUNT](...);` */
function arrayBody(name) {
    const match = SOURCE.match(new RegExp(`${name}\\[KEY_COUNT\\] = \\w+\\[KEY_COUNT\\]\\(([^;]*)\\);`));
    assert.ok(match, `${name} not found`);
    return match[1];
}

test("the shader's sun keeps the module's day", () => {
    assert.equal(constant("DEGREES_PER_HOUR"), DEGREES_PER_HOUR);
    assert.equal(constant("SUNSET_ELEVATION"), SUNSET_ELEVATION);
    assert.equal(constant("DARK_ELEVATION"), DARK_ELEVATION);
    assert.match(SOURCE, /^uniform float noonUtc;$/m);
    assert.match(SOURCE, /DEGREES_PER_HOUR \* \(iDate\.w \/ 3600\.0 - noonUtc\)/);
    assert.match(SOURCE, /return 180\.0 - mod\(180\.0 - raw, 360\.0\);/);
    assert.match(SOURCE, /vec3 sun = vec3\(-s, c, 0\.0\);/);
    assert.match(SOURCE, /float elevation = 90\.0 - abs\(hourAngle\);/);
    assert.match(SOURCE, /vec3 getSkyboxColor\(\) \{/);
});

test("the shader's haze curve is the module's, so the painted sea meets the zone's haze", () => {
    assert.equal(constant("KEY_COUNT"), LIGHT_KEYS.length);
    const elevations = arrayBody("KEY_ELEVATIONS").split(",").map(Number);
    assert.deepEqual(elevations, LIGHT_KEYS.map(row => row.elevation));
    const colours = [...arrayBody("HAZE_COLOURS").matchAll(/vec3\(([^)]*)\)/g)].map(m => m[1].split(",").map(Number));
    assert.deepEqual(colours, LIGHT_KEYS.map(({ hazeColor: { red, green, blue } }) => [red, green, blue]));
});

let glslang = false;
try {
    execFileSync("glslangValidator", ["--version"], { stdio: "ignore" });
    glslang = true;
} catch (e) {
    glslang = false;
}

// What proceduralSkybox.slf and ProceduralCommon.slh put round the block, cut to what it uses.
const PRELUDE = `layout(std140) uniform standardInputsBuffer {
    vec4 date; vec3 worldPosition; vec3 worldScale; float _spare0; float globalTime; float localCreatedTime;
    float entityTime; int frameCount; vec3 channelResolution[4]; mat3 worldOrientation; vec4 _spare2;
} standardInputs;
#define iDate standardInputs.date
in vec3 _normal;
out vec4 _fragColor;
`;
const MAIN = "void main(void) { vec3 color = getSkyboxColor(); _fragColor = vec4(pow(max(color, vec3(0)), vec3(2.2)), 1.0); }\n";

test("the shader compiles for GL 4.1 and GLES 3.1", { skip: glslang ? false : "glslangValidator is not installed" }, () => {
    const dir = mkdtempSync(join(tmpdir(), "sun-sky-"));
    try {
        for (const header of ["#version 410 core\n", "#version 310 es\nprecision highp float;\n"]) {
            const file = join(dir, "sky.frag");
            writeFileSync(file, header + PRELUDE + SOURCE + "\n" + MAIN);
            execFileSync("glslangValidator", ["-S", "frag", file], { stdio: "pipe" });
        }
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});
