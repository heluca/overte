# Piano

A proof of concept: an entity that is a speaker for a remote synthesizer, Orpheus, and shows
what it is playing. Nobody plays it from inside the world. LLM agents play Orpheus through
Orpheus's own MCP server, and people play the real piano it is connected to. The in-world side only listens and reads.
A bench lets one visitor sit at the keyboard; the desk display that opens for them is a
wireframe whose buttons reach the client script and stop there.

| File | Runs in | What it is |
| --- | --- | --- |
| `piano-core.mjs` | anywhere | Pure logic: the userData configuration, the compact status, the reconnect backoff, and the text shown |
| `piano-entity.mjs` | entity script server | The piano's `serverScripts`: plays the stream at the entity, mirrors the status into `userData`, answers `status` and `list` |
| `piano-client.mjs` | Interface | The piano's client `script`: a floating local Text above the piano with the status, the song list on a click, and the desk display for the sitter |
| `piano-bench-core.mjs` | anywhere | Pure logic for the bench: its `userData`, the sit and stand poses, the seat lease, the stand-up gesture |
| `piano-bench-entity.js` | entity script server | The bench's `serverScripts`, a classic script: one sitter at a time, on a lease |
| `piano-bench-client.mjs` | Interface | The bench's client `script`: sits and stands the avatar, asks the piano to open and close the desk display |
| `piano-display.html` | the desk display | The page on the music desk: a wireframe of the future controls, placeholder data |

The modules need the fork's ES module support for entity scripts (`.mjs` `serverScripts` and
`script` with `export default class`). Imports are relative, so the `.mjs` files must be served
from the same directory. The bench's server script is a classic `.js` entity script on purpose,
so the bench works on a domain whose entity script server is upstream Overte, which loads no
`.mjs`; the bench's client script still needs a fork-built Interface.

## Placing a piano

One entity (a Box is enough) with both scripts and a `piano` object in its `userData`. From the
Interface console (a Box has no desk joint: for the bench and the desk display, see below):

```js
Entities.addEntity({
    type: "Box",
    name: "Piano",
    position: Vec3.sum(MyAvatar.position, Vec3.multiply(2, Quat.getFront(MyAvatar.orientation))),
    dimensions: { x: 1.5, y: 1.0, z: 0.6 },
    color: { red: 20, green: 20, blue: 24 },
    serverScripts: "https://<asset-host>/piano/piano-entity.mjs",
    script: "https://<asset-host>/piano/piano-client.mjs",
    userData: JSON.stringify({ piano: { orpheusBase: "http://<host>:<port>", volume: 0.8 } })
});
```

- `orpheusBase` is the Orpheus base URL, `http://` or `https://`, with no query. It has no
  default: without a usable one the server script logs once and does nothing.
- `volume` is 0 to 1, default 0.8.
- The configuration is read when the server script loads. Reload the script after changing it.
- Do not lock the piano. A locked entity accepts no edit but to `locked`, so the status could not
  be written. A locked piano still plays, and the client asks `status` once for its display.
- The entity script server must reach `orpheusBase`. Interface never connects to Orpheus.

## What the server script does

- **Sound.** On `preload` it calls `Audio.playStream(orpheusBase + "/api/synth/stream.pcm",
  { position, volume })` at the entity's position. Everyone near the piano hears it through the
  audio mixer. Once a second it reads the entity's position and moves the injector with
  `setOptions` when it changed. When the stream ends or fails (`finished`), it reconnects after
  1 s, doubling to a 30 s cap. A connection that lasted more than a minute resets the delay to 1 s.
  On `unload` it stops the injector and clears every timer.
- **Status.** It polls `GET /api/playback/status` and `GET /api/synth/status` with `fetch`, every
  second while something is playing, starting or paused, else every 5 seconds. Each request times
  out after 4 s. It writes a compact status into `userData.piano.status`, keeping every other
  key, and only when the status changed. While a piece plays, the progress and elapsed time
  change, so that is about one edit a second.

```json
{
    "piano": {
        "orpheusBase": "http://<host>:<port>",
        "volume": 0.8,
        "status": {
            "state": "playing",
            "title": "Gymnopedie No. 1",
            "output": "synth",
            "progress": 46,
            "elapsed": null,
            "synth": true,
            "error": null
        }
    }
}
```

| Field | Meaning |
| --- | --- |
| `state` | `idle`, `starting`, `connecting`, `playing`, `paused` (from Orpheus), or `offline` (Orpheus did not answer) |
| `title` | The piece, while one is active |
| `output` | `synth` (what this entity plays) or `piano` (the real piano; the stream is then silent) |
| `progress` | Whole percent through the piece's events. Orpheus counts events, not time |
| `elapsed` | Whole seconds, only when the service sends `elapsed_seconds`. Orpheus does not; the mock does |
| `synth` | Whether Orpheus's synth output is enabled |
| `error` | Orpheus's `last_error.message`, or why Orpheus could not be reached |

The status is output only: the server script never reads it back.

### Server API

Call the methods with `Entities.callEntityServerMethod(pianoID, method, [])`. Both only read.
`callEntityServerMethod` returns nothing, so the answer goes to the caller's client script on the
piano with `Entities.callEntityClientMethod`, addressed to `Script.remoteCallerID`. A call with no
caller ID (the null UUID) gets no answer.

| Method | Answer |
| --- | --- |
| `status` | `showStatus([json])`: the last status, as above, or `null` before the first poll |
| `list` | `showSongs([text])`: Orpheus's songs (`GET /api/songs`, cached for 60 s), one line each: title, composer, duration. At most 12 lines, then "+N more" |

## The client script

Each visitor's Interface floats a local Text entity, `Piano.Status`, above the piano: the title
and state, the elapsed time if known, the progress, or "idle" or "offline". Only that visitor
sees it, and it needs no edit rights. It rereads `userData` every second and edits the text when
it changed. A primary click on the piano calls `list` and shows the songs for 8 seconds. There
are no buttons: playing is done through Orpheus.

## The bench and the desk display

The bench is a Model with two joints and is a child of the piano; its contract is section 8 of
the salon grand spec. The same axes as the piano (+Y up, the sitter faces -Z, towards the
keyboard); `seat` at (0, 0.490, 0), the top centre of the cushion, identity rotation, the point
the hips sit over; `stand_up` at (0, 0, 0.640) on the floor, 0.45 m behind the bench's back
edge, where the avatar stands on getting up. The piano must be the salon grand Model, whose
`anchor_desk_screen` joint carries the display. The piano's `script` (piano-client.mjs) is all
the display needs; without `serverScripts` its label just waits for Orpheus.

```js
// The piano: the salon grand at its natural size, with displayURL added to its userData
var pianoID = Entities.addEntity({
    type: "Model",
    name: "Piano",
    modelURL: "https://<asset-host>/props/salon_grand.glb",
    registrationPoint: { x: 0.5, y: 0, z: 0.5 },
    dimensions: { x: 1.480, y: 1.304, z: 2.110 },
    // 3 m ahead on the floor; with the avatar's rotation the keyboard (+Z) faces the avatar
    position: Vec3.sum(MyAvatar.feetPosition, Vec3.multiply(3, Quat.getFront(MyAvatar.orientation))),
    rotation: MyAvatar.orientation,
    serverScripts: "https://<asset-host>/piano/piano-entity.mjs",
    script: "https://<asset-host>/piano/piano-client.mjs",
    userData: JSON.stringify({ piano: {
        orpheusBase: "http://<host>:<port>",
        volume: 0.8,
        displayURL: "https://<asset-host>/piano/piano-display.html"
    } })
});

// The bench: a child of the piano, in front of the keys
Entities.addEntity({
    type: "Model",
    name: "Piano.Bench",
    modelURL: "https://<asset-host>/props/salon-grand-bench.glb",
    parentID: pianoID,
    localPosition: { x: 0, y: 0, z: 1.545 },    // spec 8.4: front edge 0.30 m from the key fronts
    localRotation: { x: 0, y: 0, z: 0, w: 1 },  // same axes as the piano
    registrationPoint: { x: 0.5, y: 0, z: 0.5 },
    dimensions: { x: 0.918, y: 0.490, z: 0.380 },
    shapeType: "box",
    script: "https://<asset-host>/piano/piano-bench-client.mjs",
    serverScripts: "https://<asset-host>/piano/piano-bench-entity.js",
    userData: JSON.stringify({ pianoBench: {} })  // every key is optional
});
```

On an existing pair, set the same properties in Create: on the piano, add `displayURL` to the
`piano` object in `userData`; on the bench, `parentID` the piano, `script`
`.../piano-bench-client.mjs` and `serverScripts` `.../piano-bench-entity.js`.

The display page must be served as `text/html`. raw.githubusercontent.com serves every file as
`text/plain` with `nosniff`, so a Web entity would show the page's source: the `.mjs` scripts
can come from there, `displayURL` cannot.

| Key | Where | Meaning |
| --- | --- | --- |
| `piano.displayURL` | piano `userData` | The desk display page, `http://` or `https://`. No default: without it the sitter sits but gets no display, the label stays, and the log says why. Read when the piano's client script loads |
| `pianoBench.hipsAboveSeat` | bench `userData` | How far the hips sit above `seat`, as a fraction of the avatar's height. Default 0.06 (0.105 m for a 1.75 m avatar), 0 to 0.3 |
| `pianoBench.standHoldMs` | bench `userData` | How long a movement key must be held to stand up. Default 500, 0 to 5000 |
| `pianoBench.leaseMs` | bench `userData` | How long the seat stays taken without a renewal. Default 15000, 3000 to 120000. Read by the server script, which sends it with each grant |

A value out of range is logged and replaced by its default. Both scripts read `userData` once,
when they load.

**Sitting.** A primary click on the bench (the mouse, or a hand controller's trigger with the
laser on it: the controller pointers report the trigger as the primary button,
`scripts/system/controllers/controllerDispatcher.js` 583-634) asks the bench's server script
for the seat. Nothing happens in Create's edit mode, while already seated elsewhere, or from
more than 5 m away. When the seat is granted, the client script calls the engine's own sit,
`MyAvatar.beginSit(position, rotation)`: it pins the **Hips** joint at `position` with
`rotation`, turns the avatar's collisions off (so the bench's and the piano's collision boxes
do not push a seated avatar; `endSit` turns them back on, and `stand_up` is clear of both),
disables the walking keys, and the seated state
drives the seated idle animations (`interface/src/avatar/MyAvatar.cpp` 6309-6326,
`libraries/animation/src/Rig.cpp` 1121-1125). So `position` is where the hips go: the `seat`
joint plus `hipsAboveSeat` times the avatar's height, straight up; `rotation` is the seat's
heading alone, so the avatar stays upright and faces -Z of the bench, the keyboard. The joint's
world pose comes from `Entities.localToWorldPosition` / `localToWorldRotation` with the joint
index, the transform the engine uses for an entity parented to that joint
(`libraries/shared/src/SpatiallyNestable.cpp` 339-377). The live Daybreak chairs
(`sitClient.js` on content.overte.org) use the same `beginSit`.

**Standing up.** Click the bench again, or hold forward/back (W/S), up (space) or a step key for
`standHoldMs`: `beginSit` disables those keys, but their raw values still read
(`MyAvatar.cpp` 4402-4411). Sideways is left alone, since it turns the seated avatar. A key
still held from walking up does not count until it has been let go once. `MyAvatar.endSit`
puts the avatar on `stand_up`, raised by its own feet-to-position height, facing the piano. The
avatar also stands if it is moved more than 0.5 m off the seat (a teleport: it then stays where
it went), if another script ends the sit, on a new avatar model or scale, and when the bench's
script unloads.

**One sitter.** The bench's server script holds the seat on a lease: `claim` is granted when
the seat is free, its lease ran out, or the caller holds it, and the sitter's client renews it
every third of the lease the grant carries. The answer goes back with `Entities.callEntityClientMethod` as
`seatGranted` or `seatRefused`; a refused renewal stands the sitter up. A crashed or departed
sitter frees the seat after `leaseMs`. The caller is `Script.remoteCallerID`, which the engine
stamps from the sending session, so nobody can sit someone else down or release their seat.
A lease in the bench's `userData` would need every visitor to have edit rights on the bench,
and a Messages channel has self-asserted senders and nothing to settle two claims at once; the
server script is the one place that sees every claim in order. Its state is in memory: when it
reloads, the sitter's next renewal claims the seat again.

**The desk display.** Once seated, the bench calls the piano's client script
(`Entities.callEntityMethod(piano, "openDisplay")`, a local call; the method is not in
`remotelyCallable`, so the server cannot call it). It adds a local Web entity, `Piano.Display`,
seen by the sitter only, parented to the piano at `anchor_desk_screen` with zero offset and
rotation: the anchor's +Z points out of the desk at the player, and a Web entity's face is its
+Z (the yaw billboard turns +Z to the camera, `interface/src/Application_Setup.cpp` 1763-1768).
It is the 0.30 x 0.17 m glass panel, scaled with the piano, at the dpi that makes the page
720 x 408 pixels (a Web entity is its dimensions in inches times `dpi` pixels,
`RenderableWebEntityItem.cpp` 453-457). `wantsKeyboardFocus` is off, so the movement keys still
reach the avatar. The sitter's floating status label hides while the display is up; everyone
else still sees theirs. Standing up deletes the display and shows the label again.

`piano-display.html` is self-contained (no external fonts, scripts or images) and is a
**wireframe**: placeholder now-playing, progress, a transport row (previous, play/pause, stop,
next, replay, after Orpheus's `/api/playback/*` routes), a song list (`play-song`), and status
indicators. A button sends `{"type": "piano-display", "action": "next"}` (and `songId` for
`play-song`) with `EventBridge.emitWebEvent`; the piano's client script prints it and answers
`{"type": "piano-display-ack", ...}` with `Entities.emitScriptEvent`, which the page shows in
its footer. Nothing reaches Orpheus yet. Opened in a desktop browser, the page says it has no
EventBridge and logs the events to the console.

## Trying it with a mock Orpheus

The mock is not part of this directory. It is a Python 3 script with no dependencies that serves
the three status routes with Orpheus's JSON shapes and `/api/synth/stream.pcm`: endless chunked
`audio/L16; rate=24000; channels=1`, 16-bit little-endian mono, paced to the wall clock, playing
a looping melody. With `--scripts DIR` it also serves the piano's scripts at `/scripts/`.

```sh
python3 mock_orpheus.py --bind <addr> --port <port> --scripts <this directory>
curl http://<addr>:<port>/api/playback/status
timeout 2 curl -s http://<addr>:<port>/api/synth/stream.pcm | wc -c    # about 96000
```

Then place a piano with `serverScripts` and `script` at `http://<addr>:<port>/scripts/...` and
`orpheusBase` at `http://<addr>:<port>`, an address the entity script server and Interface can
both reach.

## Development

Node (24 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: both cores, and every script with fake globals
npm run typecheck   # tsc twice: the server scripts and the client scripts against their entity declarations
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.

## Not verified yet

- The bench and the display have not run in Interface. Not seen: `beginSit` with the hips over
  `seat` (whether 0.06 of the height puts the thighs on the cushion, and the hands at the keys),
  `endSit` on `stand_up`, the drive-key stand-up,
  the lease round trip (the classic server script has run only in a Node `vm`, not on an
  upstream entity script server), `callEntityMethod` between the two `.mjs` client scripts, the Web
  entity's face and fit on the desk glass, EventBridge in a Web entity page, and the hand
  controller trigger on the bench. The bench's joints are taken from the spec, not read from a
  loaded model. The page was rendered and its buttons clicked in headless Chrome with a stub
  EventBridge.

- Nothing here has run in an Overte domain: not the stream through `Audio.playStream`, the
  position following, the reconnects, `fetch` in the entity script server, the client label or
  the click. The tests run the scripts under Node with fake engine globals, and the server script
  has been run under Node against the mock with real `fetch`.
- Orpheus has no `/api/synth/stream.pcm` yet. Its synth stream is Ogg Opus at
  `/api/synth/stream`; the PCM route at 24 kHz mono is what `Audio.playStream` needs.
- Orpheus answers 401 to every unauthenticated request, and the piano sends no credential. A
  credential must not go into `userData`, which every visitor can read.
