# Flock

N Model entities that fly as one flock: gulls wheeling over a building by day, fish in a bay
later. Every member carries the same `serverScripts`, reads its own `userData.flock`, and hz times
a second steers itself by the classic boid rules over its flockmates (separation, alignment,
cohesion), plus a pull back into a home cylinder and an altitude band. It then edits its own
`position`, `rotation` (nose along its velocity, banked into the turn), `velocity` and
`angularVelocity`, and the entity server and every client carry it forward kinematically until
the next edit. A member scatters from an avatar that comes close and, if it has a roost, spends the
night there, hidden, with no edits.

| File | Runs in | What it is |
| --- | --- | --- |
| `flock-core.mjs` | anywhere | Pure logic: the `userData` configuration, the steering, the orientation and bank, the state machine, the roost leg, the sun's elevation and the phase of the day |
| `flock-server.mjs` | entity script server | A member's `serverScripts`: one member, one edit per tick |

The server script needs the fork's ES module support for entity scripts (`.mjs` `serverScripts`
with `export default class`); it imports `flock-core.mjs` by relative URL, so both are served from
the same directory. There is no manager entity: the entity script server receives only entities
whose `serverScripts` is set (below), so a script on one entity could not see the others.

## userData

```json
{
  "flock": {
    "name": "delos-gulls",
    "home": {"x": 0, "y": 24.6, "z": -50},
    "radius": 40,
    "band": 7.5,
    "speed": {"min": 7, "max": 12},
    "hz": 5,
    "scatter": {"within": 10, "forMs": 8000},
    "roost": {"x": -260, "y": 0.5, "z": -230}
  },
  "sunSky": {"noonUtc": 16.0}
}
```

| Key | Meaning |
| --- | --- |
| `flock.name` | Required. The flock: members with the same name flock together, and only with each other |
| `flock.home` | Required. The axis of the home cylinder (x, z) and the middle of the altitude band (y), in world coordinates |
| `flock.radius` | The home cylinder's radius, m. Default 40, 1 to 1000. Inside it nothing pulls a member home; past it the pull grows with the distance |
| `flock.band` | How far above or below `home.y` a member flies freely, m. Default 7.5, 0 to 500 |
| `flock.speed.min`, `.max` | m/s. Default 7 and 12, each 0.5 to 50; a `max` below `min` is raised to it. A member never flies slower than `min` (a gull cannot hover), cruises at the middle and scatters at `max` |
| `flock.hz` | Edits a second. Default 5, clamped to 1 to 10 |
| `flock.scatter.within`, `.forMs` | An avatar this close, m, scatters the member for this long, ms. Default 10 and 8000; 0 to 100 and 0 to 120000. `within` 0 never scatters |
| `flock.roost` | Optional. Where the member spends the night. Without it (fish) the member ignores the night |
| `sunSky.noonUtc` | The hour of solar noon in UTC, 0 to 24, the same key the sunSky scripts read. Required only with a `roost` |

Without `name` or `home` (or with a `roost` and no `noonUtc`), the member logs
`Flock <entityID>: userData.flock.<key> ... The member does not fly.` and does nothing. A number
out of range is logged (`Flock <entityID>: flock.hz must be from 1 to 10, got 20; using 10`) and
clamped into it; a value that is not a number is logged and replaced by its default; a `roost`
that is not a position is logged and ignored. The script reads `userData` once, when it loads.

## Steering

Each tick a member sums these pushes, in m/s² (the gains are `WEIGHTS` in `flock-core.mjs`):

- **Separation** from each mate closer than 6 m, harder the closer.
- **Alignment** with the mean velocity of the mates it sees (within 20 m, the nearest 24 at
  most).
- **Cohesion** toward their centroid.
- **Home**: past `radius` (measured horizontally), back toward the axis, growing with the
  distance past it; and a slight circulation about the axis everywhere, so the flock wheels
  rather than bouncing off the rim.
- **Band**: past `home.y ± band`, back toward it (at most 3 m/s²); and everywhere a damping of
  the climb or sink, since gulls soar mostly level.
- **Wander**: a small push that turns once every 30 s, each member at its own phase (from its
  entity id), so the flock drifts and no two members move alike.
- **Flee**, while scattering: away from the avatar, a little upward.

Then it flies as a gull can under that push: the speed changes by at most 4 m/s per second
within `speed.min..max`, settling at cruise (`max` while scattering); the heading turns in the
horizontal plane at the push across it divided by the speed, at most 60 deg/s, and a demand to
slow that the speed cannot meet turns the member instead; the climb or dive follows the vertical
push, at most 30 deg from level. The nose points along the velocity (an entity's front is its -z,
`libraries/shared/src/GLMHelpers.cpp:38`), wings level to the horizon, rolled into the turn by
the coordinated-turn bank atan(lateral acceleration / g), at most 35 deg.

In a simulated flock of 12 at the defaults over five minutes, members stay within about 11 m
past the rim and within 2 m of the band, never closer than about 2.5 m to each other, bank 10 deg
in a typical tick and 18 deg one tick in ten, and spend most of their time near the rim: inside
it nothing pulls them home, so they cross it and turn back past it, by about the turn radius (7
to 11 m at 60 deg/s).

## States

| State | Edits | Leaves when |
| --- | --- | --- |
| `wheel` | hz a second, steering | an avatar is within `scatter.within`: `scatter`. Night, with a roost: `roost` |
| `scatter` | hz a second, steering with the flee, at `speed.max` | `scatter.forMs` has passed: `wheel` (and `scatter` again at once if an avatar is still close). Night, with a roost: `roost` |
| `roost` | hz a second, straight for the roost at cruise, slowing as it nears so its turn limit cannot hold it circling | within 2 m of the roost: `roosted`. Twilight or day: `wheel` |
| `roosted` | one edit on arrival (`velocity` and `angularVelocity` 0, `visible` false), then none; the timer drops to 1 Hz to watch the phase | twilight or day: `wheel`, taking off level toward home at `speed.min` with `visible` true |

The phase is worked out at load from the clock and `sunSky.noonUtc`, with the formulas of
`sunSky/sun-sky.mjs` (`sunPosition`'s elevation, 90 - |hour angle| at 15 deg an hour from noon,
and `phase`: `day` above -0.833 deg, `twilight` to -6 deg, `night` below), copied into
`flock-core.mjs` because each bundle is served from its own folder. After load it follows the
`oceanus.daynight` messages the sky zone's script sends at each change of phase
(`sunSky/sun-sky-server.mjs`, `{type: "phase", phase, azimuth, elevation}`). So dusk is
`twilight` then `night` (the member flies to the roost at dark), and dawn is `twilight` (it takes
off at first light) then `day`. At `noonUtc` 16 the roost holds from 22:24 to 09:36 UTC.

Only members with a roost subscribe to the channel. On unload a member disconnects its handler but
does not unsubscribe: a subscription belongs to the whole entity script server process
(`MessagesClient::unsubscribe`, `libraries/networking/src/MessagesClient.cpp:167`, keeps no count),
so one member's unload would deafen every other.

## Budget

12 members x 5 Hz = 60 entity edits a second by day, each of four properties; 0 at night once
roosted. It is the first measurement of the octree edit budget. `hz` 1 to 10 trades smoothness
for traffic: between edits every client extrapolates, so a lower rate shows as wider corrections
in a turn, not as stutter.

## The module scope

Unlike sunSky (module scope holds constants only), `flock-server.mjs` keeps two things at module
scope on purpose. The entity script server shares one module instance per URL between every entity
that uses it, and every member of a domain runs in that one process, so:

- **`flocks`**, a registry of the members of each flock, by `name`, then entity id:
  `{position, velocity, state, seen}`. Each member writes its own entry every tick and reads its
  mates from the registry, carried forward along their velocities to now, instead of calling
  `Entities.getEntityProperties` for N - 1 entities every tick (N² reads a tick). An entry not
  refreshed within 3 of its member's ticks counts as gone; a member removes its own on unload and
  on roosting, and members flying to or sitting on a roost are not steered by.
- **The avatar objects**, one `AvatarList.getAvatar` per avatar session for the life of the
  session, shared by all members, never one per tick. `getAvatar` allocates an object the script
  never frees (fork issue #83, `libraries/avatars/src/AvatarHashMap.h:120`), about 16 KB a call,
  which at 60 calls a second would be a megabyte a second. `AvatarList.getAvatarsInRange` (ids
  only, `AvatarHashMap.h:111`) finds who is close; the cached object gives the position to flee
  from; sessions no longer in `AvatarList.getAvatarIdentifiers` are dropped.

A member served from another URL (a cache-busting query, another copy) gets another module and
so another registry: it would not see its flock. Give every member the same `serverScripts`.

## Engine facts

- **Every member carries the script.** The entity script server's octree query asks only for
  entities with a non-default `serverScripts` (with their parents and children):
  `assignment-client/src/scripts/EntityScriptServer.cpp:322-324`.
- **Kinematic extrapolation.** A non-dynamic entity with a `velocity` or `angularVelocity` is
  stepped by `EntityItem::stepKinematicMotion` (`libraries/entities/src/EntityItem.cpp.in:948`):
  position by velocity (`:1032`, `:1042`), rotation as dQ x rotation, so `angularVelocity` is in
  the world frame (`:992`). It runs on the entity server and on the entity script server's own
  tree, each with a `SimpleEntitySimulation` (`assignment-client/src/entities/EntityServer.cpp:102`,
  `EntityScriptServer.cpp:343`, stepping moving entities in
  `libraries/entities/src/EntitySimulation.cpp:242`), and on every client through physics
  (`libraries/physics/src/EntityMotionState.cpp:247`). So one edit every 1/hz s holds. Each edit
  sets the position to where the last velocity carried the member, so the edits do not jump.
- **Damping.** The defaults `ENTITY_ITEM_DEFAULT_DAMPING` and
  `ENTITY_ITEM_DEFAULT_ANGULAR_DAMPING` are 0.39347
  (`libraries/entities/src/EntityItemPropertiesDefaults.h:73-74`), a 2 s decay applied in the
  kinematic step (`EntityItem.cpp.in:1008`) that would slow a member between edits. Members carry
  `damping` 0 and `angularDamping` 0.
- **Avatars in the entity script server.** `AvatarList` is registered on every entity script
  engine (`EntityScriptServer.cpp:486`). The avatar mixer sends avatar data to
  `EntityScriptServer` nodes it holds data for
  (`assignment-client/src/avatars/AvatarMixerWorker.cpp:268`); it creates that data on the first
  packet from the node (`AvatarMixer.cpp:747`), which is the ignore-radius state every node list
  sends a new avatar mixer (`libraries/networking/src/NodeList.cpp:1203`). The script server has
  no camera, so positions of far avatars may lag; the flock only asks about near ones.
- **The script never edits `locked` or `userData`.** A locked member logs
  `Flock <entityID>: the member is locked and takes no edit; unlock it and reload its script.` at
  load and does not fly, since it would only hold its mates back.

## Placing members

Twelve gulls, from a script in Interface (or the same properties from a content tool):

```js
var HOME = { x: 0, y: 24.6, z: -50 };
for (var i = 0; i < 12; i++) {
    var angle = 2 * Math.PI * i / 12;
    Entities.addEntity({
        type: "Model",
        name: "Delos.Gull " + (i + 1),
        modelURL: "https://<asset-host>/props/gull.glb",
        position: { x: HOME.x + 20 * Math.cos(angle), y: HOME.y + (i % 3) - 1, z: HOME.z + 20 * Math.sin(angle) },
        // Facing round the circle; the script starts each member level along its -z at cruise.
        rotation: Quat.fromPitchYawRollDegrees(0, 180 - angle * 180 / Math.PI, 0),
        serverScripts: "https://<asset-host>/flock/flock-server.mjs",
        userData: JSON.stringify({
            flock: { name: "delos-gulls", home: HOME, radius: 40, band: 7.5, speed: { min: 7, max: 12 },
                hz: 5, scatter: { within: 10, forMs: 8000 }, roost: { x: -260, y: 0.5, z: -230 } },
            sunSky: { noonUtc: 16.0 }
        }),
        dynamic: false,
        collisionless: true,
        gravity: { x: 0, y: 0, z: 0 },
        damping: 0,
        angularDamping: 0,
        locked: false
    });
}
```

`collisionless` keeps visitors and the members from pushing each other, and `dynamic` false
keeps the physics engine from owning the motion. A member that is not moving at load starts off
along its model's -z, level, at cruise speed; one already moving keeps its velocity. A member
loaded at night flies to its roost from where it is; one loaded hidden by day is shown with its
first edit.

## What is not here

- **No animation.** The fork's GLTF serializer reads no animations, so a gull model has no wing
  beat to play; it soars, which is also most of what a gull does. The bank is the only pose.
- **No mind.** The state machine is the whole of the behaviour. An LLM intent-setter (a flock
  that decides to follow a visitor, or to settle on a roof) is a later option, over a message
  channel the members would listen to.
- **No collision avoidance** with buildings or terrain: the content sets `home`, `radius` and
  `band` clear of them, and the roost leg flies straight.

## Development

Node (22 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: the core, and the server script with fake globals
npm run typecheck   # tsc: the server script and the core against the server entity declarations
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.

## Verified, and not yet

**Verified (2026-10-31):** the tests, including a five-minute simulated flock of 12 at the
defaults and the roost leg from three starts; the typecheck.

**Not yet seen in a domain:** the members on an entity script server, the edits reaching
visitors and the motion between them, the scatter from a real avatar's position as the script
server receives it, the roost at dark and the take-off at first light, and the edit rate
against the entity server's budget.
