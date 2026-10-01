"""Contact sheet: several PNG renders side by side with their names, for comparing variants by eye.

  python tools/sheet.py out.png a.png b.png ... [--cols 2] [--width 960]
"""
import argparse
import pathlib

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--cols", type=int, default=2)
    ap.add_argument("--width", type=int, default=960)
    a = ap.parse_args()
    ims = [Image.open(p).convert("RGB") for p in a.images]
    w = a.width
    h = int(ims[0].height * w / ims[0].width)
    rows = (len(ims) + a.cols - 1) // a.cols
    sheet = Image.new("RGB", (w * a.cols, h * rows), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    for i, (im, p) in enumerate(zip(ims, a.images)):
        x, y = (i % a.cols) * w, (i // a.cols) * h
        sheet.paste(im.resize((w, h), Image.LANCZOS), (x, y))
        d.rectangle([x, y, x + 8 * len(pathlib.Path(p).stem) + 12, y + 22], fill=(0, 0, 0))
        d.text((x + 6, y + 5), pathlib.Path(p).stem, fill=(255, 230, 200))
    sheet.save(a.out)
    print(a.out, sheet.size)


if __name__ == "__main__":
    main()
