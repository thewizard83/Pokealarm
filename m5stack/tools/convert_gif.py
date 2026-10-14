#!/usr/bin/env python3
"""
Converte una GIF animata qualsiasi nel formato usato da PokeSveglia:
320x240 pixel (schermo della M5Stack Core2), fotogrammi completi.

Uso:
    python3 tools/convert_gif.py input.gif data/gifs/nome.gif
    python3 tools/convert_gif.py input.gif data/gifs/nome.gif --fit cover --pixel

Opzioni:
    --fit contain   (predefinito) l'immagine intera entra nello schermo
    --fit cover     riempie lo schermo tagliando i bordi
    --pixel         ridimensionamento "pixel art" (bordi netti)
    --bg RRGGBB     colore di sfondo per le zone vuote (predefinito: colore dell'angolo)

Richiede Pillow:  pip3 install pillow
"""
import argparse
from PIL import Image, ImageSequence

W, H = 320, 240


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--fit", choices=["contain", "cover"], default="contain")
    ap.add_argument("--pixel", action="store_true")
    ap.add_argument("--bg", default=None)
    a = ap.parse_args()

    src = Image.open(a.input)
    w, h = src.size
    scale = (min if a.fit == "contain" else max)(W / w, H / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    ox, oy = (W - nw) // 2, (H - nh) // 2
    resample = Image.NEAREST if a.pixel else Image.LANCZOS

    frames, durations, bg = [], [], None
    for fr in ImageSequence.Iterator(src):
        rgba = fr.convert("RGBA")  # Pillow applica già trasparenze e disposal
        if bg is None:
            if a.bg:
                bg = tuple(int(a.bg[i:i + 2], 16) for i in (0, 2, 4)) + (255,)
            else:
                c = rgba.getpixel((0, 0))
                bg = c if c[3] > 0 else (0, 0, 0, 255)
        canvas = Image.new("RGBA", (W, H), bg)
        layer = rgba.resize((nw, nh), resample)
        canvas.alpha_composite(layer, (ox, oy))
        frames.append(canvas.convert("RGB").quantize(colors=255, dither=Image.Dither.NONE))
        d = fr.info.get("duration", 100) or 100
        durations.append(max(20, d))

    frames[0].save(a.output, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, disposal=1, optimize=False)
    print(f"OK: {len(frames)} fotogrammi, {W}x{H} -> {a.output}")


if __name__ == "__main__":
    main()
