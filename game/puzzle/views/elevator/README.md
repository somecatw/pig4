# ElevatorPanelView

Floor-selection panel. Source is in `elevatorpanelview.*`; the panel and
call-button images are generated in `elevatorart.*` and shared with the world
sprites.

## Event table

Outbound (view → puzzle):

```text
floor     { floorId: QString }
openDoor  { }
```

Inbound (puzzle → view):

```text
setPanel  { floors: QStringList, currentFloor: QString }
feedback  { text: QString, durationMs: int }
```

The view only knows the six physical floor buttons and which floors are
reachable. Teleport mapping, elevator doors and travel state remain in
`ElevatorPanelPuzzle` / `MainWindow`.
