# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 the Sprit's'fast authors
"""Makes the Microsoft Store images from the logo: python tools/make_store_assets.py

assets/icon/logo.png is the source (see tools/make_icons.py, which crops it to a
square). Writes, under store/:

  assets/   what AppxManifest.xml names, at every scale Windows asks for: the
            app list icon (and its unplated and target-size forms), the medium,
            small, wide and large tiles, the splash screen, the Store logo
  listing/  what is uploaded in Partner Center beside the screenshots: the
            300x300 app tile icon, 1080x1080 box art, 720x1080 poster art and
            1920x1080 hero art

The logo's right-hand stroke fades to nothing, so it is drawn on the dark of the
Slate theme (#1d2229) and never on white: the manifest's BackgroundColor is the same.

Needs Pillow. Run it again whenever the logo changes.
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SOURCE = os.path.join(ROOT, 'assets', 'icon', 'sprits_fast_1024.png')
FONT = os.path.join(ROOT, 'assets', 'fonts', 'Inter-SemiBold.ttf')
FONT_REGULAR = os.path.join(ROOT, 'assets', 'fonts', 'Inter-Regular.ttf')
ASSETS = os.path.join(ROOT, 'store', 'assets')
LISTING = os.path.join(ROOT, 'store', 'listing')
BACKGROUND = (29, 34, 41, 255)      # Slate's panel: the manifest's BackgroundColor
BACKGROUND_DEEP = (20, 24, 29, 255)

SCALES = [100, 125, 150, 200, 400]

logo = Image.open(SOURCE).convert('RGBA')


def fitted(size, fraction):
    """The logo, as large as `fraction` of the shorter side allows."""
    side = max(1, int(round(min(size) * fraction)))
    return logo.resize((side, side), Image.LANCZOS)


def tile(size, fraction, background=None):
    """A canvas of `size` with the logo centred; transparent unless a background is given."""
    canvas = Image.new('RGBA', size, background if background is not None else (0, 0, 0, 0))
    art = fitted(size, fraction)
    canvas.paste(art, ((size[0] - art.width) // 2, (size[1] - art.height) // 2), art)
    return canvas


def save(image, folder, name):
    os.makedirs(folder, exist_ok=True)
    image.save(os.path.join(folder, name), optimize=True)


def scaled(base, name, fraction, background=None):
    """name.scale-N.png for every scale; `base` is the size at 100%."""
    for scale in SCALES:
        size = (int(round(base[0] * scale / 100)), int(round(base[1] * scale / 100)))
        save(tile(size, fraction, background), ASSETS, '%s.scale-%d.png' % (name, scale))


# The app list, the taskbar and the title bar: the logo itself, nearly full size.
scaled((44, 44), 'Square44x44Logo', 0.96)
for target in (16, 24, 32, 48, 256):
    save(tile((target, target), 0.98), ASSETS, 'Square44x44Logo.targetsize-%d.png' % target)
    # Unplated: no coloured square behind it on the taskbar.
    save(tile((target, target), 0.98), ASSETS, 'Square44x44Logo.targetsize-%d_altform-unplated.png' % target)

# Tiles: the logo on a tile that the manifest's BackgroundColor fills.
scaled((71, 71), 'SmallTile', 0.70)
scaled((150, 150), 'Square150x150Logo', 0.62)
scaled((310, 150), 'Wide310x150Logo', 0.78)
scaled((310, 310), 'LargeTile', 0.62)
scaled((50, 50), 'StoreLogo', 0.94)
scaled((620, 300), 'SplashScreen', 0.66)

# The listing.
save(tile((300, 300), 0.74, BACKGROUND), LISTING, 'app-tile-300x300.png')
save(tile((1080, 1080), 0.62, BACKGROUND), LISTING, 'box-art-1080x1080.png')


def fit_font(path, text, start, max_width):
    """The largest font from `start` down at which `text` is no wider than `max_width`."""
    for size in range(start, 8, -1):
        font = ImageFont.truetype(path, size)
        if font.getlength(text) <= max_width:
            return font
    return ImageFont.truetype(path, 8)


def title_card(size, title, subtitle, logo_fraction, vertical):
    """Dark card with the logo and the name; `vertical` stacks them."""
    canvas = Image.new('RGBA', size, BACKGROUND)
    # A little depth: the deeper dark at the foot.
    shade = Image.new('RGBA', size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(shade)
    for y in range(size[1]):
        alpha = int(70 * y / size[1])
        draw.line([(0, y), (size[0], y)], fill=BACKGROUND_DEEP[:3] + (alpha,))
    canvas = Image.alpha_composite(canvas, shade)
    art = fitted(size if vertical else (size[1], size[1]), logo_fraction)
    draw = ImageDraw.Draw(canvas)
    margin = int(size[0] * 0.06)
    if vertical:
        room = size[0] - 2 * margin
    else:
        room = size[0] - (int(size[0] * 0.14) + art.width + int(size[0] * 0.03)) - margin
    big = fit_font(FONT, title, int(size[1] * (0.055 if vertical else 0.10)), room)
    small = fit_font(FONT_REGULAR, subtitle, int(size[1] * (0.028 if vertical else 0.045)), room)
    if vertical:
        x = (size[0] - art.width) // 2
        canvas.paste(art, (x, int(size[1] * 0.12)), art)
        top = int(size[1] * 0.12) + art.height + int(size[1] * 0.04)
        draw.text((size[0] // 2, top), title, font=big, fill=(232, 240, 248, 255), anchor='ma')
        draw.text((size[0] // 2, top + int(size[1] * 0.075)), subtitle, font=small,
                  fill=(160, 174, 189, 255), anchor='ma')
    else:
        left = int(size[0] * 0.14)
        canvas.paste(art, (left, (size[1] - art.height) // 2), art)
        text_x = left + art.width + int(size[0] * 0.03)
        middle = size[1] // 2
        draw.text((text_x, middle - int(size[1] * 0.02)), title, font=big,
                  fill=(232, 240, 248, 255), anchor='ls')
        draw.text((text_x, middle + int(size[1] * 0.09)), subtitle, font=small,
                  fill=(160, 174, 189, 255), anchor='ls')
    return canvas


subtitle = 'A sprite editor where every stroke stays a stroke'
save(title_card((720, 1080), "Sprit's'fast", subtitle, 0.80, True), LISTING, 'poster-720x1080.png')
save(title_card((1920, 1080), "Sprit's'fast", subtitle, 0.62, False), LISTING, 'hero-1920x1080.png')

print('store images written to', os.path.normpath(os.path.join(ROOT, 'store')))
