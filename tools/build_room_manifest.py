#!/usr/bin/env python3
"""
Extract PH_* placeholders from a room OBJ and/or a separate markers OBJ into
a room manifest JSON for pig4.

Naming:
  PH_<TYPE>_<id>          e.g. PH_SPR_phone, PH_HOT_phone
  PH_DOOR_<style>_L|_R    e.g. PH_DOOR_9_L
  PH_WIN_<style>_L|_R     e.g. PH_WIN_1_L
  Room prefix is optional: PH_DOOR_301_9L is parsed as style 9.
  If the style is missing/unknown, the style table's "default" is used.

Placeholder geometry:
  * SPR: a plane; center/size/orientation are taken from the quad.
  * DOOR/WIN: a thin box whose closed leaf is the marker; _L/_R picks the hinge edge.
  * HOT/TRIG: a box; AABB is used.

Output:
  <room_stem>.markers.json next to the room OBJ.

Styles:
  <building>/styles.json maps door/window style ids to global assets:
    {"doors":   {"9":       {"scene": "shared/props/door_9_engine.obj",   "openAngle": 1.4}},
     "windows": {"default": {"scene": "shared/props/window_1_engine.obj", "openAngle": 1.4}}}
  Default path is the styles.json next to the room directory's parent
  (assets/hotel/styles.json for assets/hotel/room_301/).

Asset lookup:
  DOOR/WIN: styles.json style entry, then legacy props/<id>_engine.obj fallback
  SPR:      assets/images/<id>.png first (shared with inventory icon),
            then sprites/<id>.png / <id>.png / textures/<id>.png
  Manifest asset paths are written relative to the room directory.

Usage:
  python -I tools/build_room_manifest.py assets/hotel/room_301/room_301.obj
  python -I tools/build_room_manifest.py assets/hotel/room_301/room_301.obj --markers assets/hotel/room_301/dev/room_301_markers.obj
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

import numpy as np


def parse_obj(path: Path):
    verts = []
    objects = []          # [{"name": str, "ids": set[int]}]
    by_name = {}
    cur = None
    for line in path.read_text(errors="replace").splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        a = s.split()
        if a[0] == "v" and len(a) >= 4:
            verts.append((float(a[1]), float(a[2]), float(a[3])))
        elif a[0] in ("o", "g") and len(a) > 1:
            # Max 会同时导出 "o Name" 和 "g Name"，要合并成同一个对象
            cur = by_name.get(a[1])
            if cur is None:
                cur = {"name": a[1], "ids": set()}
                by_name[a[1]] = cur
                objects.append(cur)
        elif a[0] == "f" and cur is not None:
            for tok in a[1:]:
                cur["ids"].add(int(tok.split("/")[0]) - 1)
    return verts, objects


def safe_name(name: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_.-]+", "_", name.strip())
    return s or "marker"


def marker_basis(points: np.ndarray):
    center = points.mean(axis=0)
    q = points - center
    if len(points) < 3:
        return center, np.array([1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0])
    cov = np.cov(q.T) + np.eye(3) * 1e-9
    vals, vecs = np.linalg.eigh(cov)   # ascending
    normal = vecs[:, 0]
    plane_axes = [vecs[:, 1], vecs[:, 2]]

    # height: in-plane axis closest to world up (room OBJ is Y-up)
    up = np.array([0.0, 1.0, 0.0])
    height = plane_axes[0] if abs(plane_axes[0].dot(up)) > abs(plane_axes[1].dot(up)) else plane_axes[1]
    width = plane_axes[1] if np.allclose(height, plane_axes[0]) else plane_axes[0]

    if height.dot(up) < 0:
        height = -height
    # stable width direction: positive dominant horizontal component
    if abs(width[0]) >= abs(width[2]):
        if width[0] < 0:
            width = -width
    else:
        if width[2] < 0:
            width = -width
    normal = np.cross(width, height)
    if np.linalg.norm(normal) < 1e-6:
        normal = np.array([0.0, 0.0, 1.0])
    normal /= np.linalg.norm(normal)
    width /= np.linalg.norm(width)
    height /= np.linalg.norm(height)
    return center, width, height, normal


def extent(points: np.ndarray, center: np.ndarray, axis: np.ndarray) -> float:
    d = (points - center) @ axis
    return float(d.max() - d.min())


def find_asset(root: Path, *names: str) -> str | None:
    for n in names:
        p = root / n
        if p.exists():
            return p.relative_to(root).as_posix()
    return None


def find_single(root: Path, *patterns: str) -> str | None:
    """Fallback for PH names that don't encode the asset name (e.g. PH_WIN_301_L).

    Prefer a .obj family with exactly one match; *_engine.obj first.
    """
    for pattern in patterns:
        matches = sorted(p for p in root.glob(pattern) if p.is_file())
        if not matches:
            continue
        engine = [p for p in matches if p.stem.endswith("_engine")]
        if len(engine) == 1:
            return engine[0].relative_to(root).as_posix()
        if len(matches) == 1:
            return matches[0].relative_to(root).as_posix()
    return None


def load_styles(path: Path) -> dict:
    if not path.exists():
        print(f"warning: styles file not found: {path}", file=sys.stderr)
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except Exception as e:
        print(f"warning: styles parse failed: {path}: {e}", file=sys.stderr)
        return {}


def has_placeholders(path: Path) -> bool:
    for line in path.read_text(errors="replace").splitlines():
        s = line.strip()
        if s.startswith(("o PH_", "g PH_")):
            return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="Build pig4 room manifest from PH_* placeholders.")
    ap.add_argument("room", type=Path, help="room OBJ (PH_* objects may live here)")
    ap.add_argument("--markers", type=Path, default=None,
                    help="separate markers OBJ; default <room>_markers.obj if it exists")
    ap.add_argument("--out", type=Path, default=None,
                    help="manifest JSON; default <room_stem>.markers.json "
                         "(_engine suffix stripped)")
    ap.add_argument("--styles", type=Path, default=None,
                    help="global style table; default <building>/styles.json")
    args = ap.parse_args()

    room = args.room.resolve()
    if not room.exists():
        print(f"error: room OBJ not found: {room}", file=sys.stderr)
        return 2
    root = room.parent
    building_root = root.parent
    styles_path = args.styles or (building_root / "styles.json")
    styles = load_styles(styles_path)

    # room_301_engine.obj 和 room_301.obj 共享同一份 manifest
    base_stem = room.stem
    if base_stem.endswith("_engine"):
        base_stem = base_stem[:-len("_engine")]
    mnum = re.search(r"(\d+)$", base_stem)
    room_number = mnum.group(1) if mnum else ""

    marker_files = [room]
    marker_path = args.markers
    if marker_path is None:
        if has_placeholders(room):
            print(f"note: {room.name} already contains PH_* markers; "
                  f"skip auto markers OBJ (use --markers to add one)")
        else:
            for stem in dict.fromkeys((room.stem, base_stem)):
                auto = room.with_name(stem + "_markers" + room.suffix)
                if auto.exists():
                    marker_path = auto
                    break
    if marker_path is not None:
        marker_path = marker_path.resolve()
        if not marker_path.exists():
            print(f"error: markers OBJ not found: {marker_path}", file=sys.stderr)
            return 2
        if marker_path != room:
            marker_files.append(marker_path)

    markers = []
    seen = set()

    for path in marker_files:
        verts, objects = parse_obj(path)
        for obj in objects:
            name = obj["name"]
            if not name.startswith("PH_"):
                continue
            if name in seen:
                continue
            seen.add(name)
            rest = name[3:]
            if "_" not in rest:
                print(f"warning: marker without type: {name}", file=sys.stderr)
                continue
            typ, tail = rest.split("_", 1)
            typ = typ.upper()
            side = ""
            mid = tail
            # 允许 PH_DOOR_301_9_L，也允许 Max 里常见的 PH_DOOR_301_9L。
            # L/R 前面必须是数字或下划线，避免 PH_DOOR_3F_elevator 结尾的
            # "r" 被误当成铰链方向。
            if (typ in ("DOOR", "WIN") and len(tail) >= 2
                    and tail[-1].upper() in ("L", "R")
                    and (tail[-2].isdigit() or tail[-2] in "_-")):
                side = tail[-1].upper()
                mid = tail[:-1].rstrip("_-")
            mid = safe_name(mid)
            # 去掉房间号前缀：PH_DOOR_301_9L -> id 9
            if room_number and mid.startswith(room_number + "_"):
                mid = mid[len(room_number) + 1:]
            if room_number and mid == room_number:
                mid = ""

            # 门窗的样式 key 是去掉房间号和旧式资源名前缀后的剩余部分
            style = mid
            for prefix in ("door_", "Door_", "window_", "Window_"):
                if style.startswith(prefix):
                    style = style[len(prefix):]
                    break

            ids = sorted(obj["ids"])
            if len(ids) < 3:
                print(f"warning: marker {name} has no usable geometry", file=sys.stderr)
                continue
            pts = np.array([verts[i] for i in ids], dtype=np.float64)
            center, width, height, normal = marker_basis(pts)
            w = extent(pts, center, width)
            h = extent(pts, center, height)
            thickness = extent(pts, center, normal)
            if w <= 1e-5 or h <= 1e-5:
                print(f"warning: marker {name} is degenerate", file=sys.stderr)
                continue

            marker = {
                "name": name,
                "type": typ,
                "id": mid,
                "position": [round(float(x), 6) for x in center],
                "basisX": [round(float(x), 6) for x in width],
                "basisY": [round(float(x), 6) for x in height],
                "basisZ": [round(float(x), 6) for x in normal],
                "size": [round(w, 6), round(h, 6)],
                "thickness": round(thickness, 6),
                "side": side,
                "openAngle": 1.4 if typ in ("DOOR", "WIN") else 0.0,
                "style": style,
            }

            if typ == "SPR":
                # 道具图统一放在 assets/images/<id>.png；场景和背包共用同一张图。
                shared = building_root.parent / "images" / f"{mid}.png"
                if shared.exists():
                    tex = os.path.relpath(shared.resolve(), root).replace(os.sep, "/")
                else:
                    tex = find_asset(root, f"sprites/{mid}.png", f"{mid}.png",
                                     f"textures/{mid}.png")
                marker["texture"] = tex or ""
                marker["billboard"] = False
            elif typ in ("DOOR", "WIN"):
                # 先按 styles.json 的样式表找全局资产
                kind = "doors" if typ == "DOOR" else "windows"
                style_table = styles.get(kind, {}) if isinstance(styles, dict) else {}
                if not isinstance(style_table, dict):
                    style_table = {}
                resolved_style = style if style in style_table else "default"
                entry = style_table.get(resolved_style)
                asset = None
                if isinstance(entry, dict):
                    scene = entry.get("scene", "")
                    if scene:
                        scene_abs = (building_root / scene).resolve()
                        if scene_abs.exists():
                            asset = os.path.relpath(scene_abs, root).replace(os.sep, "/")
                            marker["style"] = resolved_style
                            if "openAngle" in entry:
                                marker["openAngle"] = float(entry["openAngle"])
                        else:
                            print(f"warning: style {typ}/{resolved_style} missing: {scene_abs}",
                                  file=sys.stderr)

                # 兼容旧布局：房间自己的 props/ 或房间根目录
                if asset is None:
                    prefixes = ("props/", "")
                    names = []
                    if mid:
                        for prefix in prefixes:
                            if typ == "DOOR":
                                names += [f"{prefix}{mid}_engine.obj", f"{prefix}{mid}.obj",
                                          f"{prefix}door_{mid}_engine.obj", f"{prefix}door_{mid}.obj",
                                          f"{prefix}Door_{mid}_engine.obj", f"{prefix}Door_{mid}.obj"]
                            else:
                                names += [f"{prefix}{mid}_engine.obj", f"{prefix}{mid}.obj",
                                          f"{prefix}window_{mid}_engine.obj", f"{prefix}window_{mid}.obj",
                                          f"{prefix}Window_{mid}_engine.obj", f"{prefix}Window_{mid}.obj"]
                    asset = find_asset(root, *names)
                    if asset is None:
                        if typ == "DOOR":
                            asset = find_single(root, "props/door_*_engine.obj",
                                                "door_*_engine.obj", "props/Door_*_engine.obj",
                                                "Door_*_engine.obj", "props/door_*.obj", "door_*.obj",
                                                "props/Door_*.obj", "Door_*.obj")
                        else:
                            asset = find_single(root, "props/window_*_engine.obj",
                                                "window_*_engine.obj", "props/Window_*_engine.obj",
                                                "Window_*_engine.obj", "props/window_*.obj", "window_*.obj",
                                                "props/Window_*.obj", "Window_*.obj")
                marker["asset"] = asset or ""
                # PH_WIN_301_L 这类名字没有资源编号，用资源名补一个 id
                if not mid and asset:
                    stem = Path(asset).stem
                    if stem.endswith("_engine"):
                        stem = stem[:-len("_engine")]
                    marker["id"] = safe_name(stem)
            elif typ in ("HOT", "TRIG"):
                marker["halfSize"] = [round(w / 2.0, 6), round(h / 2.0, 6), round(thickness / 2.0, 6)]

            markers.append(marker)

    out = args.out or room.with_name(base_stem + ".markers.json")
    manifest = {
        "room": base_stem,
        "markers": markers,
    }
    out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(markers)} markers)")
    for m in markers:
        print(f"  {m['type']:5s} {m['id']:24s} pos={m['position']} size={m['size']} "
              f"side={m['side'] or '-'} asset={m.get('asset','')} texture={m.get('texture','')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
