# Piano

A proof of concept: an entity that is a speaker for a remote synthesizer, Orpheus, and shows
what it is playing. Nobody plays it from inside the world. LLM agents play Orpheus through
Orpheus's own MCP server, and people play the real piano it is connected to. The in-world side only listens and reads.

| File | Runs in | What it is |
| --- | --- | --- |
| `piano-core.mjs` | anywhere | Pure logic: the userData configuration, the compact status, the reconnect backoff, and the text shown |
| `piano-entity.mjs` | entity script server | The piano's `serverScripts`: plays the stream at the entity, mirrors the status into `userData`, answers `status` and `list` |
| `piano-client.mjs` | Interface | The piano's client `script`: a floating local Text above the piano with the status, and the song list on a click |

The modules need the fork's ES module support for entity scripts (`.mjs` `serverScripts` and
`script` with `export default class`). Imports are relative, so the three files must be served
from the same directory.

## Placing a piano

One entity (a Box is enough) with both scripts and a `piano` object in its `userData`. From the
Interface console:

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
npm test            # node:test over test/*.test.mjs: the core, and both scripts with fake globals
npm run typecheck   # tsc twice: the server script and the client against their entity declarations
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.

## Not verified yet

- Nothing here has run in an Overte domain: not the stream through `Audio.playStream`, the
  position following, the reconnects, `fetch` in the entity script server, the client label or
  the click. The tests run the scripts under Node with fake engine globals, and the server script
  has been run under Node against the mock with real `fetch`.
- Orpheus has no `/api/synth/stream.pcm` yet. Its synth stream is Ogg Opus at
  `/api/synth/stream`; the PCM route at 24 kHz mono is what `Audio.playStream` needs.
- Orpheus answers 401 to every unauthenticated request, and the piano sends no credential. A
  credential must not go into `userData`, which every visitor can read.
