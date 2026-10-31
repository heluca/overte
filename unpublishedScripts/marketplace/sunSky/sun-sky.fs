//
//  sun-sky.fs
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  A procedural skybox for an equinox day on the equator: a day sky with the sun's disc and
//  glow, a warm band at the horizon around sunrise and sunset, a dark night with stars that
//  wheel about the north-south axis, and below the horizon a flat sea in the haze colour.
//  The sun stands where sun-sky.mjs puts it, from the client's UTC clock (iDate.w) and the
//  noonUtc uniform. Output is in gamma space, as proceduralSkybox.slf expects.
//
//  World frame: +y up, north -z, east +x. Keep the sun's position and HAZE_COLOURS in step
//  with sun-sky.mjs (test/sun-sky-shader.test.mjs checks the numbers).
//

uniform float noonUtc;

const float DEGREES_PER_HOUR = 15.0;
const float SUNSET_ELEVATION = -0.833;
const float DARK_ELEVATION = -6.0;

// The zone's haze colour by the sun's elevation (sun-sky.mjs LIGHT_KEYS), 0-255, linear
// between rows and held beyond the ends. The sea is drawn in it so it meets the range haze.
const int KEY_COUNT = 5;
const float KEY_ELEVATIONS[KEY_COUNT] = float[KEY_COUNT](-6.0, -0.833, 5.0, 15.0, 30.0);
const vec3 HAZE_COLOURS[KEY_COUNT] = vec3[KEY_COUNT](
    vec3(22.0, 30.0, 45.0),
    vec3(128.0, 116.0, 120.0),
    vec3(170.0, 160.0, 150.0),
    vec3(165.0, 180.0, 190.0),
    vec3(160.0, 184.0, 197.0)
);

const vec3 DAY_ZENITH = vec3(0.24, 0.47, 0.82);
const vec3 NIGHT_ZENITH = vec3(0.008, 0.016, 0.05);
const vec3 TWILIGHT_COLOUR = vec3(1.0, 0.52, 0.24);
// The warm band is strongest with the sun this far below the horizon, fading over the width.
const float TWILIGHT_PEAK = -2.0;
const float TWILIGHT_WIDTH = 4.0;
const vec3 LOW_SUN_COLOUR = vec3(1.0, 0.55, 0.25);
const vec3 HIGH_SUN_COLOUR = vec3(1.0, 0.97, 0.9);
const vec3 SUN_DISC_COLOUR = vec3(1.0, 0.98, 0.94);
// cos(0.6 deg) and cos(0.45 deg): the disc's soft edge, a little larger than the real sun's.
const float SUN_DISC_OUTER = 0.9999452;
const float SUN_DISC_INNER = 0.9999692;
// Limb darkening: the disc's edge is this much dimmer than its centre.
const float SUN_LIMB_DARKENING = 0.12;

// Stars: one candidate per cell of a grid this many cells to the unit radius, standing in the
// middle of its cell so its disc (STAR_RADIUS cells) never crosses into the next.
const float STAR_GRID = 150.0;
const float STAR_RADIUS = 0.3;
const float STAR_DENSITY = 0.1;

float hourAngleDegrees() {
    float raw = DEGREES_PER_HOUR * (iDate.w / 3600.0 - noonUtc);
    return 180.0 - mod(180.0 - raw, 360.0);
}

vec3 hazeColour(float elevation) {
    vec3 colour = HAZE_COLOURS[0];
    for (int i = 1; i < KEY_COUNT; i++) {
        float t = clamp((elevation - KEY_ELEVATIONS[i - 1]) / (KEY_ELEVATIONS[i] - KEY_ELEVATIONS[i - 1]), 0.0, 1.0);
        colour = mix(colour, HAZE_COLOURS[i], t);
    }
    return colour / 255.0;
}

// Dave Hoskins's hash without sine: stable across GPUs.
vec3 hash33(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

// d: a unit direction in the sky's own frame, which turns with the sun.
vec3 stars(vec3 d) {
    vec3 p = d * STAR_GRID;
    vec3 cell = floor(p);
    vec3 place = hash33(cell);
    vec3 look = hash33(cell + 17.0);
    vec3 star = cell + 0.3 + 0.4 * place;
    float shape = 1.0 - smoothstep(0.0, STAR_RADIUS, length(p - star));
    float present = step(1.0 - STAR_DENSITY, look.x);
    float brightness = 0.3 + 1.2 * look.y * look.y * look.y;
    vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), look.z);
    return tint * (present * brightness * shape);
}

vec3 getSkyboxColor() {
    vec3 n = normalize(_normal);
    float hourAngle = hourAngleDegrees();
    float h = radians(hourAngle);
    float c = cos(h);
    float s = sin(h);
    vec3 sun = vec3(-s, c, 0.0);
    float elevation = 90.0 - abs(hourAngle);
    vec3 haze = hazeColour(elevation);
    if (n.y < 0.0) {
        return haze;
    }

    // Zenith to horizon; the horizon is the haze colour, so sky and sea meet without a seam.
    float day = smoothstep(DARK_ELEVATION, 10.0, elevation);
    vec3 sky = mix(haze, mix(NIGHT_ZENITH, DAY_ZENITH, day), sqrt(n.y));

    // Twilight: a warm band low on the horizon, strongest towards the sun.
    float fromPeak = (elevation - TWILIGHT_PEAK) / TWILIGHT_WIDTH;
    float side = 0.5 + 0.5 * dot(n, sun);
    float band = exp(-fromPeak * fromPeak) * exp(-n.y * 6.0) * (0.25 + 0.75 * side * side);
    sky = mix(sky, TWILIGHT_COLOUR, clamp(0.85 * band, 0.0, 1.0));

    // The sun: glow, fading out after sunset, and the disc over it. Output is clamped to 1, so
    // the glow must stay below white beside the disc or the disc vanishes into it: the tight term
    // falls to half 1.5 degrees out, and with the day zenith it stays under the disc's limb.
    float cosAngle = max(dot(n, sun), 0.0);
    vec3 sunColour = mix(LOW_SUN_COLOUR, HIGH_SUN_COLOUR, smoothstep(SUNSET_ELEVATION, 15.0, elevation));
    float glow = 0.3 * pow(cosAngle, 2000.0) + 0.18 * pow(cosAngle, 16.0);
    sky += sunColour * glow * smoothstep(DARK_ELEVATION - 2.0, SUNSET_ELEVATION, elevation);
    // (1 - cos) over (1 - cos R) is the squared fraction of the disc's radius, for small angles.
    float limbRadius2 = clamp((1.0 - cosAngle) / (1.0 - SUN_DISC_OUTER), 0.0, 1.0);
    float limb = 1.0 - SUN_LIMB_DARKENING * (1.0 - sqrt(1.0 - limbRadius2));
    sky = mix(min(sky, vec3(1.0)), SUN_DISC_COLOUR * limb, smoothstep(SUN_DISC_OUTER, SUN_DISC_INNER, cosAngle));

    // Stars, fixed in the sky's frame: at the equator the celestial poles lie on the horizon
    // due north and south, so the sky turns with the sun about the z axis. Turning the view back
    // by the hour angle keeps every star where it was, night after night.
    float night = 1.0 - smoothstep(-9.0, -3.0, elevation);
    if (night > 0.0) {
        vec3 celestial = vec3(n.x * c + n.y * s, n.y * c - n.x * s, n.z);
        sky += stars(celestial) * night * smoothstep(0.0, 0.08, n.y);
    }
    return clamp(sky, 0.0, 1.0);
}
