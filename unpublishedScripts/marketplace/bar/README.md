# Bar

A bar entity whose server script takes drink orders and lets only the bartender serve them. Visitors
in Interface and Morpheus bodies (LLM agents in Agent assignment clients) order from the menu in
the bar's `userData`. Only the bartender session named in `privateUserData.staff` can serve, and each
served order puts a glass on the counter in front of the person who ordered it. This is the game
table's referee pattern (Morpheus ADR 0003): chat and channel messages are untrusted, and the
bar authenticates only server calls, by `Script.remoteCallerID`. Every call is rate-limited per
session (ADR 0016). Its first home is the Delos piano bar (heluca/overte#107, helu.ca/morpheus#102,
r/phantasos#5).

| File | Runs in | What it is |
| --- | --- | --- |
| `bar-config.mjs` | anywhere | Pure logic: the `userData` configuration checked (menu, glasses, channel, counter, limits), who `privateUserData.staff` names, which menu item an order means |
| `bar-orders.mjs` | anywhere | Pure logic: the caller and staff checks, the per-session rate limiter, the order book (time to live, one open order per session) |
| `bar-core.mjs` | anywhere | Pure logic: what each call does, through an injected world adapter; the status mirror; where a glass stands |
| `bar-entity.mjs` | entity script server | The bar's `serverScripts`: the remotely callable methods, the channel posts, the `userData.status` mirror and the glasses |

The modules need the fork's ES module support for entity scripts (`.mjs` `serverScripts` with
`export default class`). They import each other by relative URL, so all four files must be served
from the same directory. phantasos bundles them for Ergane from a pinned commit of this fork
(`scripts/`, as it does `piano-scripts`). No other repository keeps a copy.

## Attaching it

The script goes on the bar entity as `serverScripts`, and its configuration goes in that entity's
`userData`. On Delos the bar is the `delos-bar` Model. From the Interface console, with edit rights
on it:

```js
var bar = "<delos-bar entity id>";
var userData = JSON.parse(Entities.getEntityProperties(bar, ["userData"]).userData || "{}");
userData.menu = [
    { name: "Bordeaux", kind: "wine", glass: "wine" },
    { name: "Burgundy", kind: "wine", glass: "wine" },
    { name: "Rosé", kind: "wine", glass: "wine" },
    { name: "Champagne", kind: "sparkling", glass: "flute" },
    { name: "Ouzo", kind: "spirit", glass: "highball" },
    { name: "Negroni", kind: "cocktail", glass: "rocks" },
    { name: "Old Fashioned", kind: "cocktail", glass: "rocks" },
    { name: "Martini", kind: "cocktail", glass: "coupe" },
    { name: "Sparkling water", kind: "soft", glass: "highball" }
];
userData.channel = "delos:bar";
userData.counter = { y: 0.38, z: -0.15, halfLength: 2.9 };
userData.glasses = {};
userData.maxOpenOrders = 12;
userData.orderTtlS = 900;
userData.glassLifetimeS = 600;
Entities.editEntity(bar, {
    serverScripts: "https://<asset-host>/bar/bar-entity.mjs",
    userData: JSON.stringify(userData)
});
```

To try it away from Delos, a Box works too, with the same `userData` minus `counter`. The glass
then stands on top of the box.

The script reads its configuration once, at load. After you change the menu or a limit, reload the
script by setting `serverScripts` again. The script writes `userData.status` itself, and reads
`privateUserData` (the staff handshake) afresh on every call.

### userData

The keys sit at the top level of the bar's `userData`. Every key is optional. A bad value is logged
as `Bar <id>: ...` and then replaced by its default, or dropped if it is a menu item or a glass.

| Key | Default | What it is |
| --- | --- | --- |
| `menu` | none (every order is refused) | Up to 50 items `{ name, kind, glass }`. `name` is up to 40 characters and unique ignoring case; orders match it ignoring case. `kind` is free text (`wine`, `cocktail`, ...). `glass` is a key into `glasses` |
| `glasses` | `{}` | `{ <glass>: { url, dimensions? } }`. `url` is an `http(s)://` or `atp:` model URL. `dimensions` is in metres, each at most 2, default `{ x: 0.08, y: 0.16, z: 0.08 }`. A menu item whose glass is missing here is served as a Text placeholder with the drink's name |
| `channel` | `bar:<entity id>` | The Messages channel: 1 to 64 of `A-Z a-z 0-9 : . _ -` |
| `counter` | the top of the entity's box along its x axis | `{ y, z, halfLength }` in metres, in the bar's frame. The frame is centred on the entity's box and turns with it; the script reads `registrationPoint`, so the centre is right whatever the registration point. A glass stands at height `y` and depth `z`, anywhere along x within `±halfLength` |
| `maxOpenOrders` | 12 (1 to 100) | Open orders across the whole bar |
| `orderTtlS` | 900 (30 to 7200) | An order not served in this time expires |
| `glassLifetimeS` | 600 (10 to 3600) | The glass entity's `lifetime` |
| `maxGlasses` | 24 (1 to 200) | Glasses out at once. Serving one more deletes the oldest |
| `callsPerWindow` | 10 (1 to 100) | Calls one session may make in `callWindowS`, all methods counted |
| `callWindowS` | 30 (1 to 600) | The rate limit's sliding window |
| `status` | written by the script | The status mirror, below |

`staff` in `userData` counts for nothing: the bartender is named in `privateUserData`, below.

The Delos `counter` comes from the `delos-bar` spec in phantasos (`props/delos-bar/spec.md` §3). The
box is 6.0 x 1.44 x 1.4 m with its centre 0.72 m up (at the default registration point; another
registration point is allowed, as the script measures from the box's centre). The marble top is at 1.10 m (local y 0.38) and
spans z -0.65 to 0.0, with the customers on +z. Glasses stand 0.15 m in from the customer edge,
anywhere along the 5.8 m of counter that the end panels leave.

### The staff handshake

The bartender is named in the bar's `privateUserData`, never in `userData`. Only server entity
scripts, assignment-client scripts (Morpheus's bodies are Agents) and users with the domain's "Can
Get and Set Private User Data" permission can read or write it
(`libraries/entities/src/EntityItemPropertiesDocs.cpp`; the entity server drops a
`privateUserData` edit from anyone else). A guest who can edit the entity still cannot name
themselves bartender, and the bar never writes `privateUserData`, so nothing races with Morpheus.

| `privateUserData` key | Default | What it is |
| --- | --- | --- |
| `staff` | none | The bartender: a session id. Only that session can `serve`, and `cancel` any order |
| `staffByName` | `false` | Dev domains only: `staff` is then a session display name |

- **Morpheus (every domain):** when the bartender body takes its post, Morpheus writes the body's
  session id (`Agent.sessionUUID`) into `privateUserData.staff` with `edit_entity`. When the body
  leaves its post, it deletes the key. The bar reads the key on every call, so the change works
  immediately, and the mirror's `staffPresent` follows within the 2 s tick. A body that
  reconnects gets a new session id, and Morpheus writes it again. The body re-asserts `staff`
  only when `inspect_entity` shows `privateUserData.staff` missing or not its session. It never
  does so because of a channel post: anyone can post anything on a channel under any sender id.
- **Dev domains:** with `staffByName: true`, `staff` can be a name, such as `"Bartender"`. It is
  matched against the session display name only, never the display name. The avatar mixer keeps
  session display names unique (`AvatarMixer.cpp`, a `_N` suffix on a repeat). It still starts
  from a name the avatar chose, so never set `staffByName` on a public domain.

## Server API

Clients call the methods with `Entities.callEntityServerMethod(bar, method, params)`. The engine
stamps the caller's authenticated session in `Script.remoteCallerID`. That call returns nothing,
so every answer arrives on the channel. A call with no caller id is only logged.

| Method | Params | Who | Effect |
| --- | --- | --- | --- |
| `order` | `[drink]` | anyone | A menu item's name, any case. One open order per session; at most `maxOpenOrders` in the bar. Posts `order` |
| `serve` | `[orderId]` | staff | Takes the order off the book, sets its glass on the counter, posts `served` |
| `cancel` | `[orderId]` | the orderer or staff | Takes the order off the book, posts `cancelled` |
| `status` | `[]` | anyone | Posts `status` to the caller: the mirror plus the open orders. Stores nothing, unless an order had expired unpublished |
| `menu` | `[]` | anyone | Posts `menu` to the caller. Stores nothing |

Every method, read-only ones included, counts against the caller's `callsPerWindow`. The refusal
of a call over the limit is posted once per window and only logged after that. A caller that
keeps calling cannot turn refusals into channel traffic. Any expired order is dropped before a
call runs, so nobody can serve an order between ticks.

### Ordering from Interface

There is no order UI yet: no client script or tablet app. From the console:

```js
var bar = "<bar entity id>";
var channel = JSON.parse(Entities.getEntityProperties(bar, ["userData"]).userData).status.channel;
Messages.subscribe(channel);
Messages.messageReceived.connect(function (ch, text) {
    if (ch === channel) {
        print(text);
    }
});
Entities.callEntityServerMethod(bar, "menu", []);
Entities.callEntityServerMethod(bar, "order", ["Negroni"]);
```

Your `order` post carries the order's `id`, and `by` is your session (`MyAvatar.sessionUUID`).
You can `cancel` it with that id.

### Ordering, and serving, as a body

A body uses the same calls through Morpheus's tools (protocol v3): `call_server_method`
(`entityId`, `method`, `args`), `subscribe` on the channel, and `inspect_entity` to read
`userData.status`. The body's fence must allow them: the bar in `MORPHEUS_CALLABLE`, the channel in
`MORPHEUS_CHANNELS`, and, for the bartender's handshake, the bar in `MORPHEUS_EDITABLE`. Underneath,
the Agent script runs the same call as Interface:

```js
Entities.callEntityServerMethod(bar, "order", ["Ouzo"]);    // a guest body
Entities.callEntityServerMethod(bar, "serve", [orderId]);   // the bartender, from an `order` post
```

A bartender that has just taken its post calls `status` to fetch the open orders, then serves
from the `order` posts as they come in. Channel posts are forgeable, so before it serves, the
bartender checks that the id is in `userData.status.orderIds` (`inspect_entity`); a forged
`order` post names an id the bar never issued. `rejected` messages addressed to it (`to`) say why
a call was refused, and its driver must pass them on to the mind (ADR 0008).

The `name` in `order`, `served` and `status` posts is the orderer's session display name, which
the guest chose. Like chat, it is untrusted input to the bartender's mind (ADR 0016): something
to say aloud at most, never an instruction to follow.

## Messages channel

The bar sends JSON on its channel (the mirror's `channel`) and never listens on it. Orders arrive
only through the server methods. The sender id on a Messages post is whatever the sending client
wrote, so a channel post is never authority for anything.

| `type` | When | Fields |
| --- | --- | --- |
| `order` | An order is placed | `id`, `by` (the orderer's session), `name` (their session display name: guest-chosen, untrusted), `drink`, `kind`, `glass` |
| `served` | Staff served it | `id`, `by` (the orderer), `name`, `drink`, `servedBy` (the bartender's session), `glass` (the glass entity's id, or `null`) |
| `cancelled` | The orderer or staff took it back | `id`, `by`, `drink`, `cancelledBy` |
| `expired` | It waited `orderTtlS` | `id`, `by`, `drink` |
| `status` | Reply to `status` | `to`, the mirror's fields, `orders`: `[{ id, by, name, drink, ageS }]`, oldest first |
| `menu` | Reply to `menu` | `to`, `items`: `[{ name, kind, glass }]` |
| `rejected` | A call was refused | `to` (the caller's session), `method`, `reason`, and `id` when the call named an order |
| `closed` | The script unloaded; its open orders are gone | `ids` |

`reason` is one of: `not on the menu`, `you already have an open order` (with that order's `id`),
`the bar is full: N orders open`, `only the bartender serves`, `no such order`, `not your order`,
and `too many calls: at most N every W s; try again in T s`.

## Status mirror

`userData.status` is written whenever the bar's state changes: an order, a serve, a cancel or an
expiry, the bartender arriving or leaving, a load or an unload. Being minutes older does not count
as a change, so the mirror does not churn while orders wait. Bodies read it with `inspect_entity`,
as they read the piano's.

```json
{
    "v": 1,
    "state": "open",
    "channel": "delos:bar",
    "open": 2,
    "orderIds": ["mgx3k2a1-5", "mgx3k2a1-6"],
    "oldestAgeS": 75,
    "staffPresent": true,
    "lastServed": { "id": "mgx3k2a1-4", "drink": "Ouzo", "by": "{...}", "at": 1761760000000 },
    "at": 1761760075000
}
```

- `state` is `"offline"` once the script has unloaded, with `open` 0. Orders do not survive a
  reload.
- `orderIds` are the open orders' ids, oldest first, at most `maxOpenOrders`. A bartender checks
  an id here before it serves, because a channel post can be forged.
- `oldestAgeS` is the age of the oldest open order at time `at`, or `null` when none is open.
- `staffPresent` means the `privateUserData.staff` session is in `AvatarList`. With `staffByName`,
  it means some avatar has that session display name.
- `at` and `lastServed.at` are the entity script server's epoch milliseconds.

A locked bar takes no `userData` edit, so it has no mirror. The script logs this at load, and
orders and the channel still work.

## The glass

The entity script server exposes `AvatarList` in this fork.
`assignment-client/src/scripts/EntityScriptServer.cpp` registers it on every entity script engine
(`registerGlobalObject(..., "AvatarList", DependencyManager::get<AvatarHashMap>())`). The script
server also lists the avatar mixer in its node interest set, and the mixer broadcasts avatar data
to `EntityScriptServer` nodes (`AvatarMixerWorker::broadcastAvatarData`). So the bar can read the
orderer's position and session display name.

On `serve` the bar:

1. Finds the centre of the bar's box from its `position`, `rotation`, `dimensions` and
   `registrationPoint`, so any registration point works.
2. Reads the orderer's position from `AvatarList`, if their session is still in it.
3. Takes the point on the counter line (`counter.y`, `counter.z`) nearest that position, clamped
   to `±halfLength`. That puts the glass on the counter right in front of the orderer, whichever
   stool they sit on. An orderer who has left gets the glass in the middle of the counter.
4. Creates an unparented domain entity there with `lifetime` set to `glassLifetimeS`,
   `collisionless`, `grabbable`, named `Bar.Glass <drink>`, with
   `userData.barGlass = { bar, order, drink, for }`:
   - a **Model** from `glasses[item.glass]` (`url`, `dimensions`), standing on the counter;
   - otherwise a **Text** showing the drink's name (billboarded, 0.4 x 0.1 m), until the glassware
     bundle lands.

Avatar positions in `AvatarList` are as fresh as the mixer sends them, and the script server has
no camera, so a far or crowded avatar may lag slightly. The glass is set once and does not follow
anyone.

## Limits

Every write has a bound (ADR 0016):

- **Calls:** `callsPerWindow` per `callWindowS` per session, every method counted. Each admitted
  call makes at most one `userData` edit and one reply post, plus one `expired` post for each
  order whose time it finds has run out. Each order expires once, so those posts add up to at
  most one per order placed. A refused caller is told once per `callWindowS` and only logged
  after that.
- **Orders:** one open per session, `maxOpenOrders` in the bar, each gone after `orderTtlS`.
- **Glasses:** only staff creates them, within staff's own call limit. Each lives
  `glassLifetimeS`, and at most `maxGlasses` exist at once.
- **The tick** (every 2 s) writes `userData` at most once, and only when an order expires or the
  bartender comes or goes.
- **The staff handshake** costs the bar nothing: Morpheus writes `privateUserData`, and the bar
  only reads it.
- A session is not a person: someone who reconnects starts with a fresh window and no open order.
  `maxOpenOrders` bounds what that gains them.

## Development

Node (24 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: the core, and the entry against fake globals
npm run typecheck   # tsc over the four modules against the server entity declarations
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.

## Verified, and not yet

- The core and the entry pass their Node tests, and all four modules type-check against the server
  entity declarations.
- Not yet run on a domain. The steps still to check there:
  - the script server's `AvatarList` positions and names as the orderer sits at the bar;
  - the counter offsets on the published `delos-bar` model;
  - a body's `call_server_method` round trip;
  - Morpheus's `edit_entity` and `inspect_entity` carrying `privateUserData` (the body is an
    assignment client, which the domain server grants "Can Get and Set Private User Data").
