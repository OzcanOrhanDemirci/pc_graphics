"""Fetch the CC0 Poly Haven textures used by Ruya (scene 14).

Files land in assets/ruya/cc0/, which git ignores. Sources: assets/ruya/KAYNAKLAR.md.
"""
import json
import pathlib
import sys
import urllib.request

MATERIALS = ["rock_face_03", "aerial_grass_rock", "large_sandstone_blocks_01"]
MAPS = {"Diffuse": "diff", "nor_gl": "nor_gl", "arm": "arm"}
RESOLUTION = "8k"


def main() -> int:
    target = pathlib.Path(__file__).resolve().parent / "cc0"
    target.mkdir(exist_ok=True)
    agent = {"User-Agent": "pc_graphics-asset-fetch"}
    for name in MATERIALS:
        request = urllib.request.Request(f"https://api.polyhaven.com/files/{name}", headers=agent)
        files = json.load(urllib.request.urlopen(request))
        for key, short in MAPS.items():
            entry = files[key][RESOLUTION]["jpg"]
            path = target / f"{name}_{short}.jpg"
            if path.exists() and path.stat().st_size == entry["size"]:
                continue
            print(f"{path.name} {entry['size'] / 1e6:.1f} MB", flush=True)
            with urllib.request.urlopen(urllib.request.Request(entry["url"], headers=agent)) as source:
                path.write_bytes(source.read())
            if path.stat().st_size != entry["size"]:
                print(f"size mismatch: {path.name}", file=sys.stderr)
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
