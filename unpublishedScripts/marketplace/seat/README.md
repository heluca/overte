# Seat

A seat for any Model whose joints follow the contract below: a bench, a banquette, a row of bar
stools, an armchair. One visitor sits on each seat at a time; who holds which seat is a lease in
the server script's memory, and the sit itself is the engine's own (`MyAvatar.beginSit`). It
grew out of the piano bench's sit, generalised to N seats; the bench's own scripts stay with the
piano (`../piano/`).

| File | Runs in | What it is |
| --- | --- | --- |
| `seat-core.mjs` | anywhere | Pure logic: the `userData` configuration, the joint names, which seat to ask for first, the sit and stand poses, the stand-up gesture |
| `seat-server.js` | entity script server | The seat's `serverScripts`, a classic script: one sitter per seat, on a lease |
| `seat-client.mjs` | Interface | The seat's client `script`: asks for a seat, sits and stands the avatar |

The client script is an ES module (`export default class`, a relative import of
`seat-core.mjs`, so both are served from the same directory) and needs the fork's Interface.
The server script is a classic `.js` entity script on purpose, so the seat works on a domain
whose entity script server is upstream Overte, which loads no `.mjs`.

## The joints

A seat model carries joints `seat_1` to `seat_N` and `stand_1` to `stand_N`, N from 1 and
contiguous: the client counts up from `seat_1` and stops at the first one missing (at 64 at
most). `seat_i` is on the seat's top surface at its centre, the point the hips go over;
`stand_i` is on the floor where the avatar stands on getting up. A `seat_i` without its
`stand_i` is left out, with a note in the log.

Each joint's rotation is the way the sitter looks. An avatar faces its own -Z, so the sitter is
given the `seat_i` joint's world heading alone (its yaw; pitch and roll dropped, so the avatar
stays upright): an identity joint seats the avatar facing the model's -Z, and a joint carrying
the half turn (0, 1, 0, 0) about +Y seats it facing the model's +Z. Standing up, the avatar
keeps the seat joint's heading at the `stand_i` position.

The phantasos Delos bench, in the model's frame, its joints carrying (0, 1, 0, 0) so its sitters
face +Z:

| Seat | `seat_i` | `stand_i` |
| --- | --- | --- |
| 1 | (-0.52, 0.4385, 0.0503) | (-0.52, 0, 0.72) |
| 2 | (0, 0.4385, 0.0503) | (0, 0, 0.72) |
| 3 | (0.52, 0.4385, 0.0503) | (0.52, 0, 0.72) |

The Delos banquette (5 seats), bar stools (6), cafe table (2) and armchair (1) follow the same
contract.

## Placing a seat

```js
Entities.addEntity({
    type: "Model",
    name: "Delos.Bench",
    modelURL: "https://<asset-host>/props/delos-bench.glb",
    position: Vec3.sum(MyAvatar.feetPosition, Vec3.multiply(2, Quat.getFront(MyAvatar.orientation))),
    rotation: MyAvatar.orientation,
    shapeType: "box",
    script: "https://<asset-host>/seat/seat-client.mjs",
    serverScripts: "https://<asset-host>/seat/seat-server.js",
    userData: JSON.stringify({ seat: {} }),  // every key is optional
    locked: true
});
```

On an existing Model, set `script`, `serverScripts` and `userData` the same way in Create.
Neither script writes the entity, so it can stay locked.

| Key | Read by | Meaning |
| --- | --- | --- |
| `seat.hipsAboveSeat` | client | How far the hips sit above `seat_i`, as a fraction of the avatar's height. Default 0.06 (0.105 m for a 1.75 m avatar), 0 to 0.3 |
| `seat.standHoldMs` | client | How long a movement key must be held to stand up. Default 500, 0 to 5000 |
| `seat.leaseMs` | server | How long a seat stays taken without a renewal. Default 15000, 3000 to 120000. Sent with each grant |

A value out of range is logged (`Seat <entityID>: seat.<key> must be a number from ...`) and
replaced by its default. Both scripts read `userData` once, when they load.

## Sitting and standing

**Sitting.** A primary click on the model (the mouse, or a hand controller's trigger with the
laser on it: the controller pointers report the trigger as the primary button,
`scripts/system/controllers/controllerDispatcher.js`) asks the server script for a seat,
naming every seat of the model nearest the avatar first, and the visitor gets the nearest free
one. Nothing happens in Create's edit mode (`io.highfidelity.isEditing`), while already seated,
or when the nearest seat is more than 5 m away. A refusal or a server that does not answer
within 3 s is logged; nothing moves.

When a seat is granted, the client calls `MyAvatar.beginSit(position, rotation)`: it pins the
**Hips** joint at `position` with `rotation`, turns the avatar's collisions off (so the model's
collision shape does not push a seated avatar; `endSit` turns them back on), disables the
walking keys, and the seated state drives the seated idle animations
(`interface/src/avatar/MyAvatar.cpp`, `libraries/animation/src/Rig.cpp`). `position` is the
`seat_i` joint plus `hipsAboveSeat` times the avatar's height, straight up; `rotation` is the
joint's heading. The joints' world poses come from `Entities.localToWorldPosition` /
`localToWorldRotation` with the joint index, the transform the engine uses for an entity
parented to that joint (`libraries/shared/src/SpatiallyNestable.cpp`). Joints exist only once
Interface has loaded the model, so every pose is read by the client; the server never sees one.

**Standing up.** Click the model again, or hold forward/back (W/S), up (space) or a step key
for `standHoldMs`: `beginSit` disables those keys, but their raw values still read
(`MyAvatar::getRawDriveKey`). Sideways is left alone, since it turns the seated avatar. A key
still held from walking up does not count until it has been let go once. `MyAvatar.endSit`
puts the avatar on `stand_i`, raised by its own feet-to-position height, facing the seat's way.
The avatar also stands, where it is, if it is moved more than 0.5 m off the seat (a teleport, a
go-to), and stands up as usual if another script ends the sit, on a new avatar model or scale,
when the lease is lost, and when the seat's script unloads. Standing up releases the seat.

## The protocol

Other callers can use the server script directly; a Morpheus body command `sit` / `stand` will.
Both methods are called with `Entities.callEntityServerMethod(seatEntityID, method, params)`.

| Method | `params` | Answer |
| --- | --- | --- |
| `claim` | Seat names, the caller's preference first: `["seat_3", "seat_2", "seat_1"]`. Only strings `seat_<N>` (N from 1, no leading zero) count; anything else is ignored, and entries after the 64th are not read | `seatGranted` `[seatName, leaseMs]` when the caller holds a seat; `seatRefused` `["taken"]` when every seat named is held by someone else; `seatRefused` `["no-seat"]` when no valid seat name was given |
| `release` | none | None. Frees the caller's seat, if it holds one |

**The lease.** A seat is held until `leaseMs` passes without a renewal; to renew, call `claim`
again. A caller who holds a seat always gets that same seat back from `claim`, whatever it
names, so one session holds at most one seat per model. A seat whose lease ran out is free to
anyone, its old holder included. The client renews every third of the lease the grant carries,
so one lost renewal does not lose the seat; a crashed or departed sitter frees it after
`leaseMs`. The server has no model, so it takes any well-formed seat name: a caller that names
a seat the model does not have may hold it, and the client script gives back a grant for a seat
whose joints it cannot find.

**The caller.** Who claims or releases is `Script.remoteCallerID`, which the engine stamps from
the sending session, so nobody can claim or release a seat for someone else. A call with no
caller is ignored and gets no answer.

**The answers** are client entity methods, sent with `Entities.callEntityClientMethod` to the
calling session's instance of the seat's `script`: `seatGranted` and `seatRefused`, both in its
`remotelyCallable`. A caller that wants to hear them must run that script, or one with those
two methods, on the seat; the lease itself works without any answer being heard. A
`seatRefused` to a holder's renewal means the seat was lost.

The leases are in the server script's memory only. Nothing is written to `userData` or any
other property: a lease in `userData` would need every visitor to have edit rights on the
entity, and the server script is the one place that sees every claim in order. When the script
reloads, every seat is free, and each sitter's next renewal claims its seat again (or, if
someone took it first, is refused and stands the sitter up).

## Tests

Node (24 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: the core, and both scripts with fake globals
npm run typecheck   # tsc twice: the server script and the client script against their entity declarations
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.
