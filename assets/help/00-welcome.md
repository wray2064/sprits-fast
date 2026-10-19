# Welcome

Sprit's'fast is a sprite and pixel-art editor with one unusual promise: **nothing you
draw is turned into pixels until it is shown.** A pencil stroke is kept as the path the
pencil took and the brush it took it with. A bucket fill is kept as the area it found. A
rectangle is a rectangle. Pixels exist only on the screen and in the files you export.

That is why you can turn a layer by 23 degrees, scale it, change a palette colour, or
change your mind ten steps back, and the picture is simply drawn again from what you
did. Nothing is resampled, so nothing goes soft.

> This guide is inside the program, works offline, and describes this version. Press
> **F1** any time to open it, and use the search box on the left to find a word.

## Where to begin

- New here? Read [Getting started](topic:getting-started): five minutes from an empty
  canvas to a saved, exported sprite.
- Coming from another pixel editor? Read [Drawing](topic:drawing), then
  [Colour and palettes](topic:colour): the palette works differently, and it is the best part.
- Making an animation? [Animation](topic:animation), then
  [Cutout puppets](topic:puppets) for characters built from parts.
- Want to know what every key does? [Keyboard shortcuts](topic:shortcuts), or
  **Help > Keyboard shortcuts**, which also lets you change them.
- Something is not working? [Troubleshooting and limits](topic:troubleshooting).

## What stays editable

Because the work is kept as what you did, a great deal stays editable that is usually
lost the moment you let go of the mouse:

- **Shapes**: a rectangle you drew an hour ago still has its size, corners and handles.
- **Text**: a piece of text is words, a place and a size, so a typo is retyped, not erased.
- **Colours**: a colour painted through a palette slot follows the slot. Change the slot
  and everything that used it changes, on every frame.
- **Transforms**: rotation, scale and offset are numbers on the layer. Set them back to
  zero and the original returns exactly.
- **Outlines and shadows**: generated from whatever is drawn, so they follow it.

## What it keeps for you

Your work is written to `.lsprite` files. While you work the program also saves recovery
copies, so a crash or a power cut does not cost you anything: see
[Opening, saving and exporting](topic:files).

Sprit's'fast is free and open source (Apache License 2.0). It has no account, collects
nothing and never connects to the internet.
