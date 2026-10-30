# Sun and Sky

A day and night for a domain: every day is an equinox day on the equator, with solar noon at a
fixed hour of UTC (Oceanus ADR 0020,
`oceanus/docs/decisions/0020-the-sun-keeps-an-equinox-day-with-its-noon-at-2-pm-eastern-in-a-sky-a-shader-paints.md`).
The sun rises due east, passes straight overhead at noon and sets due west, the same every day of
the year: no declination, no equation of time, no daylight saving. A shader paints the sky from
each visitor's own clock, so everyone sees the same hour with no edits; a server script moves the
light, which a shader cannot do, once a minute.

| File | Runs in | What it is |
| --- | --- | --- |
| `sun-sky.mjs` | anywhere | Pure logic: where the sun stands at a moment, the phase of the day for its elevation, the sky zone's light for it, and the daily phase times |
| `sun-sky-server.mjs` | entity script server | The sky zone's `serverScripts`: lights the zone once a minute, sets the other zones' ambient light and haze colour, switches the lamps, announces each change of phase |
| `sun-sky.fs` | Interface (GPU) | The sky zone's procedural skybox: day sky and sun, twilight band, night and stars, the sea below the horizon in the haze colour |

The server script needs the fork's ES module support for entity scripts (`.mjs` `serverScripts`
with `export default class`); it imports `sun-sky.mjs` by relative URL, so the two must be served
from the same directory. The shader runs in a stock Interface. Importing `sun-sky.mjs` from a
client script (to ask whether it is night, say) needs the fork's Interface.

The day is computed from the clock, never stored. Noon has no default anywhere: the shader takes
it as a uniform and the server script from `userData.sunSky`, and both must be set.

## The sun

The hour angle is H = 15 deg x (UTC hours - `noonUtc`), in (-180, 180], negative before noon. In
the world frame (+y up, north -z, east +x) the unit vector toward the sun is
(-sin H, cos H, 0): (1, 0, 0), due east, six hours before noon. Its elevation is 90 - |H| and its
compass bearing (clockwise from north) is 90 from midnight to noon and 270 after. The key light
points the other way, from the sun down.

| Phase | The sun's centre | At `noonUtc` 19 (UTC) | Eastern Standard Time |
| --- | --- | --- | --- |
| `day` | above -0.833 deg | 12:57 to 01:03 | 07:57 to 20:03 |
| `twilight` | -0.833 to -6 deg (civil twilight) | 12:36 to 12:57 and 01:03 to 01:24 | 07:36 to 07:57 and 20:03 to 20:24 |
| `night` | below -6 deg | 01:24 to 12:36 | 20:24 to 07:36 |

`phaseTimes(noonUtc)` gives the four transitions as hours of UTC: first light (night to
twilight), sunrise, sunset and dark (twilight to night). Each is noonUtc -/+ (90 - elevation) / 15
hours: 6 h 3 min 20 s from noon for sunrise and sunset, 6 h 24 min for first light and dark.

```js
import { sunPosition, phase, lightFor, phaseTimes } from "./sun-sky.mjs";

const sun = sunPosition(Date.now(), 19);         // { hourAngle, azimuth, elevation, direction }
phase(sun.elevation);                            // "day" | "twilight" | "night"
lightFor(sun.elevation, sun.direction);          // the sky zone's light, below
phaseTimes(19);                                  // { firstLight: 12.6, sunrise: 12.94, sunset: 1.06, dark: 1.4 }
```

`lightFor` takes the direction toward the sun as well as the elevation, since the elevation alone
does not say whether the sun is in the east or the west.

## The light

`lightFor(elevation, towardSun)` returns the zone property groups the server script writes:

```json
{
    "keyLight": { "direction": { "x": 0, "y": -1, "z": 0 }, "color": { "red": 255, "green": 245, "blue": 225 }, "intensity": 2.5, "castShadows": true },
    "ambientLight": { "ambientIntensity": 0.5, "ambientColor": { "red": 255, "green": 255, "blue": 255 } },
    "haze": { "hazeColor": { "red": 160, "green": 184, "blue": 197 } }
}
```

The curve is `LIGHT_KEYS`: rows by the sun's elevation, interpolated linearly between rows and
held beyond the first and last. From 30 deg up it is the look Delos had under its fixed afternoon
sun (Oceanus `tools/ground.py`: key light 255, 245, 225 at 2.5; ambient 0.5 over a white ambient
colour; haze 160, 184, 197), so the middle eight hours of the day look as before. Below that the
key light warms and dims to nothing at sunset; it casts shadows only while the sun is up. At night
a cool ambient floor keeps the island walkable.

| Elevation (deg) | Key colour | Key intensity | Ambient intensity | Ambient colour | Haze colour |
| --- | --- | --- | --- | --- | --- |
| -6 and below (night) | 255, 140, 80 | 0 | 0.10 | 150, 165, 200 | 22, 30, 45 |
| -0.833 (sunset) | 255, 150, 90 | 0 | 0.22 | 230, 190, 190 | 128, 116, 120 |
| 5 | 255, 190, 140 | 1.0 | 0.35 | 255, 225, 205 | 170, 160, 150 |
| 15 | 255, 225, 190 | 1.9 | 0.45 | 255, 245, 238 | 165, 180, 190 |
| 30 and above (day) | 255, 245, 225 | 2.5 | 0.50 | 255, 255, 255 | 160, 184, 197 |

- The named constants are in `sun-sky.mjs`: `DAY_KEY_COLOR`, `DAY_KEY_INTENSITY`,
  `DAY_AMBIENT_INTENSITY`, `DAY_AMBIENT_COLOR`, `DAY_HAZE_COLOR`, `FULL_DAY_ELEVATION` (30),
  `NIGHT_AMBIENT_INTENSITY` (0.10), `NIGHT_AMBIENT_COLOR`, `NIGHT_HAZE_COLOR`, `NIGHT_KEY_COLOR`,
  `SUNSET_ELEVATION` (-0.833) and `DARK_ELEVATION` (-6).
- The key colour below sunset is never seen (the intensity is 0); it only keeps the curve
  continuous.
- With an ambient map (`ambientURL`), the ambient colour tints the map; without one it is the
  ambient light's colour.
- Intensities are rounded to 3 decimals and colours to whole units.

## The sky shader

`sun-sky.fs` supplies `getSkyboxColor()` for the view direction `_normal` (the fork's
`libraries/procedural/src/procedural/proceduralSkybox.slf`), in gamma space. It reads the time of
day from `iDate.w`, the client's seconds since UTC midnight (`Procedural.cpp`), and noon from
`uniform float noonUtc`, and works out the sun exactly as `sun-sky.mjs` does. It paints:

- **Day:** a gradient from the zenith (a deep blue) to the horizon, where the sky takes the haze
  colour of the moment, so sky and sea meet without a seam; the sun's disc (a little over a degree
  across) and its glow, warm and orange when the sun is low.
- **Twilight:** a warm band low on the horizon, strongest with the sun 2 deg below it and towards
  the sun's side, fading out by the time the sun is 6 deg up or 10 deg down.
- **Night:** a dark blue sky and a hash-based starfield, the same every night. At the equator the
  celestial poles lie on the horizon due north and south, so the stars wheel about the
  north-south (z) axis with the sun, rising in the east. They fade in from 3 deg to 9 deg below
  the horizon and thin out just above it. The sky turns at the solar rate, so the stars stand
  where they stood at the same hour yesterday.
- **Below the horizon:** a flat sea in the haze colour of the moment (`LIGHT_KEYS`' haze column,
  repeated in the shader as `HAZE_COLOURS`), so it meets the zone's range haze at every hour.

There is no moon and there are no clouds (ADR 0020's open questions). The shader's sun position
and haze curve are tested against the module's numbers (`test/sun-sky-shader.test.mjs`); change
both together.

The zone's skybox must be enabled (`skyboxMode: "enabled"`). The procedural sky replaces the
skybox once its shader has loaded; until then the zone's plain skybox (`skybox.url` or
`skybox.color`) is drawn. A visitor who has turned procedural shaders off ("Material Procedural
Shaders", `Render.proceduralMaterialsEnabled`, on by default) gets the engine's built-in
placeholder sky instead, and a shader that fails to compile gets the engine's error sky; neither
falls back to the plain skybox.

## Placing it

All of it goes on the sky zone: the shader in `userData.ProceduralEntity`, the server script's
configuration in `userData.sunSky`, and the script in `serverScripts`. The script never writes
`userData`.

```json
{
    "ProceduralEntity": {
        "version": 4,
        "shaderUrl": "https://<asset-host>/sunSky/sun-sky.fs",
        "uniforms": { "noonUtc": 19.0 }
    },
    "sunSky": {
        "noonUtc": 19.0,
        "zoneNames": ["<other zone name>"],
        "lampNames": ["<lamp name 01>", "<lamp name 02>"],
        "searchRadius": 2500
    }
}
```

- **`ProceduralEntity`:** `version` 4, the newest the engine accepts (1 to 4,
  `Procedural.cpp`). For a skybox the version changes nothing but the `PROCEDURAL_V4` define the
  engine adds: the entry point is `getSkyboxColor()` in every version. `shaderUrl` is the
  published `sun-sky.fs`. `uniforms.noonUtc` is the hour of solar noon in UTC; if it is left out
  the uniform is 0 and the painted sun has its noon at midnight UTC. No `channels`.
- **`sunSky.noonUtc`:** the same hour, 0 to 24 (19.0 for Delos, ADR 0020). Required: without a
  number in range the script logs once, naming the key, and does nothing. If the shader's
  uniform differs, it logs that the painted sun and the light disagree, and runs on
  `sunSky.noonUtc`.
- **`sunSky.zoneNames`:** other zones that take the sky zone's ambient light (intensity and
  colour) and haze colour, and nothing else. Optional, default none.
- **`sunSky.lampNames`:** entities shown from sunset to sunrise and hidden by day. Optional,
  default none.
- **`sunSky.searchRadius`:** metres from the sky zone's position within which the named entities
  are found. Required when either list names anything, and no default.
- Names match exactly (`Entities.findEntitiesByName`, case-sensitive). Every entity of a name is
  switched, and the sky zone itself is never one of them.
- **The entity script server sees only entities with a `serverScripts`, and their parents and
  children** (its entity query filters on `serverScripts`; see the note on
  `Entities.findEntitiesByName`). A lamp or a zone named here must therefore carry a
  `serverScripts` of its own, or be a child (or parent) of an entity that does, such as the sky
  zone; one the server cannot see is never found, and stays as it was.
- The configuration is read when the script loads. Reload the script after changing it.
- Do not lock the sky zone, the other zones or the lamps. A locked entity takes no edit but to
  `locked`; the script logs once if the sky zone is locked, and its edits do nothing.

## What the server script does

- **On load, then once a minute** (`TICK_MS`, 60 000 ms; the sun moves a quarter of a degree in
  that time), it works out the sun for `Date.now()` and edits the sky zone's `keyLight`
  (direction, colour, intensity, castShadows), `ambientLight` (ambientIntensity, ambientColor)
  and `haze` (hazeColor). Only those members are written; `ambientURL`, the haze range, the
  skybox and every other property keep their values. That is one edit a minute.
- **Other zones** get `ambientLight` (ambientIntensity, ambientColor) and `haze` (hazeColor), the
  sky zone's values, and only when those changed since the last edit to that zone: through the
  middle of the day and the night, nothing; through twilight and the low sun, about once a minute.
  They are found again on every tick, so one that arrives after load is lit at the next.
- **Lamps** are `visible` while the phase is not `day`, from sunset to sunrise, so they come on
  as the sun's centre sinks below -0.833 deg and go off as it rises above it. Each tick, every
  lamp whose `visible` is wrong is edited: one edit per lamp at sunset and at sunrise, and on load
  any lamp left wrong (by a server that was down at dusk, say) is put right.
- **Each change of phase** is announced on the Messages channel `oceanus.daynight`, at the first
  tick after it (so up to a minute late). The phase found at load is not a change and is not
  announced; a script that needs the phase on arrival works it out from the clock with
  `sun-sky.mjs`.
- **On unload** it clears its interval.
- It never writes `userData` (which holds the shader) and never `locked`.

### Messages channel

```json
{ "type": "phase", "phase": "twilight", "azimuth": 270, "elevation": -0.95 }
```

| Field | Meaning |
| --- | --- |
| `type` | Always `"phase"` |
| `phase` | The new phase: `"day"`, `"twilight"` or `"night"` |
| `azimuth` | The sun's compass bearing in degrees: 90 in the morning, 270 in the evening |
| `elevation` | The sun's elevation in degrees at the tick that saw the change |

The order through a day is `twilight` (first light), `day` (sunrise), `twilight` (sunset),
`night` (dark).

## Development

Node (22 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: the sun and the light, the server script with fake globals, the shader's numbers
npm run typecheck   # tsc twice: the server script against the server entity declarations, the module against the client ones
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`. The
shader test also compiles `sun-sky.fs` for GL 4.1 and GLES 3.1 when `glslangValidator` is on
`PATH`, and is skipped otherwise.

## Verified, and not yet

**Verified (2026-10-29):** the tests; the typecheck; the shader compiled by glslang for GL 4.1
and GLES 3.1, and rendered off-screen on an NVIDIA GPU (EGL, GL 4.1 core) in a stand-in for
`proceduralSkybox.slf` at 12:30, 12:45, 12:57, 13:30, 15:00, 19:00, 23:00, 00:54, 01:12 and
03:00 UTC with `noonUtc` 19: the sun rose in the east, stood at the zenith at 19:00 and set in the
west, the warm band showed at both twilights on the sun's side, and stars filled the night.

**Not yet seen in a domain:** the shader in Interface, the server script on an entity script
server, the edits reaching visitors, the lamps switching, and the sea's edge against the range
haze at each hour.
