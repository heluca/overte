# Game Table

Two tables live here:

- **The referee** (`*.mjs`): Connect Four refereed by the entity script server, so humans and AI
  agents can play each other through the same board. The referee owns the game; players only ask
  it to act, and it checks who is asking.
- **The original physics table** (`*.js`, `games/`, below): checkers, chess, dominoes, dice and
  cards as physical pieces, with no rules enforced.

## The referee

| File | Runs in | What it is |
| --- | --- | --- |
| `connect4.mjs` | anywhere | The rules: pure functions over an immutable board, no engine globals |
| `referee-core.mjs` | anywhere | Seats, turns, the clock and the published state; side effects go through an injected world adapter |
| `gametable-referee.mjs` | entity script server | The board's `serverScripts`: the remotely callable methods, and the adapter over `Entities`, `Messages` and `Script` |
| `gametable-client.mjs` | Interface | The client entity `script` of the board, its column targets and its seat buttons: clicks become server method calls, and the channel becomes announcements and highlights |

The modules need the fork's ES module support for entity scripts (`.mjs` `serverScripts` and
`script` with `export default class`). Imports are relative, so the four files and `assets/` must
be served from the same directory.

### Rezzing a board

A board is one Box, upright, with the referee as its `serverScripts`. The referee creates
everything else (the status display and the discs) as children. From the Interface console:

```js
Entities.addEntity({
    type: "Box",
    name: "Game Table: Connect Four",
    position: Vec3.sum(MyAvatar.position, Vec3.multiply(1.5, Quat.getFront(MyAvatar.orientation))),
    rotation: MyAvatar.orientation,
    dimensions: { x: 0.75, y: 0.65, z: 0.04 },
    color: { red: 30, green: 60, blue: 160 },
    serverScripts: "https://<asset-host>/gameTable/gametable-referee.mjs",
    script: "https://<asset-host>/gameTable/gametable-client.mjs",
    userData: JSON.stringify({ gameTableConfig: { turnTimeoutS: 180, waitTimeoutS: 600 } })
});
```

- The disc grid follows the Box's `dimensions`: 7 columns across `x`, 6 rows down `y`.
- Do not lock the board. A locked entity accepts only changes to `locked`, so the referee could
  not write the game into the board's `userData`. On a locked board it still referees (the
  channel, `GT.Status` and the discs work), but it logs a warning, shows `BOARD LOCKED` on
  `GT.Status`, and leaves `userData` alone.
- `gameTableConfig` is read once, when the script loads. `turnTimeoutS` (default 180) is how long
  the player to move has before forfeiting; `waitTimeoutS` (default 600) is how long a lone player
  keeps a seat. Values outside 5 to 86400 seconds fall back to the defaults.
- The operator should rez the table: whoever can edit the board can change its `serverScripts`.

The children are found by name, so a reload does not duplicate them:

- `GT.Status`: a Text entity above the board, in Inconsolata, showing the seats, whose turn it is
  with the time left, the result, and the board as text. `look()` returns Text, so an agent can
  read the game without inspecting the board.
- `GT.Disc r<row>c<col>`: one red or black checker model per disc, collisionless and not
  grabbable. The discs of a winning line are drawn larger. Row 0 is the top row.
- `GT.Col0` to `GT.Col6`: column targets, a strip of small boxes just above the board's top edge,
  one over each column, between the board and `GT.Status`.
- `GT.Seat0` and `GT.Seat1`: seat buttons, Text entities reading "Play Red" and "Play Black" to
  the left and right of the board's bottom corners. They are labelled buttons rather than the old
  table's chair models by decision: a label says what a click does.

The column targets and seat buttons run `gametable-client.mjs` (their `script` is set to the copy
next to the referee) and are triggerable, so hand controllers can press them too.

When the referee loads, it sweeps the board's children for a few seconds while the entity server
delivers them. It adopts `GT.Status`, the column targets and the seat buttons, and deletes any
duplicate of them and any disc it did not create. An adopted child gets its position, dimensions
and (for the clickables) `script` set again from the current layout, so a resized board or a moved
asset host leaves nothing stale. Whichever of the first three it has not found by the end of the
sweep, it creates.

On unload the referee abandons a game in progress (an `over` message with reason `abandoned`). It
then writes the table to `userData` as `offline`, deletes its discs, and shows "Referee not running"
on the status Text. If it loads and finds a game still `playing` in `userData`, the old instance
died without unloading. It then announces that game as abandoned and starts an empty table.

### Server API

Call the methods with `Entities.callEntityServerMethod(boardID, method, params)`. Parameters are
strings only, and nothing comes back: the effect shows in `userData`, on `GT.Status` and on the
channel. Each method reads `Script.remoteCallerID`, the session ID the entity script server stamps
from the caller's authenticated connection, and seats are keyed by it. No parameter names a seat,
so one player cannot act for another.

| Method | Params | Accepted when |
| --- | --- | --- |
| `join` | `[name, seat?]` | The table is `waiting` and the seat is free. `seat` is `"0"` (Red) or `"1"` (Black); absent or `""` takes the first free seat. Idempotent for a seated caller, whatever seat it asks for. The second join starts the game. `name` is cosmetic, at most 32 characters |
| `move` | `[column]` | The game is `playing`, the caller holds the seat to move, and `column` is exactly one digit `0`-`6` whose column has room |
| `leave` | `[]` | The caller is seated. In a game in progress the caller resigns and the opponent wins. The seat is freed and the table waits for a new opponent |
| `state` | `[]` | Always: sends the current state on the channel (call it after subscribing). It stores nothing, so it cannot be used to flood the board with edits; nor does a repeated `join` |
| `newGame` | `[]` | The game is `over` and the caller is seated. Same seats; the first move alternates between games |

A refused call changes nothing and sends a `rejected` message (below). Calls without a caller ID
(the null UUID, as from a script on the server itself) are refused.

The clock: the player to move forfeits when `turnDeadline` passes (`result.reason` `"timeout"`).
A lone seat is freed after `waitTimeoutS` (an `expired` message). A finished game resets to an
empty table after 300 seconds; an empty table starts its next game with Red. Every call applies a
passed deadline before anything else, so a move that arrives after `turnDeadline` is refused even
before the next clock tick. The entity script server cannot see avatars come and go, so these timeouts are how the
referee notices a player has gone.

### State

After every change the referee writes the state into the board's `userData` under `gameTable`,
keeping any other keys. Readers (`Entities.getEntityProperties(boardID, ["userData"])`, Morpheus
`inspect_entity`) see:

```json
{
    "gameTable": {
        "v": 1,
        "game": "connect4",
        "gameId": "{board-id}:1760000000000",
        "phase": "playing",
        "seats": [
            { "session": "{red-session-id}", "name": "Morpheus" },
            { "session": "{black-session-id}", "name": "Robert" }
        ],
        "toMove": 1,
        "moveNo": 3,
        "rows": [".......", ".......", ".......", ".......", "...B...", "..RR..."],
        "legal": [0, 1, 2, 3, 4, 5, 6],
        "last": { "seat": 0, "col": 2, "row": 5 },
        "result": null,
        "turnDeadline": 1760000180000,
        "channel": "gametable:{board-id}"
    }
}
```

| Field | Meaning |
| --- | --- |
| `phase` | `waiting` (seats filling; may still show the last game's board and result), `playing`, `over`, or `offline` (the referee script is not running) |
| `seats` | Seat 0 is Red (`R`), seat 1 is Black (`B`); `null` when free. `session` is the player's session ID, which is the avatar ID other avatars see |
| `toMove` | The seat to move while `playing`, else `null` |
| `rows` | Six strings of seven characters, top row first: `.` empty, `R`, `B` |
| `legal` | Columns with room while `playing`, else `[]` |
| `last` | The last disc played: seat, column and the row it landed on |
| `result` | `null`, or `{ "winner": 0 \| 1 \| null, "reason": "four" \| "draw" \| "timeout" \| "resign" \| "abandoned", "line": [[row, col], ...] \| null }`; `line` is the four winning cells (the first line found, if a move made two) |
| `turnDeadline` | Epoch milliseconds when the player to move forfeits, while `playing` |

The state is output only. The referee never reads it back as input (anyone who can edit the board
can change `userData`), and its next write repairs any change. The one exception is at load: a
`playing` phase there means the previous instance died mid-game.

### Messages channel

The referee sends JSON on `gametable:<board id>` (the `channel` field) and never listens on it.
Moves arrive only through the server methods, whose caller the engine authenticates; a Messages
sender ID is whatever the sending client wrote.

| `type` | When | Other fields |
| --- | --- | --- |
| `state` | After every change, on `state`, and on a repeated `join` | The whole state above |
| `rejected` | A call was refused | `to` (the caller's session ID), `method`, `reason` |
| `over` | A game ended, or was abandoned by an unload or a crash (`result.reason` `"abandoned"`) | `gameId`, `result`, `seats` as they played |
| `expired` | A lone player's seat timed out | `seat`, `session`, `name` |

`reason` for `rejected` is one of: `no caller id`, `table full`, `seat taken`, `seat must be 0 or 1`,
`game not started`, `game over`, `game not over`, `not seated`, `not your turn`,
`column must be one digit 0-6`, `column full`.

### Playing as a human

1. Rez the board with both scripts set, as above: `serverScripts` (the referee) and `script` (the
   client). Within a few seconds the status display, the seven column targets and the two seat
   buttons appear.
2. Click "Play Red" or "Play Black" (or point a hand controller at it and pull the trigger). The
   referee seats you in that seat under your display name, unless someone already holds it. When
   both seats are taken the game starts.
3. On your turn an announcement says "Your turn" and the columns you can play light up green. Click
   the target above a column to drop a disc there.

Click your own seat button again to leave. In a game in progress leaving resigns, so the first
click only announces "Click your seat again within 5 s to resign the game", and a second click
within 5 seconds resigns. Anything the
referee refuses ("not your turn", "column full", "table full") comes back as an announcement. There
is no rematch button yet: for a rematch one player leaves and rejoins, which starts the next game.
The table also resets on its own 300 seconds after a game ends.

The client is the same module on every one of those entities, with a role taken from the entity's
name:

- A column target calls `Entities.callEntityServerMethod(board, "move", [String(column)])`.
- A seat button calls `join` with `MyAvatar.sessionDisplayName` and its own seat, or `leave` when
  the board's `userData` shows the user in that seat (confirmed by a second click during a game).
- The board's own instance subscribes to the channel named in `userData` (or `gametable:<board
  id>` if there is no state there yet). It calls `state` once if `userData` has no state, or one
  that looks stale (`offline`, or `playing` past its `turnDeadline`). It announces what concerns
  the user with `Window.displayAnnouncement`: whose turn it is, waiting for an opponent, the result,
  losing the seat, the referee going offline, and refusals addressed to the user's session ID.
  Spectators get no announcement for every move. While it is the user's turn it puts a green box
  around each legal column target. These boxes are local entities: the user sees them, nobody else
  does, and they need no edit rights. They ignore picks, so the click goes through to the target.
  A target that has not reached the client yet is looked for again every second; a highlight whose
  target is deleted moves to the one that replaces it.

The client keeps no game state and writes nothing to the board: the referee decides everything,
and the client only asks. Channel messages are self-asserted (any client can send one on the
channel), so the client treats them as cosmetic: they change only what the user is shown, never
what is sent to the referee, which checks every caller itself. There is no tap sound on a click. On unload the board's instance unsubscribes and deletes its highlights.
Messages subscriptions belong to the whole Interface session (one `MessagesClient`), so any other
script of yours listening on the same channel stops receiving it too.

If Interface's entity script allowlist is enabled, it must admit the client's URL.

### How an agent plays

An agent is a player like any other: its session ID is its seat. The referee neither knows nor
cares which players are AI. From an Agent assignment client script, with `chooseColumn` the
agent's own. The channel name comes from the board's state. The same state can arrive more than
once (after `state`, a repeated `join`, or a refused call), so the agent plays each turn once:

```js
const board = "<board entity id>";
const userData = JSON.parse(Entities.getEntityProperties(board, ["userData"]).userData || "{}");
const channel = userData.gameTable.channel;
let lastTurn = null;
Messages.subscribe(channel);
Messages.messageReceived.connect((ch, text) => {
    if (ch !== channel) {
        return;
    }
    const message = JSON.parse(text);
    if (message.type !== "state" || message.phase !== "playing") {
        return;
    }
    const turn = message.gameId + "/" + message.moveNo;
    const mine = message.seats.findIndex(seat => seat && seat.session === Agent.sessionUUID);
    if (mine === message.toMove && turn !== lastTurn) {
        lastTurn = turn;
        Entities.callEntityServerMethod(board, "move", [String(chooseColumn(message.rows, message.legal))]);
    }
});
Entities.callEntityServerMethod(board, "join", ["Morpheus"]);
Entities.callEntityServerMethod(board, "state", []);
```

Agents cannot call server methods yet: the assignment client has no entity script client until
heluca/overte#30 is fixed. Morpheus will expose the same loop as tools (`call_server_method`,
`subscribe_channel`, and `recent_messages` with a long-poll for "wait for my turn"). Until then an
agent can read the game (`look()` for `GT.Status`, `inspect_entity` for `userData`) but not play.

### Development

Node (24 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: the rules, the referee core, and the entry with fake globals
npm run typecheck   # tsc twice: the referee against the server entity declarations, the client against the client entity ones
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.
`Script.remoteCallerID` and `import.meta.url` come from those generated declarations: a
`remoteCallerID` property on the `Script` JSDoc in
`libraries/script-engine/src/ScriptManagerScriptingInterface.h` and an `ImportMeta` declaration
from `tools/typings/generate.js` (heluca/overte#46). No local stop-gap typings are needed.

## The original physics table

Ready to play? Try out our new gaming table, with five games to choose from: checkers, chess,
dominoes, dice, and a deck of cards! Add your own game!

The gaming table is designed with the following principles:

1. Gets its list of games from a remote location, and so can be updated without requiring a new
   download by users.
2. New games can be defined in a simple JSON data structure.
3. Games are made for using hand controllers. Pieces are physical as much as possible.
4. Simple controls: reset game, next game.
5. No 'rules' are enforced. It's up to players to respect each other.

### Adding games

There are two spawning styles for games, depending on the type of game you're making: 'arranged'
spawning and 'pile' spawning.

For 'arranged' style games, the table divides its surface into tiles based on how many rows are in
the arrangement. Currently, the boards must be square (i.e. 8x8 or 10x10). Then it goes through the
starting arrangement and pastes the JSON for each piece at the appropriate location. A player
number in front of the piece name in the game definition helps pick the right color piece for games
where player pieces are similar. Releasing a held piece with 'snapToGrid' on will result in that
piece snapping to the nearest available tile.

### JSON game parameters

Both styles:

- `@gameName` - string - what the game is called. `checkers`
- `@matURL` - string - url of picture to put on the table. `http://mywebsite/checkers.jpg`
- `@spawnStyle` - string - either `pile` or `arranged`.

Arranged style:

- `@snapToGrid` - boolean - should pieces snap to tiles
- `@startingArrangement` - 2d array - ['playerNumber:pieceName'] - `["1:rook","2:queen"]`
- `@pieces` - array of object mappings - one object mapping per player - {pieceName:JSONurl} -
  `[{"king": "http://mywebsite/chess/king_black.json"},{"king": "http://mywebsite/chess/king_white.json"}]`

Pile style:

- `@identicalPieces` - boolean - whether or not the pieces are identical. If `true`, only the first
  piece is used and repeatedly spawned.
- `@howMany` - number - the amount of pieces to create
- `@pieces` - array of strings - `["http://mywebsite/domino.json"]`

### Testing

Change the `GAMES_LIST_ENDPOINT` variable in `table.js` to point at your array of game .json files
on a local or remote server.

The list should look more or less like this: https://api.myjson.com/bins/437s6

### Publishing

- As a script: just add `createGameTable.js` to the marketplace.
- As an item: run `createGameTable.js` and select everything, export it to json. Go through the
  usual marketplace uploading and prep sequence.
