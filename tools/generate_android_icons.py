#!/usr/bin/env python3
"""
Generates Android launcher icons (standard, round, adaptive foreground, XMLs)
and hero banner drawable from assets/emesis_logo.png.
"""
import math
import pathlib
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC_LOGO = ROOT / "assets" / "emesis_logo.png"
RES_DIR = ROOT / "app" / "src" / "main" / "res"

MIPMAP_SIZES = {
    "mipmap-mdpi": 48,
    "mipmap-hdpi": 72,
    "mipmap-xhdpi": 96,
    "mipmap-xxhdpi": 144,
    "mipmap-xxxhdpi": 192,
}

ADAPTIVE_FG_SIZES = {
    "mipmap-mdpi": 108,
    "mipmap-hdpi": 162,
    "mipmap-xhdpi": 216,
    "mipmap-xxhdpi": 324,
    "mipmap-xxxhdpi": 432,
}


def make_round_icon(square_img: Image.Image) -> Image.Image:
    size = square_img.size[0]
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    draw.ellipse((0, 0, size - 1, size - 1), fill=255)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(square_img.convert("RGBA"), (0, 0), mask)
    return out


def main():
    src = Image.open(SRC_LOGO).convert("RGBA")

    # 1. Hero banner drawable (512x512 crisp RGBA) for MainActivity companion UI
    nodpi = RES_DIR / "drawable-nodpi"
    nodpi.mkdir(parents=True, exist_ok=True)
    hero = src.resize((512, 512), Image.Resampling.LANCZOS)
    hero.save(nodpi / "emesis_logo_hero.png", format="PNG", optimize=True)

    # 2. Standard & Round launcher icons per density
    for folder, px in MIPMAP_SIZES.items():
        d = RES_DIR / folder
        d.mkdir(parents=True, exist_ok=True)
        sq = src.resize((px, px), Image.Resampling.LANCZOS)
        sq.save(d / "ic_launcher.png", format="PNG", optimize=True)
        rd = make_round_icon(sq)
        rd.save(d / "ic_launcher_round.png", format="PNG", optimize=True)

    # 3. Adaptive icon foregrounds (safe-zone inset ~66% of 108dp canvas)
    for folder, canvas_px in ADAPTIVE_FG_SIZES.items():
        d = RES_DIR / folder
        d.mkdir(parents=True, exist_ok=True)
        canvas = Image.new("RGBA", (canvas_px, canvas_px), (0, 0, 0, 0))
        inner_px = int(round(canvas_px * 0.68))
        inner = src.resize((inner_px, inner_px), Image.Resampling.LANCZOS)
        off = (canvas_px - inner_px) // 2
        canvas.paste(inner, (off, off), inner)
        canvas.save(d / "ic_launcher_foreground.png", format="PNG", optimize=True)

    # 4. Adaptive icon XMLs (API 26+)
    anydpi = RES_DIR / "mipmap-anydpi-v26"
    anydpi.mkdir(parents=True, exist_ok=True)
    adaptive_xml = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
</adaptive-icon>
"""
    (anydpi / "ic_launcher.xml").write_text(adaptive_xml, encoding="utf-8")
    (anydpi / "ic_launcher_round.xml").write_text(adaptive_xml, encoding="utf-8")
    print("Generated all Android mipmap icons and hero logo drawable.")


if __name__ == "__main__":
    main()
