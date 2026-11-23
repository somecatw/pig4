#!/usr/bin/env python3
"""
Build pig4 room assets from a 3ds Max / V-Ray RTX bake.

Default mode (--prefer complete):
  * use <object>_VRayCompleteMap.png directly when it exists
  * otherwise merge <object>_VRayDiffuseFilterMap.png and
    <object>_VRayRawTotalLightingMap.png
  * otherwise create a checker fallback

It writes an MTL that binds every object to its texture and a derived OBJ
with `mtllib` / `usemtl`, while leaving the source OBJ untouched.

The engine has one texture per material; CompleteMap is used as-is, no
resizing unless --size is given. `--size 512` etc. forces square power-of-two
output and can be used to cap merged 2K diffuse/lighting maps.

Usage:
  python -I tools/merge_vray_rtx.py assets/hotel/room_301/room_301.obj
  python -I tools/merge_vray_rtx.py assets/hotel/room_301/room_301.obj --size 512
  python -I tools/merge_vray_rtx.py assets/hotel/shared/props/door_9.obj --size 1024
  python -I tools/merge_vray_rtx.py assets/hotel/shared/props/window_1.obj --size 512
  python -I tools/merge_vray_rtx.py assets/hotel/room_301/room_301.obj --prefer separate --size 1024
  python -I tools/merge_vray_rtx.py room.obj --lighting-space srgb
  python -I tools/merge_vray_rtx.py --prune-only --prune-root assets/hotel --dry-run
  python -I tools/merge_vray_rtx.py --prune-only --prune-root assets/hotel

Default output is <obj>_engine.obj plus a matching <obj>_engine.mtl; the source
OBJ/MTL are left untouched.

`--prune-only` removes V-Ray intermediate passes (DiffuseFilter, RawLighting,
RawTotalLighting, AO) after the final Complete/Merged texture has been written;
it never deletes a texture referenced by an MTL. `--prune` does the same after a
normal room build. `--trash-dir` moves removed files there instead of deleting
them, which makes cleanup reversible.

Requires Pillow and numpy.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

DIFFUSE_SUFFIX = "_VRayDiffuseFilterMap"
LIGHTING_SUFFIX = "_VRayRawTotalLightingMap"
# 新版 V-Ray 导出省略了 Total；两种 lighting pass 都接受。
LIGHTING_SUFFIXES = (LIGHTING_SUFFIX, "_VRayRawLightingMap")
AO_SUFFIX = "_VRayAOMap"
COMPLETE_SUFFIX = "_VRayCompleteMap"
IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".tga", ".bmp")
# These are inputs to the merge, not runtime textures. Keep Complete/Merged.
SOURCE_PASS_SUFFIXES = (DIFFUSE_SUFFIX, *LIGHTING_SUFFIXES, AO_SUFFIX)


def safe_name(name: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_.-]+", "_", name.strip())
    return s or "material"


def find_map(root: Path, object_name: str, suffix: str) -> Path | None:
    for ext in IMAGE_EXTS:
        p = root / f"{object_name}{suffix}{ext}"
        if p.exists():
            return p
    target = f"{object_name}{suffix}".lower()
    for p in root.iterdir():
        if p.is_file() and p.suffix.lower() in IMAGE_EXTS and p.stem.lower() == target:
            return p
    return None


def parse_obj_objects(lines: list[str]) -> tuple[list[str], dict[str, bool], set[str]]:
    objects: list[str] = []
    has_uv: dict[str, bool] = {}
    has_group: set[str] = set()
    current: str | None = None

    for line in lines:
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        parts = s.split()
        if parts[0] == "o" and len(parts) > 1:
            current = parts[1]
            if current not in objects:
                objects.append(current)
            has_uv.setdefault(current, False)
        elif parts[0] == "g" and len(parts) > 1:
            current = parts[1]
            has_group.add(current)
            if current not in objects:
                objects.append(current)
            has_uv.setdefault(current, False)
        elif parts[0] == "f" and current is not None:
            for token in parts[1:]:
                q = token.split("/")
                if len(q) > 1 and q[1]:
                    has_uv[current] = True
    return objects, has_uv, has_group


def srgb_to_linear(x: np.ndarray) -> np.ndarray:
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92,
                    1.055 * np.power(x, 1.0 / 2.4) - 0.055)


def load_rgba(path: Path, size: int) -> np.ndarray:
    im = Image.open(path).convert("RGBA")
    if im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)
    return np.asarray(im, dtype=np.float32) / 255.0


def merge_pair(diffuse: np.ndarray, lighting: np.ndarray,
               diffuse_space: str, lighting_space: str) -> Image.Image:
    d = diffuse[..., :3]
    l = lighting[..., :3]
    if diffuse_space == "srgb":
        d = srgb_to_linear(d)
    if lighting_space == "srgb":
        l = srgb_to_linear(l)

    rgb = linear_to_srgb(d * l)
    alpha = diffuse[..., 3:4]
    out = np.concatenate([rgb, alpha], axis=-1)
    out8 = np.clip(out * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return Image.fromarray(out8, "RGBA")


def is_black_image(path: Path, threshold: int = 2) -> bool:
    """RawLighting passes from some V-Ray exports are entirely black.

    Treat them as missing so the diffuse-only fallback is used instead of
    multiplying the albedo by zero.
    """
    try:
        with Image.open(path) as im:
            extrema = im.convert("RGB").getextrema()
        return all(hi <= threshold for _lo, hi in extrema)
    except Exception:
        return False


def make_fallback(path: Path, color: tuple[int, int, int], size: int = 64) -> None:
    im = Image.new("RGBA", (size, size), (color[0], color[1], color[2], 255))
    d = ImageDraw.Draw(im)
    step = max(8, size // 8)
    for y in range(0, size, step):
        for x in range(0, size, step):
            if ((x // step) + (y // step)) % 2 == 0:
                d.rectangle([x, y, x + step - 1, y + step - 1],
                            fill=(color[0], color[1], color[2], 255))
            else:
                d.rectangle([x, y, x + step - 1, y + step - 1],
                            fill=(min(255, color[0] + 18),
                                  min(255, color[1] + 18),
                                  min(255, color[2] + 18), 255))
    im.save(path)


def native_square_size(path: Path) -> int:
    with Image.open(path) as im:
        w, h = im.size
    target = max(w, h)
    p = 1
    while p < target:
        p <<= 1
    return p


def collect_mtl_texture_refs(root: Path) -> set[Path]:
    """Return every texture path referenced by any .mtl below root."""
    refs: set[Path] = set()
    for mtl in root.rglob("*.mtl"):
        try:
            lines = mtl.read_text(errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            key, _, rest = s.partition(" ")
            if not (key.startswith("map_") or key in ("bump", "disp", "decal")):
                continue
            tokens = rest.strip().split()
            if not tokens:
                continue
            token = tokens[-1].strip('"')
            if token:
                refs.add((mtl.parent / token).resolve())
    return refs


def prune_source_maps(root: Path, dry_run: bool = False,
                      trash_dir: Path | None = None) -> int:
    """Remove intermediate V-Ray passes that no MTL references.

    CompleteMap and Merged are final textures and are intentionally kept.
    """
    root = root.resolve()
    refs = collect_mtl_texture_refs(root)
    candidates: list[Path] = []
    image_exts = {e.lower() for e in IMAGE_EXTS}

    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in image_exts:
            continue
        if not any(path.stem.endswith(suffix) for suffix in SOURCE_PASS_SUFFIXES):
            continue
        if path.resolve() in refs:
            print(f"keep referenced: {path.relative_to(root).as_posix()}")
            continue
        candidates.append(path)

    total_bytes = 0
    for path in candidates:
        size = path.stat().st_size
        total_bytes += size
        rel = path.relative_to(root)
        if dry_run:
            print(f"would remove: {rel.as_posix()} ({size / 1024:.1f} KiB)")
            continue
        if trash_dir is not None:
            dest = trash_dir / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(path), str(dest))
            print(f"moved to trash: {rel.as_posix()} -> {dest}")
        else:
            path.unlink()
            print(f"removed: {rel.as_posix()}")

    action = "would remove" if dry_run else "removed"
    print(f"\n{action} {len(candidates)} V-Ray pass files, "
          f"{total_bytes / (1024 * 1024):.1f} MB total")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Build pig4 OBJ/MTL from V-Ray RTX maps.")
    ap.add_argument("obj", type=Path, nargs="?",
                    help="source OBJ exported from 3ds Max (omit with --prune-only)")
    ap.add_argument("--prefer", choices=("complete", "separate"), default="complete",
                    help="prefer existing CompleteMap (default) or diffuse+lighting merge")
    ap.add_argument("--size", type=int, default=0,
                    help="force output square power-of-two size; 0 = CompleteMap native / 1024 for merges")
    ap.add_argument("--diffuse-space", choices=("srgb", "linear"), default="srgb")
    ap.add_argument("--lighting-space", choices=("srgb", "linear"), default="linear",
                    help="VRayRawTotalLightingMap is treated as linear by default")
    ap.add_argument("--out-obj", type=Path, default=None)
    ap.add_argument("--out-mtl", type=Path, default=None)
    ap.add_argument("--fallback-color", default="120,120,120")
    ap.add_argument("--prune", action="store_true",
                    help="after building, remove unreferenced V-Ray source passes")
    ap.add_argument("--prune-only", action="store_true",
                    help="skip building; clean V-Ray source passes below --prune-root")
    ap.add_argument("--prune-root", type=Path, default=Path("assets/hotel"),
                    help="root for --prune-only (default: assets/hotel)")
    ap.add_argument("--dry-run", action="store_true",
                    help="with prune, only list files that would be removed")
    ap.add_argument("--trash-dir", type=Path, default=None,
                    help="move pruned files here instead of deleting them")
    args = ap.parse_args()

    trash_dir = args.trash_dir.resolve() if args.trash_dir else None
    if args.prune_only:
        prune_root = args.prune_root.resolve()
        if not prune_root.exists():
            print(f"error: prune root not found: {prune_root}", file=sys.stderr)
            return 2
        return prune_source_maps(prune_root, args.dry_run, trash_dir)

    if args.obj is None:
        print("error: OBJ is required unless --prune-only is used", file=sys.stderr)
        return 2

    if args.size < 0 or (args.size and (args.size & (args.size - 1)) != 0):
        print("error: --size must be 0 or a power of two", file=sys.stderr)
        return 2

    src = args.obj.resolve()
    if not src.exists():
        print(f"error: OBJ not found: {src}", file=sys.stderr)
        return 2
    root = src.parent

    out_obj = (args.out_obj or src.with_name(src.stem + "_engine" + src.suffix)).resolve()
    # 默认让生成的 MTL 跟着 engine OBJ 走，避免覆盖 3ds Max 导出的源 MTL
    out_mtl = (args.out_mtl or out_obj.with_suffix(".mtl")).resolve()
    if out_obj == src:
        print("error: refusing to overwrite source OBJ; use --out-obj", file=sys.stderr)
        return 2

    try:
        fallback_rgb = tuple(int(x) for x in args.fallback_color.split(","))
        if len(fallback_rgb) != 3:
            raise ValueError
    except Exception:
        print("error: --fallback-color must be R,G,B", file=sys.stderr)
        return 2

    lines = src.read_text(errors="replace").splitlines()
    objects, has_uv, has_group = parse_obj_objects(lines)
    if not objects:
        print("error: no o/g objects found", file=sys.stderr)
        return 2

    # PH_* 是给 manifest 用的标志物，不进材质表；OBJ 里的几何保留，
    # 运行时 AssetManager 会按 o/g 名字跳过。
    placeholders = [o for o in objects if o.startswith("PH_")]
    if placeholders:
        print("skip placeholders: " + ", ".join(placeholders))
        objects = [o for o in objects if not o.startswith("PH_")]

    material_names: dict[str, str] = {}
    used_names: set[str] = set()
    for obj in objects:
        base = safe_name(obj)
        name = base
        i = 2
        while name in used_names:
            name = f"{base}_{i}"
            i += 1
        used_names.add(name)
        material_names[obj] = name

    mtl_lines = ["# generated by tools/merge_vray_rtx.py"]
    total_tex_bytes = 0
    status_lines: list[str] = []

    for obj in objects:
        mat = material_names[obj]
        safe = mat
        diffuse = find_map(root, obj, DIFFUSE_SUFFIX)
        lighting = None
        for suffix in LIGHTING_SUFFIXES:
            lighting = find_map(root, obj, suffix)
            if lighting is not None:
                break
        if lighting is not None and is_black_image(lighting):
            status_lines.append(
                f"  note      {obj:24s} lighting map is black; using diffuse-only")
            lighting = None
        ao = find_map(root, obj, AO_SUFFIX)
        complete = find_map(root, obj, COMPLETE_SUFFIX)

        use_complete = complete is not None and (
            args.prefer == "complete" or diffuse is None or lighting is None
        )

        tex_path: Path | None = None
        tex_size = 0

        if use_complete:
            try:
                target = args.size or native_square_size(complete)
                if target == native_square_size(complete) and args.size == 0:
                    tex_path = complete
                    tex_size = target
                    status_lines.append(f"  complete  {obj:24s} using existing {complete.name}")
                else:
                    im = Image.open(complete).convert("RGBA")
                    if im.size != (target, target):
                        im = im.resize((target, target), Image.LANCZOS)
                    tex_path = root / f"{safe}_Complete.png"
                    im.save(tex_path, optimize=True)
                    tex_size = target
                    status_lines.append(f"  complete  {obj:24s} {complete.name} -> {tex_path.name} @ {target}")
            except Exception as e:
                status_lines.append(f"  ERROR     {obj:24s} CompleteMap failed: {e}")

        if tex_path is None and diffuse is not None and lighting is not None:
            try:
                target = args.size or 1024
                d = load_rgba(diffuse, target)
                l = load_rgba(lighting, target)
                merged = merge_pair(d, l, args.diffuse_space, args.lighting_space)
                tex_path = root / f"{safe}_Merged.png"
                merged.save(tex_path, optimize=True)
                tex_size = target
                status_lines.append(f"  merged    {obj:24s} {diffuse.name} + {lighting.name} -> {tex_path.name} @ {target}")
            except Exception as e:
                status_lines.append(f"  ERROR     {obj:24s} merge failed: {e}")

        if tex_path is None and diffuse is not None and ao is not None:
            try:
                target = args.size or 1024
                d = load_rgba(diffuse, target)
                a = load_rgba(ao, target)
                merged = merge_pair(d, a, args.diffuse_space, args.lighting_space)
                tex_path = root / f"{safe}_Merged.png"
                merged.save(tex_path, optimize=True)
                tex_size = target
                status_lines.append(f"  mergedAO  {obj:24s} {diffuse.name} x {ao.name} -> {tex_path.name} @ {target}")
            except Exception as e:
                status_lines.append(f"  ERROR     {obj:24s} AO merge failed: {e}")

        if tex_path is None and diffuse is not None:
            try:
                target = args.size or 1024
                d = load_rgba(diffuse, target)
                white = np.ones_like(d)
                merged = merge_pair(d, white, args.diffuse_space, args.lighting_space)
                tex_path = root / f"{safe}_Merged.png"
                merged.save(tex_path, optimize=True)
                tex_size = target
                status_lines.append(f"  diffuse   {obj:24s} lighting missing; used white lighting")
            except Exception as e:
                status_lines.append(f"  ERROR     {obj:24s} diffuse fallback failed: {e}")

        if tex_path is None and lighting is not None:
            try:
                target = args.size or 1024
                l = load_rgba(lighting, target)
                white = np.ones_like(l)
                merged = merge_pair(white, l, args.diffuse_space, args.lighting_space)
                tex_path = root / f"{safe}_Merged.png"
                merged.save(tex_path, optimize=True)
                tex_size = target
                status_lines.append(f"  lighting  {obj:24s} diffuse missing; used white albedo")
            except Exception as e:
                status_lines.append(f"  ERROR     {obj:24s} lighting fallback failed: {e}")

        if tex_path is None:
            tex_path = root / f"_fallback_{safe}.png"
            make_fallback(tex_path, fallback_rgb)
            tex_size = 64
            status_lines.append(f"  FALLBACK  {obj:24s} no maps -> {tex_path.name}")

        if not has_uv.get(obj, False):
            status_lines.append(f"  WARNING   {obj:24s} no UVs in OBJ; texture cannot map correctly")

        total_tex_bytes += tex_size * tex_size * 4 * 4 // 3
        mtl_lines += [f"newmtl {mat}", "Kd 1.0 1.0 1.0", f"map_Kd {tex_path.name}", ""]

    out_mtl.write_text("\n".join(mtl_lines) + "\n")

    out_lines: list[str] = []
    inserted_mtllib = False
    for line in lines:
        s = line.strip()
        if s.startswith("mtllib ") or s.startswith("usemtl "):
            continue
        if not inserted_mtllib and s.startswith("o "):
            out_lines.append(f"mtllib {out_mtl.name}")
            inserted_mtllib = True
        out_lines.append(line)
        if s.startswith("g ") and len(s.split()) > 1:
            obj = s.split()[1]
            if obj in material_names:
                out_lines.append(f"usemtl {material_names[obj]}")
        elif s.startswith("o ") and len(s.split()) > 1:
            obj = s.split()[1]
            if obj not in has_group and obj in material_names:
                out_lines.append(f"usemtl {material_names[obj]}")
    if not inserted_mtllib:
        out_lines.insert(0, f"mtllib {out_mtl.name}")
    out_obj.write_text("\n".join(out_lines) + "\n")

    print("\n".join(status_lines))
    print()
    print(f"wrote {out_obj}")
    print(f"wrote {out_mtl}")
    print(f"per-room runtime texture estimate: "
          f"{total_tex_bytes / (1024 * 1024):.1f} MB (RGBA + mips)")
    if total_tex_bytes > 128 * 1024 * 1024:
        print("warning: texture budget is high; rerun with --size 512 or pack an atlas.")
    if args.prune:
        print()
        prune_source_maps(root, args.dry_run, trash_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
