# Piano

An entity that is a speaker for a remote synthesizer, Orpheus, and shows what it is playing.
Visitors ask it for songs and run its transport from inside the world, each visitor session held
to limits (Morpheus ADRs 0020 and 0022), and the salon grand's lid, stick and fallboard follow
what Orpheus is doing (ADR 0021). Nobody plays its keys in-world: LLM agents compose through
Orpheus's own MCP server, and the domain's Orpheus is a headless, synth-only instance, never the
appliance at a real piano. A bench lets one visitor sit at the keyboard; the desk display that
opens for them is a wireframe whose buttons reach the client script and stop there (the song
picker and the transport buttons on it are heluca/overte#74).

| File | Runs in | What it is |
| --- | --- | --- |
| `piano-core.mjs` | anywhere | Pure logic: the userData configuration, the compact status, the reconnect backoff, and the text shown |
| `piano-bridge-core.mjs` | anywhere | Pure logic for requests and transport: the per-session limits, the bridge routes, refusal reasons, the song list for a picker |
| `piano-pose-core.mjs` | anywhere | Pure logic for the lid, stick and fallboard: joints from the prop's manifest, the pose per status, the ordered tween, the joint arrays |
| `piano-entity.mjs` | entity script server | The piano's `serverScripts`: plays the stream at the entity, mirrors the status into `userData`, moves the lid, answers `status` and `list`, takes requests and runs the transport |
| `piano-client.mjs` | Interface | The piano's client `script`: a floating local Text above the piano with the status, the song list on a click, the answers to requests and transport calls, and the desk display for the sitter |
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
  default: without a usable one the server script logs once and does nothing, and refuses every
  call with "This piano is not connected to Orpheus".
- `volume` is 0 to 1, default 0.8.
- `manifestURL` is the salon grand's published `manifest.json` (the same Ergane bundle as its
  model), where the server script reads the lid, stick and fallboard joints. No default: without
  it, or on a Box, the piano plays but does not move.
- The limits, every one optional, an integer, and logged and replaced by its default when out of
  range:

| Key | Default | Range | Meaning |
| --- | --- | --- | --- |
| `requestCooldownMs` | 10000 | 0 to 600000 | One song request per visitor session per this many milliseconds |
| `requestQueueCap` | 5 | 0 to 50 | Requests from this piano waiting in Orpheus's queue at once; 0 turns requests off |
| `transportCalls` | 5 | 1 to 100 | Transport calls per visitor session per window |
| `transportWindowMs` | 30000 | 1000 to 600000 | The transport window |

  A session may also have only one request waiting: it must start (or leave the queue) before
  that session requests again. That one is not configurable (ADR 0020).
- The configuration is read when the server script loads. Reload the script after changing it.
- Do not lock the piano. A locked entity accepts no edit but to `locked`, so neither the status
  nor the lid could be written. A locked piano still plays and takes requests, and the client
  asks `status` once for its display.
- The entity script server must reach `orpheusBase`. Interface never connects to Orpheus.
- **No credential in `userData`**, which every visitor can read. The bearer comes from the
  domain's script credential table (`script_credentials`, heluca/overte#63): a row whose
  `url_prefix` covers `<orpheus-base>/api/` and whose `script_prefix` covers the folder these
  scripts are served from. The engine adds `Authorization` to this script's `fetch` and
  `Audio.playStream` requests under that prefix, and only when every script on the stack and
  every module imported is under `script_prefix`, so the modules must all come from one folder.
  The domain's Orpheus takes it as a bridge token, scoped to the bridge routes.

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
  change, so that is about one edit a second. After a request or a transport call that went
  through, it polls at once rather than wait for the next poll.
- **Lid, stick and fallboard.** With `manifestURL` set, it reads the joints `lid`, `lid_prop` and
  `fallboard` from the manifest (`joints.<name>.index`, `axis`, `travel`, and
  `overte.joint_count`), retrying every 60 s while the manifest cannot be fetched, and then
  follows the status as the prop's spec says (phantasos `props/salon-grand/spec.md` section 4):
  open while `playing`, `starting` or `connecting` (the lid to its travel, 40 deg, then the stick
  up, 103.4 deg), closed while `idle` or `paused` (the stick down, then the lid), and closed with
  the fallboard shut over the keys (90 deg) while `offline`. The entity script server has no
  model loaded, so it writes the joints as properties: `Entities.editEntity(id,
  {jointRotationsSet, jointRotations})`, arrays of `joint_count`, these three joints flagged set
  on every edit (a joint flagged unset keeps whatever it was last given) and every other joint
  left unset. The first pose after loading is written at once; after that each change is a
  tween at 10 edits a second, the lid at 30 deg/s, the stick at 100 deg/s and the fallboard at
  90 deg/s: about 2.5 s and 25 edits to open, the same to close, none while the pose holds. The
  stick only rises once the lid is fully open, and the lid only comes down once the stick is
  folded, also when the status changes part way through. The keys (`keysHz`, `keyDelayMs`) are
  heluca/overte#73 and not read yet.
- **Requests and transport.** See the server API below. Each call POSTs one bridge route with
  `X-Overte-Session: <the caller's session>`, which Orpheus uses for its own per-visitor limits,
  and `X-Orpheus-Request: 1`, which Orpheus's sandbox network grant asks of a POST without a
  credential. Every route that can start a piece names `output=synth`: Orpheus forces it for a
  bridge token anyway, and a wrong credential must still never sound a real piano.

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

Call the methods with `Entities.callEntityServerMethod(pianoID, method, params)`, `params` an
array of strings. `callEntityServerMethod` returns nothing, so the answer goes to the caller's
client script on the piano with `Entities.callEntityClientMethod`, addressed to
`Script.remoteCallerID`, the caller's session, which the engine stamps and a caller cannot
choose. A call with no caller ID (the null UUID) gets no answer and shares one set of limits.

| Method | Params | What it does | Answer |
| --- | --- | --- | --- |
| `status` | none | Reads | `showStatus([json])`: the last status, as above, or `null` before the first poll |
| `list` | none | Reads | `showSongs([text, json])`: Orpheus's songs (`GET /api/songs`, cached for 60 s). `text` one line each, title, composer and duration, at most 12 lines then "+N more"; `json` the songs for a picker, `[{"id", "title", "composer", "duration"}]` (`composer` and `duration`, in whole seconds, may be `null`), or `null` when Orpheus did not answer |
| `request` | `[songId]` | Appends the library song to Orpheus's queue on the synth (`POST /api/playback/request/{id}?output=synth`). Never plays it now, never pre-empts | `showResult` with `position`: 1 plays next, 0 started at once |
| `play` | none | Paused: resumes (`POST /api/playback/pause`, which toggles). Idle: plays the last piece again (`POST /api/playback/replay`), as Orpheus's own Play does. Refused while already playing | `showResult` |
| `pause` | none | Pauses (`POST /api/playback/pause`). Refused while nothing plays or already paused | `showResult` |
| `stop` | none | Stops and clears the queue (`POST /api/playback/stop`) | `showResult` |
| `next` | none | Skips to the next queued piece (`POST /api/playback/next`). Refused when nothing is queued | `showResult` |
| `previous` | none | Restarts the current piece, or when idle replays the last one (`POST /api/playback/previous`) | `showResult` |
| `replay` | none | Plays the most recent piece again (`POST /api/playback/replay`) | `showResult` |
| `select` | `[songId]` | Plays the library song now, replacing what plays and what is queued (`POST /api/playback/play-song/{id}?play_now=true`), the one call that may (ADR 0022) | `showResult` |

`play` and `pause` ask Orpheus what is playing first, so each goes one way although Orpheus's
pause toggles. `songId` is a library id, a positive integer as text; anything else is refused
before it leaves the piano.

`showResult([json])` carries `{"action", "ok", "reason", "songId", "title", "position"}`: `ok`
false with `reason` when the piano or Orpheus refused, `songId` and `title` (when the piano has
the song list cached) for `request` and `select`. The server script also logs one line per call:
the action, the song, the caller's session, done or refused and why.

**Limits.** Checked by the piano before anything is sent, then by Orpheus, which holds its own
per bridge credential (orpheus `docs/orpheus.md`, "Overte bridge") and answers 429 with a reason
that the piano passes on as it is. The piano's, per session (`Script.remoteCallerID`):

- `request`: one request waiting per session, held from the call until the status poll shows it
  has left Orpheus's queue (started, removed or cleared); one request per `requestCooldownMs`,
  counted when admitted, also when Orpheus then refuses it; at most `requestQueueCap` of this
  piano's requests waiting at once.
- The transport calls share one sliding window: at most `transportCalls` per
  `transportWindowMs`. A call refused for its state (nothing playing, nothing queued) still
  counts. Requests do not count against it.

A refusal names the limit and how long to wait, for example "One request every 10 s; try again
in 6 s" or "Too many controls: at most 5 every 30 s; try again in 30 s". The counts are in
memory: a reloaded script, or a visitor who reconnects with a new session, starts afresh, and
the queue cap bounds what that buys (ADR 0020). Orpheus answers 409 while its real piano is
busy, which only an owner can cause and which the bridge never touches.

## The client script

Each visitor's Interface floats a local Text entity, `Piano.Status`, above the piano: the title
and state, the elapsed time if known, the progress, or "idle" or "offline". Only that visitor
sees it, and it needs no edit rights. It rereads `userData` every second and edits the text when
it changed. A primary click on the piano calls `list` and shows the songs for 8 seconds. The
answers to that visitor's requests and transport calls (`showResult`) show the same way for 8
seconds, a refusal with its reason, and are logged. There are no buttons yet: a script calls the
server methods (the desk display's song picker and transport buttons are heluca/overte#74).

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
        displayURL: "https://<asset-host>/piano/piano-display.html",
        manifestURL: "https://<asset-host>/props/manifest.json"
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
| `piano.display` | piano `userData` | Optional size and place of the display, every key optional: `{"width": 0.80, "height": 0.275, "offset": {"x": 0, "y": 0.0275, "z": 0.013}, "pagePixelsWide": 1600}` (the defaults). Metres at the piano's natural size, `offset` from `anchor_desk_screen` in its frame. Ranges: width 0.05 to 3, height 0.05 to 2, offset -1 to 1 each, pagePixelsWide 200 to 4096. Reread every second: an open display follows a change within a second |
| `pianoBench.hipsAboveSeat` | bench `userData` | How far the hips sit above `seat`, as a fraction of the avatar's height. Default 0.06 (0.105 m for a 1.75 m avatar), 0 to 0.3 |
| `pianoBench.standHoldMs` | bench `userData` | How long a movement key must be held to stand up. Default 500, 0 to 5000 |
| `pianoBench.leaseMs` | bench `userData` | How long the seat stays taken without a renewal. Default 15000, 3000 to 120000. Read by the server script, which sends it with each grant |

A value out of range is logged (once per change) and replaced by its default. The bench's
scripts and `piano.displayURL` are read once, when the scripts load; `piano.display` is
reread with the status every second.

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
seen by the sitter only, parented to the piano at `anchor_desk_screen` with identity rotation:
the anchor's +Z points out of the desk at the player, and a Web entity's face is its +Z (the
yaw billboard turns +Z to the camera, `interface/src/Application_Setup.cpp` 1763-1768). By
default it covers the whole face of the music desk, the score included. Measured in
`salon-grand.glb` in the anchor's frame, the desk board spans x -0.41 to 0.41 with its face at
z -0.0045, the ledge stands proud below y -0.12, the board's top is at y 0.175, and the score
reaches z 0.0099. So the display is 0.80 x 0.275 m (10 mm inside the edges, above the ledge),
centred at y 0.0275, and 13 mm in front of the anchor (3 mm in front of the score). All of it
scales with the piano. The dpi makes the page `pagePixelsWide` pixels across (a Web entity is
its dimensions in inches times `dpi` pixels, `RenderableWebEntityItem.cpp` 453-457); that sets
sharpness only, since the page scales itself to the viewport. Changing `piano.display` while
someone sits edits the open display in place. `wantsKeyboardFocus` is off, so the movement keys still
reach the avatar. The sitter's floating status label hides while the display is up; everyone
else still sees theirs. Standing up deletes the display and shows the label again.

`piano-display.html` is self-contained (no external fonts, scripts or images). It is laid out
in rem on a 50 x 17.1875 rem canvas, the 0.80 x 0.275 m desk face, so 1 rem (body text) is
16 mm on the desk and the title 28 mm, legible from the bench at 0.6 to 0.7 m; the root font
size fits that canvas to the viewport, so another size or aspect scales rather than crops.
It is a **wireframe**: placeholder now-playing, progress, a transport row (previous, play/pause, stop,
next, replay, after Orpheus's `/api/playback/*` routes), a song list (`play-song`), and status
indicators. A button sends `{"type": "piano-display", "action": "next"}` (and `songId` for
`play-song`) with `EventBridge.emitWebEvent`; the piano's client script prints it and answers
`{"type": "piano-display-ack", ...}` with `Entities.emitScriptEvent`, which the page shows in
its status bar. Nothing reaches Orpheus yet. Opened in a desktop browser, the page says it has no
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
both reach. The mock has no request or transport routes: those need a real Orpheus, such as the
headless synth container built from the Orpheus repository (its README, "Run as a container").

## Development

Node (24 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: both cores, and every script with fake globals
npm run typecheck   # tsc twice: the server scripts and the client scripts against their entity declarations
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.

## Verified, and not yet

**In a domain (2026-10-22).** v2 at 18e0989, published as the `piano-scripts` 0.2.0 bundle and
attached to the salon grand in the Oceanus sandbox domain (a fork domain server, the bearer in
its script credential table, Orpheus's network grant off), with two fork Interfaces (headless,
on Xvfb) connected:

- The server script loaded from the bundle and its stream connected with the credential
  (Orpheus logged "Overte listener connected").
- A `request(1)` from one client appended the song on the synth (Orpheus: "Bridge request
  queued ... for session `<the caller>`", then "Playing: ... output=synth"), and that client's
  script showed "Requested: Gymnopedie No. 1: plays next". A second `request` 3 s later was
  refused: "One request every 10 s; try again in 7 s".
- A burst of seven `next` calls 0.3 s apart: five reached Orpheus and were refused for their
  state ("Nothing is queued to skip to"), the sixth and seventh were refused by the limit ("Too
  many controls: at most 5 every 30 s; try again in 29 s"). `stop` 32 s later went through.
- The lid opened within about 2.5 s of the piece starting (lid 40 deg, then the stick 103.4
  deg) and closed after the stop, read from the entity's joint arrays and seen in screenshots
  by both clients. The status in `userData` followed.
- Heard, by measurement: the first client's audio output, captured from a null sink, was
  silent before the request, carried the piece while it played (RMS about -55 dBFS at 3 m), and
  went silent at the stop. Nobody has listened to it by ear.

Run against the real Orpheus under Node (the bearer added by the harness): `pause` and `play`
(resume) on a playing piece, `next`, `stop`, and the transport limit.

**Not yet seen in a domain:** `select`, `previous`, `replay`, `play` from idle, the queue cap,
the fallboard closing when Orpheus is offline, the manifest retry, and the desk display with
v2 (its buttons still stop at the client script, #74). From before: the bench seen in
Interface at bcb58d1df0 and a70ff32 (sit and stand by click); not yet the drive-key stand-up,
the lease against a second visitor, the display's buttons over EventBridge, or the hand
controller trigger.
