# Puzzle views

Each subdirectory is one self-contained puzzle interface: implementation,
programmatic art, a reference image and this event table. A view must not
include `puzzle.h` or call puzzle logic directly.

## Contract

- Outbound: `PuzzleView::viewEvent(PuzzleViewEvent)` describes what the user did
  (`key`, `take`, `drop`, ...).
- Inbound: `PuzzleView::applyEvent(PuzzleViewEvent)` receives display state or
  feedback from the puzzle (`setInput`, `setState`, `feedback`, ...).
- `appearanceChanged(QImage)` and `closeRequested()` stay in the base class.
- Persistent state belongs to `Puzzle`. A view may keep only transient UI state
  (hover, animations, current feedback text) and must be rebuildable from an
  initial state event.
- Event `type` names and payload keys are constants in the view header and are
  listed in the view's own `README.md`.

## Current views

- `password/` — reusable letter/password keypad.
- `elevator/` — floor-selection panel and call-button art.

Simple views that do not need their own art/model directory may stay in
`game/puzzle/puzzleui.cpp`; `KeypadPuzzleView` and `ShardPinkPuzzleView` are
there for now, but both use the same event contract.
