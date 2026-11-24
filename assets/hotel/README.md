# Hotel assets

Each room is self-contained for baked geometry, but door/window models are global
styles shared by all rooms:

```
building.json                   # room grid cells, portals, building transform
styles.json                     # PH_DOOR_<style> / PH_WIN_<style> -> shared asset
dev/                            # cross-room screenshots / temporary test assets
shared/props/
  door_9.obj                    # canonical source: X width, Y height, Z thickness
  door_9_engine.obj/.mtl
  Door_9_*.png
  window_1.obj
  window_1_engine.obj/.mtl
  Window_1_*.png

room_301/
  room_301.obj                  # 3ds Max source, contains PH_* markers
  room_301_engine.obj/.mtl      # pig4 runtime room
  *_VRayCompleteMap.png         # baked room textures
  room_301.markers.json         # generated from PH_* markers
  sprites/<id>.png              # alpha-tested 2D sprites
  dev/                          # screenshots, old tests, unused bake iterations
```

## Building placement

`building.json` places rooms on a grid:

```json
{
  "scale": 28.0,
  "flipX": true,
  "floorOffsetY": -2.3328,
  "anchorRoom": "room_301",
  "rooms": [
    { "id": "room_301", "cell": [0, 0, 0], "yaw": 0.0,
      "scene": "room_301/room_301_engine.obj",
      "markers": "room_301/room_301.markers.json" },
    { "id": "room_303", "cell": [1, 0, 0], "yaw": 0.0,
      "scene": "room_303/room_303_engine.obj",
      "markers": "room_303/room_303.markers.json" }
  ]
}
```

`cell = [col, row, floor]` maps to X / Z / Y. An optional `label` overrides the
room-name label for non-room spaces such as the third-floor west corridor.
Do **not** write per-room translations: RoomManager computes each room's
non-PH AABB and normalizes its min/floor corner to the cell. The cell size is the per-axis maximum room AABB.
The anchor room keeps its original world position; every other room is placed
relative to it. `PH_*` geometry is skipped by the loader and therefore never
contributes to the placement AABB. Irregular rooms (the pool) will get an
explicit override later.

Current 1F west wing: `room_1wcorridor` is cell `[2, 0, -2]`, and
`room_lobby` is cell `[2, -1, -2]` (the lobby sits on the corridor's
front/connecting side; its north wall opening lines up with the existing
`1W_lobby` door).

Current 5F west wing: `room_5wcorridor` is cell `[2, 0, 2]`,
`room_billard` is cell `[2, -1, 2]`, and `room_lifesong` is cell `[1, 0, 2]`.
The lifesong room's east-wall opening lines up with the corridor's
`5W_lifesong` door; its own `lifesong_manager` marker is an interior door.

## Portals

Portals are only for transitions that cannot be walked normally, such as
climbing through a window. Ordinary room doors are normal collision openings:
once unlocked/opened, the player walks through with WASD, so they do **not**
need a portal entry.

A portal links a marker in one room to another room. It is directional:

```json
{
  "id": "bath_window_301_303",
  "from": "room_301",
  "marker": "window_1",
  "to": "room_303",
  "kind": "window",
  "ends": {
    "room_301": { "pos": [30.0, 17.7, -27.2], "lookAt": [36.0, 14.0, -27.2] },
    "room_303": { "pos": [55.0, 17.7, -27.2], "lookAt": [85.0, 14.0, -27.2] }
  }
}
```

`ends.<room>.pos` / `lookAt` use that **room's own OBJ source coordinates**
(before the building scale is applied), so moving a room on the grid does not
require editing portal data. The same window can be climbed from either side:
the camera path is `current camera -> near-side end -> far-side end`. The
legacy single-sided `enter` object is still parsed as the `to`-side end.

## Locks and connections

Locks live in `building.json` because model re-exports regenerate the per-room
marker JSON. `type: "key"` uses the currently selected inventory item (not just
ownership): aiming at the door with the right key selected shows
`[F] 用 <key> 开锁`, and F only unlocks. A second F opens the door/window.
`type: "event"` starts locked and waits for an event connection.

A door can have multiple unlock methods. Keep the existing key lock and add
puzzle/story event connections as additional paths; the first successful path
unlocks the same door state. Do not replace a valid key lock just because a
password or story event can also unlock the door.

An optional `directRoom` marks a lock that is also a deadbolt on one side.
When the player's current room matches `directRoom`, the locked prompt shows the
normal key line plus `从这一侧反锁着，可以直接解锁`, and F unlocks it without the
key (showing `门锁解除`); a second F opens the door. From any other room only the
key line is shown, and the key is still required. Unlock and open are separate
actions for every door.

```json
"locks": [
  { "room": "room_301", "marker": "9", "type": "key", "id": "key_8", "label": "8号钥匙" },
  { "room": "room_303", "marker": "9", "type": "event", "label": "机关" },
  { "room": "room_1wcorridor", "marker": "1W_lobby", "type": "key",
    "id": "key_1", "label": "1号钥匙", "directRoom": "room_1wcorridor" }
],
"connections": [
  { "event": "puzzle_303.solved", "room": "room_303", "marker": "9", "action": "unlock" },
  { "event": "puzzle_303.solved", "room": "room_303", "marker": "9", "action": "open" }
]
```

Actions are `unlock`, `lock`, `open`, `close`, `toggle`. They are registered on
the game's `GameEvents` bus at load time, so a door can be unlocked/locked by
any later story event without adding puzzle-specific code. Actions run in JSON
order; use `unlock` before `open` on a locked door.

## Door interaction states

Door locks are not the whole story. A door can enter a story-controlled
interaction state (0 = locked, 1 = unlocked, 2+ = configured behavior) without
changing its physical open/closed state. Add a `doorStates` entry when a state
needs its own prompt/action:

```json
"doorStates": [
  {
    "room": "room_5wcorridor",
    "marker": "5W_billard",
    "state": 2,
    "prompt": "[F] 开门",
    "notice": "危险！不要把门打开……",
    "action": "none"
  }
]
```

`action` may be `none`, `unlock`, `lock`, `open`, `close` or `toggle`. With
`none`, F displays `notice` and leaves the door unchanged. While a door is in a
custom state, generic `connections` actions are ignored; use the story command
`setDoorState room marker state` to enter or leave it. The physical lock still
exists underneath, so scripts normally lock the door first and then set state 2.

## Story triggers

`storyTriggers` maps a transient `GameEvents` event to a story. Generic pickups
publish `pickup.<itemId>`; the story runtime also publishes
`story.<plotId>:<module>.started` and `.finished` for optional chaining.

```json
"storyTriggers": [
  { "event": "pickup.key_3", "story": "_5_MeetShadow:module_1", "once": true }
]
```

`once` defaults to true. The story module itself is hand-authored in
`assets/story/plots_custom.json`; commands such as `teleport`, `giveItem`,
`lockDoor` and `setDoorState` belong there rather than in the door connection
table. Unknown story commands are passed, so a module can be wired before every
command has a handler.

## Interactable props (INTERACT_ / HOT)

3D props that stay part of the room model (clocks, fake doors, etc.) should be
**separate object groups named `INTERACT_<id>`**. The loader keeps them as normal
room geometry, registers `<id>` as an interact target, raycasts the actual mesh,
and tints that mesh with the hover highlight. Multi-material submeshes with the
same object name are merged into the same target.

```json
"interactions": [
  { "room": "room_1lobby", "marker": "clock_1",
    "prompt": "查看 时钟", "notice": "时钟停在三点十五分。" },
  { "room": "room_1lobby", "marker": "fake_door",
    "prompt": "查看 门", "notice": "这扇门打不开，像是装饰。" }
]
```

`prompt` is shown after the automatic `[F] ` prefix; pressing F shows `notice` in
the bottom bar. The interaction is repeatable. A fake door is just
`INTERACT_fake_door` geometry, **not** a `PH_DOOR_*` marker, so it gets no door
state or collider. Keep each prop in its own `o INTERACT_<name>` group; do not
merge it into the wall mesh, or the wall will highlight too.

`PH_HOT_<id>` still works as an invisible OBB trigger for props that cannot be
split into their own object, but it does not tint the prop: place the box so it
sticks out toward the room, otherwise static geometry is hit first. The
`INTERACT_` path uses a small surface tolerance instead because the prop's own
mesh is in the static BVH.

## Procedural clocks

Wall clocks that need to show different puzzle times can stay as `PH_SPR_*`
markers without PNGs. Add a `clocks` entry in `building.json`:

```json
"clocks": [
  { "room": "room_lobby", "marker": "lobby_C1", "hour": 12, "minute": 0 },
  { "room": "room_lobby", "marker": "lobby_C2", "hour": 3, "minute": 0 }
]
```

`hour` accepts 0–23 (or 1–12), `minute` 0–59. `game/clockart.cpp` draws a
256×256 clock face at load time from these parameters, so changing the time
does not require a texture re-export. Optional `prompt` / `notice` override the
defaults `查看 时钟` and `时钟显示 HH:MM。`; the clock is a normal interact
target and `F` shows the notice. The current 12:00 / 3:00 / 6:00 / 9:00 values
in `building.json` are placeholders for the lobby puzzle.

## Text sprites and pickups

`textSprites` draws a text card for a `PH_SPR_*` marker that has no exported
PNG. `rotate` rotates the final card (useful for table markers whose basis axes
are swapped); text wraps anywhere so long strings fit.

```json
"textSprites": [
  { "room": "room_lobby", "marker": "lobby_timeclue",
    "text": "ABC...VWXABC...VWX=day*2", "font": "Consolas",
    "pixelSize": 44, "width": 512, "height": 512, "rotate": 90 }
]
```

Pure text sprites are **not** interact targets by default. Set
`"interactive": true` plus `prompt` / `notice` if the player should be able to
aim at them.

`pickups` binds a marker to an inventory item. `icon` can be `key:1` for a
numbered key or a filename under `assets/images/`:

```json
"pickups": [
  { "room": "room_lobby", "marker": "item_key1",
    "item": "key_1", "name": "1号钥匙", "icon": "key:1" }
]
```

The pickup prompt is automatic (`[F] 拾取 <name>`); F adds the item and removes
the world sprite. The item's `assets/items.json` entry may omit `views` and
will use the inventory icon as its detail view.

## Puzzles

Puzzles are attached to existing `SPR` targets by `room + marker`. The scene
sprite is unchanged; `interactive: false` marks a clue sprite that highlights
but does not open a view.

Puzzle-target sprites are placed against the wall using a short ray from the
marker in the outward direction: the sprite snaps to the first wall surface and
is lifted by a very small offset (1.4 world units at the default scale) to
avoid z-fighting. Other sprites (items, clocks, text cards, paintings) keep the
old marker-thickness offset.

```json
"puzzles": [
  {
    "room": "room_303",
    "marker": "puzzle_303",
    "kind": "keypad",
    "interactive": true,
    "solution": "9132",
    "event": "puzzle_303.solved",
    "solvedText": "密码正确，门锁解除"
  }
]
```

`kind` selects the runtime subclass (`keypad` → `KeypadPuzzle`). The completed
puzzle publishes `event`; the existing locks/connections table unlocks the
door. The current solution is `9132`, revealed on the back of the moon block.

For a reusable letter password pad, use `letter_keypad` (alias
`password_keypad`). The wood panel and key faces are generated; no PNG is
required. `keys` are row-major, `columns` sets keys per row, and the number of
characters in `solution` is the password length. `clearKey`, `backspaceKey`
and `submitKey` are optional action labels that must also appear in `keys`;
without `submitKey`, the pad validates automatically when the solution length
is reached.

```json
{
  "room": "room_5wcorridor",
  "marker": "5W_puzzle1",
  "kind": "letter_keypad",
  "interactive": true,
  "solution": "retaw",
  "keys": ["a", "b", "c", "d", "e", "f", "r", "s", "t", "w", "z", "C"],
  "columns": 3,
  "clearKey": "C",
  "event": "5W_puzzle1.solved",
  "errorText": "没有反应……"
}
```

The 5F west corridor password comes from the paper clue:
`18 05 20 01 23` → `RETAW`. It is entered as `RETAW`, not reversed to `WATER`.
The green `C` key clears the input; the display shows one green mark per
entered character, not the letters themselves. Action labels are matched
exactly, so the normal `c` key and the clear key `C` can coexist on the same
pad. Solving it unlocks the adjacent billiard-room door `5W_billard`, but does
not open it; the player still opens the door explicitly with F.

Elevators use the panel itself as the teleport anchor. There is no separate
trigger volume: selecting a floor finds `elevator_panel_<floor>` in the target
room and maps the camera's transform relative to the current panel onto it.

```json
{
  "room": "room_3wcorridor",
  "marker": "elevator_panel_3F",
  "kind": "elevator_panel",
  "floors": ["1F", "2F", "3F", "4F", "5F", "6F"]
}
```

Only floors whose rooms and `elevator_panel_<floor>` markers exist are
teleportable; the panel reports a missing floor otherwise.

`PH_DOOR_*elevator*` currently uses a generated gray placeholder leaf and
slides along its marker `basisX` by one door width (west for the current 3F
corridor). It does not need a `styles.json` entry yet.

Item inspection views and combination recipes live in `assets/items.json`
(`views` with image + caption, and `combines` with `with` / `result` / `notice`).

The 2D view is the single source of appearance. `PuzzleView::syncAppearance()`
renders its `appearanceWidget()` to a `QImage`; `Puzzle::setWorldTexture()`
applies that image to the in-world sprite material. Call `syncAppearance()`
again whenever the 2D puzzle art changes so the 3D sprite stays in sync.

## Item icons

`assets/images/<id>.png` is the canonical item image. `build_room_manifest.py`
prefers it for `PH_SPR_<id>` and writes a relative path such as
`../../images/silver_key.png`; the inventory loads the same file, so the world
sprite and inventory icon cannot drift apart. Room-local `sprites/<id>.png`
remains a fallback for non-item decorations.

The sprite quad is fitted to the image aspect ratio, so portrait icons (for
example the silver key) are not stretched by a marker box.

## Marker naming

```
PH_DOOR_<style>_L|R             # e.g. PH_DOOR_9_L
PH_WIN_<style>_L|R              # e.g. PH_WIN_1_L
PH_SPR_<id>                     # alpha-tested sprite, texture sprites/<id>.png
PH_HOT_<id>                     # OBB hotspot; same id as a sprite merges targets
```

A room-number prefix is tolerated for older exports: `PH_DOOR_301_9L` is parsed as
style `9`, and `PH_WIN_301_L` (no explicit style) uses the style table's
`windows.default`. Unknown door styles also fall back to `doors.default`.

`styles.json`:

```json
{
  "doors": {
    "default": { "scene": "shared/props/door_9_engine.obj", "openAngle": 1.4 },
    "9":       { "scene": "shared/props/door_9_engine.obj", "openAngle": 1.4 }
  },
  "windows": {
    "default": { "scene": "shared/props/window_1_engine.obj", "openAngle": 1.4 },
    "1":       { "scene": "shared/props/window_1_engine.obj", "openAngle": 1.4 }
  }
}
```

## Pipeline

Run from the repository root.

Room:

```powershell
python -I tools/merge_vray_rtx.py assets/hotel/room_301/room_301.obj
python -I tools/build_room_manifest.py assets/hotel/room_301/room_301.obj
```

Global door/window styles (only when the model or bake changes):

```powershell
python -I tools/merge_vray_rtx.py assets/hotel/shared/props/door_9.obj --size 1024
python -I tools/merge_vray_rtx.py assets/hotel/shared/props/window_1.obj --size 512
```

Clean up V-Ray intermediate passes (DiffuseFilter / RawLighting / RawTotalLighting
/ AO). The tool only keeps final Complete/Merged textures and never removes a
texture referenced by an MTL:

```powershell
# preview
python -I tools/merge_vray_rtx.py --prune-only --prune-root assets/hotel --dry-run
# apply; use --trash-dir for a recoverable move
python -I tools/merge_vray_rtx.py --prune-only --prune-root assets/hotel
```

Adding `--prune` to a room merge command performs the same cleanup after the
new engine MTL is written.

The manifest builder resolves the style table and writes asset paths relative to
the room directory (`../shared/props/...`). Sprite textures stay under
`sprites/`. `PH_WIN_*` markers are always treated as openable windows.
