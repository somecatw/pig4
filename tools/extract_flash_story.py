#!/usr/bin/env python3
"""Extract readable story text from the decompiled Flash source.

Two sources:
  * juqing/movie/*.as   -> this.t[frame] = "..." movie text arrays
                          (movie_text.json; KaiChangBai is the runtime opening)
  * juqing/_*.as        -> note.push(...) plot timelines -> assets/story/plots.json

plots.json parses only "show one line" steps: [speakerId, text] becomes a
`line`, plain strings become `narration`. Doors, mechanisms, portraits, choices
and other commands stay under `raw` as migration reference; the runtime ignores
raw and executes hand-authored commands from assets/story/plots_custom.json.
This tool never writes plots_custom.json. A human-readable dump is also written
to plot_text.txt.
Run from the pig4 repository root:

  python -I tools/extract_flash_story.py
"""

from __future__ import annotations

import argparse
import ast
import json
import re
from pathlib import Path

# `this.t[n]` keys are Flash frame labels, not necessarily playback order.
# The decompiled AS3 only contains the label-to-text map; the real order is on
# the FLA timeline. This is the verified KaiChangBai timeline order.
MOVIE_LABEL_ORDER = {
    "KaiChangBai": [
        10, 1, 2, 39, 3, 22, 23, 24, 29, 30, 31, 5, 6, 26, 27, 28, 7,
        8, 9, 11, 35, 36, 12, 13, 14, 38, 37, 15, 32, 33, 34, 16, 17, 21,
    ],
}

SPEAKER_FALLBACK = {
    0: "???",
    1: "丹",
    2: "丹（本尊）",
    3: "如心",
    4: "如意",
    5: "黑影",
    6: "布莉萝",
    7: "胥洋",
    8: "珍弓",
    9: "佩芝",
    10: "素素",
    11: "大玮",
    12: "小琪",
}


def load_speakers(flash: Path) -> dict[int, str]:
    plot = flash / "juqing" / "Plot.as"
    if not plot.exists():
        return dict(SPEAKER_FALLBACK)
    text = plot.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"name\s*:\s*Array\s*=\s*\[(.*?)\]\s*;", text, re.S)
    if not m:
        return dict(SPEAKER_FALLBACK)
    try:
        names = ast.literal_eval("[" + m.group(1) + "]")
    except Exception:
        return dict(SPEAKER_FALLBACK)
    return {i: str(name) for i, name in enumerate(names)}


def parse_speaker(text: str) -> tuple[str | None, str]:
    """Split a movie line like '丹：...' or '【旁白】' into speaker/text."""
    s = text.strip()
    if s.startswith("【") and s.endswith("】"):
        return None, s
    m = re.match(r"^([^：:]{1,10})[：:](.*)$", s, re.S)
    if m:
        return m.group(1).strip(), m.group(2).strip()
    return None, s


def extract_movies(flash: Path, out: Path, speakers: dict[int, str]) -> None:
    movie_dir = flash / "juqing" / "movie"
    results: dict[str, list[dict]] = {}
    for path in sorted(movie_dir.glob("*.as")):
        text = path.read_text(encoding="utf-8", errors="replace")
        entries = []
        for m in re.finditer(r"this\.t\[(\d+)\]\s*=\s*\"((?:[^\"\\]|\\.)*)\"\s*;", text):
            frame = int(m.group(1))
            try:
                value = json.loads('"' + m.group(2) + '"')
            except Exception:
                value = m.group(2)
            entries.append((frame, value))
        if not entries:
            continue
        by_frame = dict(entries)
        override = MOVIE_LABEL_ORDER.get(path.stem)
        if override:
            ordered = [(frame, by_frame[frame]) for frame in override if frame in by_frame]
            seen = {frame for frame, _ in ordered}
            ordered.extend(sorted((frame, value) for frame, value in entries
                                  if frame not in seen))
            entries = ordered
        else:
            entries.sort(key=lambda item: item[0])
        lines = []
        for frame, value in entries:
            if not value.strip():
                continue
            speaker, body = parse_speaker(value)
            lines.append({
                "frame": frame,
                "speaker": speaker,
                "text": body,
            })
        results[path.stem] = lines

    out.mkdir(parents=True, exist_ok=True)
    (out / "movie_text.json").write_text(
        json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    opening = results.get("KaiChangBai")
    if opening:
        (out / "opening.json").write_text(json.dumps({
            "id": "opening",
            "title": "开场白",
            "source": "flash_export/juqing/movie/KaiChangBai.as",
            "note": "Frame order follows the numeric this.t[frame] keys. "
                    "The original movie may use named frame labels; verify against the FLA.",
            "lines": opening,
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(f"movies: {len(results)} files, opening lines: {len(opening or [])}")


def step_from_push(value, speakers: dict[int, str]) -> dict:
    # 暂时只解析“显示一条剧情文本”：[说话人 id, 文本] 和纯文本旁白。
    # 其余门/机关/立绘/选项等指令原样保留，之后按自己的配置重写。
    if (isinstance(value, list) and len(value) >= 2
            and isinstance(value[0], int) and isinstance(value[1], str)):
        step = {
            "type": "line",
            "speakerId": value[0],
            "speaker": speakers.get(value[0], str(value[0])),
            "text": value[1],
        }
        if len(value) > 2:
            step["extra"] = value[2:]
        return step
    if isinstance(value, str) and value not in ("jump", "crossTheDoor", "changeTheDoor"):
        return {"type": "narration", "text": value}
    return {"type": "raw", "value": value}


def format_step(step: dict) -> str:
    kind = step["type"]
    if kind == "line":
        return f"[{step['speaker']}] {step['text']}"
    if kind == "narration":
        return step["text"]
    value = step.get("value", step)
    if isinstance(value, str):
        return value
    return json.dumps(value, ensure_ascii=False)


def extract_plots(flash: Path, out: Path, speakers: dict[int, str]) -> None:
    plot_dir = flash / "juqing"
    chunks: list[str] = []
    plots: list[dict] = []

    for path in sorted(plot_dir.glob("_*.as")):
        text = path.read_text(encoding="utf-8", errors="replace")
        index_match = re.search(r"this\.plot_index\s*=\s*(\d+)\s*;", text)
        plot_index = int(index_match.group(1)) if index_match else None

        modules: list[dict] = []
        module = "constructor"
        steps: list[dict] = []

        def emit(current_module: str, current_steps: list[dict]) -> None:
            if not current_steps:
                return
            modules.append({"name": current_module, "steps": current_steps})
            chunks.append(f"===== {path.name} :: {current_module} =====")
            chunks.append("\n".join(format_step(step) for step in current_steps))
            chunks.append("")

        for raw_line in text.splitlines():
            module_match = re.search(r"function\s+(module_\w+)\s*\(", raw_line)
            if module_match:
                emit(module, steps)
                steps = []
                module = module_match.group(1)
            # 只匹配裸 push(...)，不要匹配 Line.arr.push(...)。
            m = re.match(r"^\s*push\((.*)\);\s*$", raw_line)
            if not m:
                continue
            arg = m.group(1).strip()
            try:
                value = ast.literal_eval(arg)
            except Exception:
                # 含 Main.MC 之类运行时表达式的指令，原样保留源码。
                steps.append({"type": "raw", "value": arg})
                continue
            steps.append(step_from_push(value, speakers))
        emit(module, steps)

        if modules:
            plots.append({
                "id": path.stem,
                "file": path.name,
                "plotIndex": plot_index,
                "modules": modules,
            })

    out.mkdir(parents=True, exist_ok=True)
    (out / "plot_text.txt").write_text("\n".join(chunks) + "\n", encoding="utf-8")
    (out / "plots.json").write_text(json.dumps({
        "source": "flash_export/juqing",
        "speakers": {str(k): v for k, v in sorted(speakers.items())},
        "plots": plots,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    module_count = sum(len(plot["modules"]) for plot in plots)
    step_count = sum(len(module["steps"]) for plot in plots for module in plot["modules"])
    print(f"plots: {len(plots)} files, {module_count} modules, {step_count} steps")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--flash", type=Path, default=Path("flash_export"))
    ap.add_argument("--out", type=Path, default=Path("assets/story"))
    args = ap.parse_args()
    speakers = load_speakers(args.flash)
    extract_movies(args.flash, args.out, speakers)
    extract_plots(args.flash, args.out, speakers)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
