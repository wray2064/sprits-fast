#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 the Sprit's'fast authors
"""Checks Fast's lossless WebP writer and reader against libwebp, through Pillow.

    python tools/webp_crosscheck.py build/fast_webp_tests.exe

Both directions, every pixel:
  * what Fast writes (`--write DIR`) is decoded by libwebp and compared with the
    raw RGBA it came from;
  * pictures libwebp writes, at every effort level and in the shapes that pull
    in each transform (palette, predictor, cross-colour, colour cache, several
    prefix-code groups), are decoded by Fast (`--read IN OUT`) and compared.

Needs Pillow built with WebP. Nothing here ships; it is a check for a person to
run after touching src/app/webp*.cpp. Exit status is the number of mismatches.
"""
import os
import random
import subprocess
import sys
import tempfile

from PIL import Image


def read_raw(path):
    with open(path, "rb") as f:
        width, height = map(int, f.readline().split())
        return width, height, f.read()


def pictures():
    rng = random.Random(1234)
    out = {}
    # Smooth: a gradient with a little noise, the photograph-like case.
    img = Image.new("RGBA", (96, 64))
    img.putdata([(x * 2 + rng.randrange(4), y * 3 + rng.randrange(4), (x + y) % 256, 255)
                 for y in range(64) for x in range(96)])
    out["gradient"] = img
    # Flat colour blocks with transparency: the sprite-like case.
    img = Image.new("RGBA", (80, 48))
    img.putdata([((x // 8) * 30, (y // 8) * 40, 100, 0 if (x // 8 + y // 8) % 3 == 0 else 255)
                 for y in range(48) for x in range(80)])
    out["blocks"] = img
    # Few colours, tiny and awkward widths (the palette packing).
    for colours, width in ((2, 13), (3, 7), (9, 21), (200, 33)):
        img = Image.new("RGBA", (width, 17))
        palette = [(rng.randrange(256), rng.randrange(256), rng.randrange(256), 255)
                   for _ in range(colours)]
        img.putdata([palette[rng.randrange(colours)] for _ in range(width * 17)])
        out[f"palette{colours}"] = img
    # Noise: nothing to predict.
    img = Image.new("RGBA", (50, 40))
    img.putdata([tuple(rng.randrange(256) for _ in range(4)) for _ in range(50 * 40)])
    out["noise"] = img
    # Repeats at a distance, for the copy codes and the colour cache.
    tile = [tuple(rng.randrange(256) for _ in range(3)) + (255,) for _ in range(37)]
    img = Image.new("RGBA", (123, 70))
    img.putdata([tile[(x + 3 * y) % 37] for y in range(70) for x in range(123)])
    out["tiled"] = img
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        # Fast writes, libwebp reads.
        subprocess.run([exe, "--write", tmp], check=True, stdout=subprocess.DEVNULL)
        checked = 0
        for name in sorted(os.listdir(tmp)):
            if not name.startswith("sample") or not name.endswith(".webp"):
                continue
            width, height, raw = read_raw(os.path.join(tmp, name[:-5] + ".rgba"))
            got = Image.open(os.path.join(tmp, name)).convert("RGBA")
            checked += 1
            if got.size != (width, height) or got.tobytes() != raw:
                print("MISMATCH (libwebp reading Fast's):", name)
                bad += 1
        print(f"{checked} pictures Fast wrote, read by libwebp")

        # libwebp writes, Fast reads.
        checked = 0
        for name, img in pictures().items():
            for method in range(7):
                path = os.path.join(tmp, f"lib_{name}_{method}.webp")
                img.save(path, "WEBP", lossless=True, method=method, exact=True)
                out = path + ".rgba"
                done = subprocess.run([exe, "--read", path, out], capture_output=True, text=True)
                checked += 1
                if done.returncode != 0:
                    print("FAST COULD NOT READ:", name, "method", method, done.stdout.strip())
                    bad += 1
                    continue
                width, height, raw = read_raw(out)
                if (width, height) != img.size or raw != img.tobytes():
                    print("MISMATCH (Fast reading libwebp's):", name, "method", method)
                    bad += 1
        print(f"{checked} pictures libwebp wrote, read by Fast")
    print("webp cross-check:", "all agree" if bad == 0 else f"{bad} mismatch(es)")
    return bad


if __name__ == "__main__":
    sys.exit(main())
