# Game Table

Two tables live here:

- **The referee** (`*.mjs`): Connect Four and checkers refereed by the entity script server, so
  humans and AI agents can play each other through the same board. The referee owns the game;
  players only ask it to act, and it checks who is asking.
- **The original physics table** (`*.js`, `games/`, below): checkers, chess, dominoes, dice and
  cards as physical pieces, with no rules enforced.

## The referee

| File | Runs in | What it is |
| --- | --- | --- |
| `connect4.mjs`, `checkers.mjs` | anywhere | The rules of each game: pure functions over an immutable state, no engine globals, behind one contract (below) |
| `game-registry.mjs` | anywhere | The games a board can play, by name: each game's rules, its layout, and for 8x8 games its piece models and how its notation maps to squares |
| `referee-core.mjs` | anywhere | Seats, turns, the clock, draw offers, rematches and the published state, for any game; side effects go through an injected world adapter |
| `status-text.mjs` | anywhere | What `GT.Status` shows |
| `grid-select.mjs` | anywhere | Turning clicks on an 8x8 board into one of the legal moves |
| `gametable-referee.mjs` | entity script server | The board's `serverScripts`: the remotely callable methods, the child sweep, and the adapter over `Entities`, `Messages` and `Script` |
| `layout-connect4.mjs`, `layout-grid.mjs` | entity script server | Where each board's children go, and drawing its pieces: the upright Connect Four board, and the flat 8x8 board |
| `gametable-client.mjs`, `client-grid.mjs` | Interface | The client entity `script` of the board, its click targets and its seat buttons: clicks become server method calls, and the channel becomes announcements and highlights |

The modules need the fork's ES module support for entity scripts (`.mjs` `serverScripts` and
`script` with `export default class`). Imports are relative, so the files and `assets/` must be
served from the same directory.

### Rezzing a board

A board is one Box with the referee as its `serverScripts` and the client as its `script`. The
referee creates everything else (the status display, the click targets, the seat buttons and the
pieces) as children. The game is chosen once, when the referee loads, from
`userData.gameTableConfig.game`. From the Interface console, a Connect Four board (upright):

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

A checkers board (flat, at table height; put it on a table and lower `position` to suit):

```js
Entities.addEntity({
    type: "Box",
    name: "Game Table: Checkers",
    position: Vec3.sum(MyAvatar.position, Vec3.multiply(0.8, Quat.getFront(MyAvatar.orientation))),
    rotation: Quat.cancelOutRollAndPitch(MyAvatar.orientation),
    dimensions: { x: 0.6, y: 0.04, z: 0.6 },
    color: { red: 60, green: 40, blue: 25 },
    serverScripts: "https://<asset-host>/gameTable/gametable-referee.mjs",
    script: "https://<asset-host>/gameTable/gametable-client.mjs",
    userData: JSON.stringify({ gameTableConfig: { game: "checkers", turnTimeoutS: 300 } })
});
```

`gameTableConfig` is read once, when the script loads:

| Key | Default | Meaning |
| --- | --- | --- |
| `game` | `"connect4"` | `"connect4"` or `"checkers"`. Any other value (including `"chess"` until chess is registered in `game-registry.mjs`) logs a warning and plays Connect Four |
| `turnTimeoutS` | 180 | How long the player to move has before forfeiting |
| `waitTimeoutS` | 600 | How long a lone player keeps a seat |

Timeouts outside 5 to 86400 seconds fall back to the defaults. To change a board's game, edit
`gameTableConfig.game` and reload the referee (or rez a new board): it removes the other game's
children and lays out its own.

- Do not lock the board. A locked entity accepts only changes to `locked`, so the referee could
  not write the game into the board's `userData`. On a locked board it still referees (the
  channel, `GT.Status` and the pieces work), but it logs a warning, shows `BOARD LOCKED` on
  `GT.Status`, and leaves `userData` alone.
- The operator should rez the table: whoever can edit the board can change its `serverScripts`.

The children are found by name, so a reload does not duplicate them. On a Connect Four board
(upright, the disc grid across its front face, 7 columns across `x`, 6 rows down `y`):

- `GT.Status`: a Text entity above the board, in Inconsolata, showing the seats, whose turn it is
  with the time left, a standing draw offer, the result, and the board as text. `look()` returns
  Text, so an agent can read the game without inspecting the board.
- `GT.Disc r<row>c<col>`: one red or black checker model per disc, collisionless and not
  grabbable. The discs of a winning line are drawn larger. Row 0 is the top row.
- `GT.Col0` to `GT.Col6`: column targets, a strip of small boxes just above the board's top edge,
  one over each column, between the board and `GT.Status`.
- `GT.Seat0` and `GT.Seat1`: seat buttons, Text entities reading "Play Red" and "Play Black" to
  the left and right of the board's bottom corners. They are labelled buttons rather than the old
  table's chair models by decision: a label says what a click does.

On an 8x8 board (flat: the playing surface is the Box's local +y face, the squares across local
`x` and `z`; seat 0 plays from the +z edge, which faces the rezzer when the board is rezzed with
the avatar's orientation, and seat 1 from the -z edge):

- `GT.Status`: as above, raised over the far (-z) edge and turning to face the viewer.
- `GT.Sq r<row>c<col>`: 64 thin square tiles on the surface, light and dark (the dark ones,
  `(row + col)` odd, are the checkers squares). Row and column are those of `rows` in the state.
  Each side's home rows are nearest its own seat button: in checkers squares 1-12 (row 0) are at
  seat 0's edge, and each player has a dark square at their near-left corner.
- `GT.Pc r<row>c<col>`: one model per piece, collisionless, not grabbable, and ignoring picks
  (`ignorePickIntersection`) so a click on a piece lands on its tile. Checkers men are the red and
  black checker models; a king is the same model drawn two and a half times as tall. A move edits the
  piece entities (position, model, size) rather than deleting and adding them.
- `GT.Seat0` and `GT.Seat1`: seat buttons at each player's edge, to their right, reading "Play"
  and the side's name ("Play Black", "Play Red" for checkers).

The column targets and seat buttons run `gametable-client.mjs` (their `script` is set to the copy
next to the referee) and are triggerable, so hand controllers can press them too. The tiles run no
script (see "Playing as a human").

When the referee loads, it sweeps the board's children for a few seconds while the entity server
delivers them. It adopts its layout's fixed children (`GT.Status`, the click targets or tiles,
and the seat buttons), and deletes any duplicate of them, any piece it did not create, and any
other `GT.` child (left by another game). An adopted child gets its position, size, look and (for
the clickables) `script` set again from the current layout, so a resized board, a moved asset
host or a change of game leaves nothing stale. Whichever fixed child it has not found by the end
of the sweep, it creates.

On unload the referee abandons a game in progress (an `over` message with reason `abandoned`). It
then writes the table to `userData` as `offline`, deletes its pieces, and shows "Referee not
running" on the status Text. If it loads and finds a game still `playing` in `userData`, the old
instance died without unloading. It then announces that game as abandoned and starts an empty
table.

### Server API

Call the methods with `Entities.callEntityServerMethod(boardID, method, params)`. Parameters are
strings only, and nothing comes back: the effect shows in `userData`, on `GT.Status` and on the
channel. Each method reads `Script.remoteCallerID`, the session ID the entity script server stamps
from the caller's authenticated connection, and seats are keyed by it. No parameter names a seat,
so one player cannot act for another.

| Method | Params | Accepted when |
| --- | --- | --- |
| `join` | `[name, seat?]` | The table is `waiting` and the seat is free. `seat` is `"0"` or `"1"` (Red or Black in Connect Four, Black or Red in checkers); absent or `""` takes the first free seat. Idempotent for a seated caller, whatever seat it asks for. The second join starts the game. `name` is cosmetic, at most 32 characters |
| `move` | `[move]` | The game is `playing`, the caller holds the seat to move, and `move` is one of `legal` in the game's notation (below). The text is cut to 64 characters before it is parsed |
| `draw` | `[]` | The game is `playing` and the caller is seated. With the opponent's offer standing, the game ends drawn (`agreement`); otherwise the caller's offer is recorded as `drawOffer` and published. The opponent moving declines it; offering again while your own offer stands does nothing |
| `leave` | `[]` | The caller is seated. In a game in progress the caller resigns and the opponent wins. The seat is freed and the table waits for a new opponent |
| `state` | `[]` | Always: sends the current state on the channel (call it after subscribing). It stores nothing, so it cannot be used to flood the board with edits; nor does a repeated `join` |
| `newGame` | `[]` | The game is `over` and the caller is seated. A rematch, as below |

Rematches follow the game. Connect Four alternates the first move: the same seats, and whoever
moved second moves first. In checkers (and chess) side 0 always moves first and the players swap
seats instead. Either way the change happens on `newGame` and when a new opponent joins someone
who has just played (so the player who stays changes colour in checkers, and the newcomer gets
the colour the stayer had). An emptied table starts over: seat 0 moves first and nobody swaps.

A refused call changes nothing and sends a `rejected` message (below). Calls without a caller ID
(the null UUID, as from a script on the server itself) are refused.

The clock: the player to move forfeits when `turnDeadline` passes (`result.reason` `"timeout"`).
A timeout is always a loss, whatever is left on the board (there is no exception for a winner
without mating material). A lone seat is freed after `waitTimeoutS` (an `expired` message). A
finished game resets to an empty table after 300 seconds. Every call applies a passed deadline
before anything else, so a move that arrives after `turnDeadline` is refused even before the next
clock tick. The entity script server cannot see avatars come and go, so these timeouts are how
the referee notices a player has gone.

### Notation

| Game | `move` text | Examples |
| --- | --- | --- |
| Connect Four | The column, exactly one digit `0`-`6` | `"3"` |
| Checkers | PDN square numbers 1-32 (1-4 along Black's home row, 29-32 along Red's): `a-b` for a step, `axb` for a jump, every landing square for a multi-jump. Exactly `^\d{1,2}(-\d{1,2}\|(x\d{1,2})+)$`, and only as `legal` lists it | `"11-15"`, `"22x15"`, `"22x15x6"` |

Checkers is English draughts (American checkers): Black (seat 0) moves first. Men move and
capture diagonally forward, kings both ways, one square at a time (no flying kings). Capturing is
compulsory, and a jump sequence continues while it can, though any sequence may be chosen (no
majority rule). A man reaching the far row is crowned and the move ends there.

### State

After every change the referee writes the state into the board's `userData` under `gameTable`,
keeping any other keys. Readers (`Entities.getEntityProperties(boardID, ["userData"])`, Morpheus
`inspect_entity`) see, for Connect Four:

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
        "drawOffer": null,
        "turnDeadline": 1760000180000,
        "channel": "gametable:{board-id}"
    }
}
```

and for checkers (after Black's `11-15`):

```json
{
    "gameTable": {
        "v": 1,
        "game": "checkers",
        "phase": "playing",
        "toMove": 1,
        "moveNo": 1,
        "rows": [".b.b.b.b", "b.b.b.b.", ".b.b...b", "....b...", "........", "r.r.r.r.", ".r.r.r.r", "r.r.r.r."],
        "legal": ["21-17", "22-17", "22-18", "23-18", "23-19", "24-19", "24-20"],
        "last": { "seat": 0, "move": "11-15" },
        "drawOffer": null,
        "...": "the other fields as above"
    }
}
```

`v` stays 1 for every game: readers switch on `game`.

| Field | Meaning |
| --- | --- |
| `game` | `"connect4"` or `"checkers"` |
| `phase` | `waiting` (seats filling; may still show the last game's board and result), `playing`, `over`, or `offline` (the referee script is not running) |
| `seats` | `null` when free. `session` is the player's session ID, which is the avatar ID other avatars see. Seat 0 is Red in Connect Four and Black in checkers; seat index and side index are the same |
| `toMove` | The seat to move while `playing`, else `null` |
| `rows` | The board, top row first. Connect Four: six strings of seven, `.` empty, `R`, `B`. Checkers: eight strings of eight, Black's home row first, `.` empty or light, `b` / `r` men, `B` / `R` kings |
| `legal` | While `playing`, else `[]`. Connect Four: the columns with room, as numbers. Checkers: the legal moves in notation, sorted as strings |
| `last` | The last move. Connect Four: `{ seat, col, row }`, the disc and the row it landed on. Checkers: `{ seat, move }` with the move in notation |
| `drawOffer` | The seat whose draw offer stands while `playing`, else `null` |
| `result` | `null`, or `{ "winner": 0 \| 1 \| null, "reason": ..., "line": [[row, col], ...] \| null }`; `line` is Connect Four's four winning cells (the first line found, if a move made two), else `null` |
| `turnDeadline` | Epoch milliseconds when the player to move forfeits, while `playing` |

`result.reason`:

| Reason | Game | Winner |
| --- | --- | --- |
| `four` | Connect Four | Four in a row |
| `draw` | Connect Four | `null`: the board is full |
| `no moves` | checkers | The side to move has no legal move (blocked, or no pieces left) and loses |
| `repetition` | checkers | `null`: the same position, with the same side to move, for the third time |
| `forty-move` | checkers | `null`: 80 plies without a capture or a man moving |
| `agreement` | any | `null`: a draw offer was accepted |
| `timeout` | any | The opponent of the player who ran out of time |
| `resign` | any | The opponent of the player who left |
| `abandoned` | any | `null`: the referee unloaded or died mid-game |

Checkers' repetition and forty-move draws are applied automatically, because an agent has no way
to claim one. Chess will add `checkmate`, `stalemate`, `repetition`, `fifty-move` and `material`.

The state is output only. The referee never reads it back as input (anyone who can edit the board
can change `userData`), and its next write repairs any change. The one exception is at load: a
`playing` phase there means the previous instance died mid-game.

### Messages channel

The referee sends JSON on `gametable:<board id>` (the `channel` field) and never listens on it.
Moves arrive only through the server methods, whose caller the engine authenticates; a Messages
sender ID is whatever the sending client wrote.

| `type` | When | Other fields |
| --- | --- | --- |
| `state` | After every change (a draw offer included), on `state`, and on a repeated `join` | The whole state above |
| `rejected` | A call was refused | `to` (the caller's session ID), `method`, `reason` |
| `over` | A game ended, or was abandoned by an unload or a crash (`result.reason` `"abandoned"`) | `gameId`, `result`, `seats` as they played |
| `expired` | A lone player's seat timed out | `seat`, `session`, `name` |

`reason` for `rejected` is one of: `no caller id`, `table full`, `seat taken`, `seat must be 0 or 1`,
`game not started`, `game over`, `game not over`, `not seated`, `not your turn`, `illegal move`
(well formed but not legal), the game's notation hint when the move does not parse
(`column must be one digit 0-6`, `move must be like 11-15 or 22x15x6`), and Connect Four's
`column full`.

### Playing as a human

1. Rez the board with both scripts set, as above: `serverScripts` (the referee) and `script` (the
   client). Within a few seconds the status display, the click targets or tiles, the two seat
   buttons and the pieces appear.
2. Click a seat button, "Play Red" or "Play Black" (or point a hand controller at it and pull the
   trigger). The referee seats you in that seat under your display name, unless someone already
   holds it. When both seats are taken the game starts.
3. On your turn an announcement says "Your turn".
   - Connect Four: the columns you can play light up green. Click the target above a column to
     drop a disc there.
   - Checkers: click one of your pieces (or its square). Its square lights up yellow and the
     squares it can move to green; click one to play the move. For a multi-jump, click the square
     the piece finally lands on. If two different jump paths end there, that square turns yellow
     too and the squares the paths go through next light up green: click the one you mean (again,
     until only one path is left). Clicking another of your pieces selects it instead; clicking
     anything else clears the selection.

Click your own seat button again to leave. In a game in progress leaving resigns, so the first
click only announces "Click your seat again within 5 s to resign the game", and a second click
within 5 seconds resigns. Anything the referee refuses ("not your turn", "column full", "illegal
move", "table full") comes back as an announcement, and so does an opponent's draw offer. There
is no rematch or draw button yet: for a rematch one player leaves and rejoins, which starts the
next game, and a human cannot offer or accept a draw (an agent's offer simply lapses when the
human moves). The table also resets on its own 300 seconds after a game ends.

The client is the same module on every entity that runs it, with a role taken from the entity's
name:

- A column target calls `Entities.callEntityServerMethod(board, "move", [String(column)])`.
- A seat button calls `join` with `MyAvatar.sessionDisplayName` and its own seat, or `leave` when
  the board's `userData` shows the user in that seat (confirmed by a second click during a game).
- The board's own instance subscribes to the channel named in `userData` (or `gametable:<board
  id>` if there is no state there yet). It calls `state` once if `userData` has no state, or one
  that looks stale (`offline`, or `playing` past its `turnDeadline`). It announces what concerns
  the user with `Window.displayAnnouncement`: whose turn it is, a draw offer, waiting for an
  opponent, the result, losing the seat, the referee going offline, and refusals addressed to the
  user's session ID. Spectators get no announcement for every move. While it is the user's turn
  it highlights where they can play: a green box around each legal column target, or the
  selection and its destinations on an 8x8 board. These highlights are local entities: the user
  sees them, nobody else does, and they need no edit rights. They ignore picks, so the click goes
  through to the target or tile. A target or tile that has not reached the client yet is looked
  for again every second; a highlight whose target is deleted moves to the one that replaces it.
- On an 8x8 board the board's own instance also takes the clicks on the tiles: it connects to the
  global `Entities.mousePressOnEntity` signal and keeps the presses on this board's `GT.Sq` and
  `GT.Pc` children (primary button only). That signal comes from a desktop mouse press
  (`EntityTreeRenderer::mousePressEvent`) and from a hand-controller laser trigger, which the
  pointer system emits as `PointerManager::triggerBeginEntity` and `EntityTreeRenderer` forwards to
  `mousePressOnEntity`. So one client instance serves the 64 tiles instead of 64 scripted tiles
  per visitor. (The `clickDownOnEntity` entity method the column targets and seat buttons use
  comes from the mouse path only, which is why those are also triggerable for the far-trigger
  controller module.)

The client keeps no game state and writes nothing to the board: the referee decides everything,
and the client only asks (its piece selection is local and dropped on every new turn). Channel
messages are self-asserted (any client can send one on the channel), so the client treats them as
cosmetic: they change only what the user is shown, never what is sent to the referee, which
checks every caller itself. There is no tap sound on a click. On unload the board's instance
unsubscribes, disconnects from `mousePressOnEntity` and deletes its highlights. Messages
subscriptions belong to the whole Interface session (one `MessagesClient`), so any other script of
yours listening on the same channel stops receiving it too.

If Interface's entity script allowlist is enabled, it must admit the client's URL.

### How an agent plays

An agent is a player like any other: its session ID is its seat. The referee neither knows nor
cares which players are AI. From an Agent assignment client script, with `chooseMove` the agent's
own (it picks one of `message.legal`, a column number in Connect Four and a notation string in
checkers). The channel name comes from the board's state. The same state can arrive more than
once (after `state`, a repeated `join`, a draw offer, or a refused call), so the agent plays each
turn once:

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
        Entities.callEntityServerMethod(board, "move", [String(chooseMove(message))]);
    }
});
Entities.callEntityServerMethod(board, "join", ["Morpheus"]);
Entities.callEntityServerMethod(board, "state", []);
```

An agent offers or accepts a draw with `draw` (`drawOffer` in the state shows a standing offer).

Agents cannot call server methods yet: the assignment client has no entity script client until
heluca/overte#30 is fixed. Morpheus will expose the same loop as tools (`call_server_method`,
`subscribe_channel`, and `recent_messages` with a long-poll for "wait for my turn"). Until then an
agent can read the game (`look()` for `GT.Status`, `inspect_entity` for `userData`) but not play.

### Adding a game

A rules module exports the contract `connect4.mjs` and `checkers.mjs` implement, and the core
plays it without looking at the board:

| Export | What it is |
| --- | --- |
| `GAME` | The name in `gameTableConfig.game` and the projection's `game` |
| `SEAT_NAMES` | The two sides' names; seat index is side index |
| `REMATCH` | `"alternate"` (same seats, the first move alternates) or `"swap"` (side 0 moves first, the players swap seats) |
| `MOVE_HINT` | The rejection reason for move text that does not parse |
| `newGame(firstSide = 0)` | A new state: plain JSON, never mutated |
| `sideToMove(state)` | `0` or `1` |
| `legalMoves(state)` | Sorted notation strings; `[]` once the game is over |
| `play(state, text)` | `{ state, move }` or `{ error }` (`MOVE_HINT`, or `"illegal move"`); `move` is `{ side, notation, san?, from?, to?, path?, captured, promoted? }` with cells as `[row, col]` |
| `outcome(state)` | `null`, or `{ winner, reason, line }` |
| `rows(state)` | The board as strings, top row first |
| `render(state)` | The board as monospace text with coordinates, for `GT.Status` |
| `extra(state)` | Fields merged into the projection (`{}` for none) |

Register it in `GAMES` in `game-registry.mjs`. An 8x8 game uses the `"grid"` layout and adds
`rowZeroSide` (the side whose home row is row 0 of `rows`), `cells` (notation to the squares a
move passes through, for the client) and `pieces` (each `rows` character's model and size in
square widths). Chess is meant to plug in this way, with `uciCells` already there for its
notation; until it is registered, `"chess"` falls back to Connect Four.

### Limits

- Humans have no draw or rematch button (above).
- A timeout is a loss even when the winner could not mate.
- Checkers kings are drawn as a taller checker, not a stacked pair of models.

### Development

Node (24 or later) runs the tests; it is a development tool, never a runtime dependency:

```sh
npm test            # node:test over test/*.test.mjs: the rules, the referee core, the layouts and the entries with fake globals
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
