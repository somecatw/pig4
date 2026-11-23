# PasswordKeypadView

Reusable letter/password keypad. Source is in `passwordkeypadview.*`; the
programmatic wood panel and key layout live in `passwordart.*`.

Reference: `reference.png` (original Flash game, 5F west corridor).

## Event table

Outbound (view → puzzle):

```text
key   { label: QString }
```

Inbound (puzzle → view):

```text
setInput  { text: QString }
feedback  { text: QString, durationMs: int }
```

`closeRequested` and `appearanceChanged` are inherited from `PuzzleView`.

The view only maps clicks/keyboard input to key labels. Password matching,
clear/backspace/submit key semantics and the `RETAW` solution live in
`PasswordKeypadPuzzle`; the view does not know the password or whether it is
correct.

Keys are matched exactly for action labels, so the normal lowercase `c` key and
the green clear key `C` can coexist. The display shows one green mark per
entered character instead of the letters themselves.
