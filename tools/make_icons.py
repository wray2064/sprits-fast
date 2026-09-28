# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 the Sprit's'fast authors
"""Makes the program's icons from the logo: python tools/make_icons.py

assets/icon/logo.png is the source. From it: the artwork cropped to what it
draws, centred on a square with a little margin, and resized (Lanczos) to
every size Windows asks an icon for --

  sprits_fast.ico        16 to 256, the executable's (src/ui/sprits_fast.rc)
  sprits_fast_256.png    the window's (compiled in; app_window.cpp)
  sprits_fast_32.png     the window's small one, beside it
  sprits_fast_1024.png   for a store page, a README, an installer

Needs Pillow. Run it again whenever the logo changes, then rebuild.
"""
import os
from PIL import Image

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'icon')

logo = Image.open(os.path.join(here, 'logo.png')).convert('RGBA')
# The artwork's bounds, ignoring the faint haze at the edge of its alpha.
solid = logo.getchannel('A').point(lambda a: 255 if a > 40 else 0)
art = logo.crop(solid.getbbox())
w, h = art.size
side = int(max(w, h) * 1.06)
square = Image.new('RGBA', (side, side), (0, 0, 0, 0))
square.paste(art, ((side - w) // 2, (side - h) // 2))

master = square.resize((1024, 1024), Image.LANCZOS)
sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
frames = {s: master.resize((s, s), Image.LANCZOS) for s in sizes}

master.save(os.path.join(here, 'sprits_fast_1024.png'), optimize=True)
frames[256].save(os.path.join(here, 'sprits_fast_256.png'), optimize=True)
frames[32].save(os.path.join(here, 'sprits_fast_32.png'), optimize=True)
frames[256].save(os.path.join(here, 'sprits_fast.ico'), sizes=[(s, s) for s in sizes],
                 append_images=[frames[s] for s in sizes[:-1]])
print('icons written to', os.path.normpath(here))
