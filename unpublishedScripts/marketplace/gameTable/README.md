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

The modules need the fork's ES module support for entity scripts (`.mjs` `serverScripts` with
`export default class`). Imports are relative, so the three files and `assets/` must be served
from the same directory.

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
    userData: JSON.stringify({ gameTableConfig: { turnTimeoutS: 180, waitTimeoutS: 600 } })
});
```

- The disc grid follows the Box's `dimensions`: 7 columns across `x`, 6 rows down `y`.
- Do not lock the board. A locked entity accepts only changes to `locked`, and the referee writes
  the game into the board's `userData`.
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

On unload the referee deletes its discs and shows "Referee not running" on the status Text. When
it loads and finds a game still in progress in `userData`, the old instance died mid-game: it
announces that game as abandoned and starts an empty table.

### Server API

Call the methods with `Entities.callEntityServerMethod(boardID, method, params)`. Parameters are
strings only, and nothing comes back: the effect shows in `userData`, on `GT.Status` and on the
channel. Each method reads `Script.remoteCallerID`, the session ID the entity script server stamps
from the caller's authenticated connection, and seats are keyed by it. No parameter names a seat,
so one player cannot act for another.

| Method | Params | Accepted when |
| --- | --- | --- |
| `join` | `[name]` | The table is `waiting` and has a free seat; the first joiner is Red (seat 0). Idempotent for a seated caller. The second join starts the game. `name` is cosmetic, at most 32 characters |
| `move` | `[column]` | The game is `playing`, the caller holds the seat to move, and `column` is exactly one digit `0`-`6` whose column has room |
| `leave` | `[]` | The caller is seated. In a game in progress the caller resigns and the opponent wins. The seat is freed and the table waits for a new opponent |
| `state` | `[]` | Always: republishes the current state on the channel (call it after subscribing) |
| `newGame` | `[]` | The game is `over` and the caller is seated. Same seats; the first move alternates between games |

A refused call changes nothing and sends a `rejected` message (below). Calls without a caller ID
(the null UUID, as from a script on the server itself) are refused.

The clock: the player to move forfeits when `turnDeadline` passes (`result.reason` `"timeout"`).
A lone seat is freed after `waitTimeoutS`. A finished game resets to an empty table after 300
seconds. The entity script server cannot see avatars come and go, so these timeouts are how the
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
| `phase` | `waiting` (seats filling; may still show the last game's board and result), `playing`, `over` |
| `seats` | Seat 0 is Red (`R`), seat 1 is Black (`B`); `null` when free. `session` is the player's session ID, which is the avatar ID other avatars see |
| `toMove` | The seat to move while `playing`, else `null` |
| `rows` | Six strings of seven characters, top row first: `.` empty, `R`, `B` |
| `legal` | Columns with room while `playing`, else `[]` |
| `last` | The last disc played: seat, column and the row it landed on |
| `result` | `null`, or `{ "winner": 0 \| 1 \| null, "reason": "four" \| "draw" \| "timeout" \| "resign", "line": [[row, col], ...] \| null }`; `line` is the four winning cells |
| `turnDeadline` | Epoch milliseconds when the player to move forfeits, while `playing` |

The state is output only. The referee never reads it back as input (anyone who can edit the board
can change `userData`), and its next write repairs any change.

### Messages channel

The referee sends JSON on `gametable:<board id>` (the `channel` field) and never listens on it.
Moves arrive only through the server methods, whose caller the engine authenticates; a Messages
sender ID is whatever the sending client wrote.

| `type` | When | Other fields |
| --- | --- | --- |
| `state` | After every change, and on `state` | The whole state above |
| `rejected` | A call was refused | `to` (the caller's session ID), `method`, `reason` |
| `over` | A game ended, or a reload found one abandoned (`reason` `"abandoned"`) | `gameId`, `result`, `seats` |

`reason` for `rejected` is one of: `no caller id`, `table full`, `game not started`, `game over`,
`game not over`, `not seated`, `not your turn`, `column must be one digit 0-6`, `column full`.

### How an agent plays

An agent is a player like any other: its session ID is its seat. The referee neither knows nor
cares which players are AI. From an Agent assignment client script, with `chooseColumn` the
agent's own:

```js
const board = "<board entity id>";
const channel = "gametable:" + board;
Messages.subscribe(channel);
Messages.messageReceived.connect((ch, text) => {
    if (ch !== channel) {
        return;
    }
    const message = JSON.parse(text);
    if (message.type !== "state" || message.phase !== "playing") {
        return;
    }
    const mine = message.seats.findIndex(seat => seat && seat.session === Agent.sessionUUID);
    if (mine === message.toMove) {
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
npm run typecheck   # tsc over the three modules against tools/typings' server entity declarations
```

`typecheck` needs the declarations built first: `npm ci && npm run build` in `tools/typings`.
`overte-server-entity-extras.d.ts` declares the two things the generated declarations lack:
`Script.remoteCallerID` and `import.meta.url`.

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
