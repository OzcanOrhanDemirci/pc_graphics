"""Fetch the CC0 Poly Haven scans used by Ruya (scene 14).

Files land in assets/ruya/cc0/, which git ignores. Sources: assets/ruya/KAYNAKLAR.md.
Running it again only downloads what is missing or has the wrong size.
Keep SETS in step with kTextureNames in src/scenes/ruya/ruya_renderer.cpp.
"""
import json
import pathlib
import sys
import urllib.request

AGENT = {"User-Agent": "pc_graphics-asset-fetch"}
SURFACE = {"Diffuse": "diff", "nor_gl": "nor_gl", "arm": "arm", "Displacement": "disp"}

# (Poly Haven asset, local name, resolution, API map key -> local suffix)
SETS = [
    ("rock_face_03", "rock_face_03", "8k", SURFACE),
    ("aerial_grass_rock", "aerial_grass_rock", "8k", SURFACE),
    ("large_sandstone_blocks_01", "large_sandstone_blocks_01", "8k", SURFACE),
    ("cliff_side", "cliff_side", "8k", SURFACE),
    ("leafy_grass", "leafy_grass", "8k", SURFACE),
    ("pine_bark", "pine_bark", "4k", SURFACE),
    ("clay_roof_tiles", "clay_roof_tiles", "4k", SURFACE),
    ("weathered_planks", "weathered_planks", "4k", SURFACE),
    ("rust_coarse_01", "rust_coarse_01", "4k", SURFACE),
    ("tree_small_02", "tree_small_02_leaves", "4k",
     {"leaves_diff": "diff", "leaves_nor_gl": "nor_gl", "leaves_arm": "arm", "leaves_alpha": "alpha"}),
    ("fir_tree_01", "fir_tree_01_twig", "4k",
     {"twig_diff": "diff", "twig_nor_gl": "nor_gl", "twig_arm": "arm", "twig_alpha": "alpha"}),
]


def fetch_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT)) as source:
        return json.loads(source.read().decode("utf8"))


def main() -> int:
    target = pathlib.Path(__file__).resolve().parent / "cc0"
    target.mkdir(exist_ok=True)
    for asset, local, resolution, maps in SETS:
        files = fetch_json(f"https://api.polyhaven.com/files/{asset}")
        for key, suffix in maps.items():
            formats = files[key][resolution]
            extension = "jpg" if "jpg" in formats else "png"
            entry = formats[extension]
            path = target / f"{local}_{suffix}.{extension}"
            if path.exists() and path.stat().st_size == entry["size"]:
                continue
            print(f"{path.name} {entry['size'] / 1e6:.1f} MB", flush=True)
            with urllib.request.urlopen(urllib.request.Request(entry["url"], headers=AGENT)) as source:
                path.write_bytes(source.read())
            if path.stat().st_size != entry["size"]:
                print(f"size mismatch: {path.name}", file=sys.stderr)
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
