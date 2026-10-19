# Animation

## The timeline

A frame is a sprite, and the document's frames in order are the animation. Press `T` to open the
**timeline**. It has two forms:

- **A drawer** (the default): a small tab at the bottom edge of the canvas says which frame you are on.
  Rest the pointer on it, or click it, or press `T`, and the strip opens over the canvas; move away and
  it tucks again (a click or `T` keeps it open until `T`, `Esc` or **Hide**). The canvas keeps the whole
  window, and nothing resizes when the strip comes and goes.
- **Pinned**: the **Pin** button (or **Window > Pin the timeline**) docks it under the canvas as an
  ordinary panel, which is what animating all day wants. **Unpin** lets it go back. The **Animation**
  layout (Window > Layouts) pins it for you.

## Frames

- **+ Frame** adds a frame after the one you are on, copying its drawing and sharing its palette;
  **+ Linked** adds one that shares its cels with the original (a stroke in either lands in both);
  **+ Empty** adds a blank one.
- Click a frame's thumbnail to edit it. `,` and `.` step back and forward.
- **Drag** a frame to reorder it. **Delete** removes it. `Ctrl+Shift+D` duplicates the current frame.
- **Hold** sets how long the frame stays on screen, in milliseconds; `Shift`+click selects a run of
  frames, and the buttons then act on the whole run: duplicate, delete, **Reverse**, or give them one hold.
- Right-click a frame for its menu: add an empty frame after it, duplicate it, move it left or right,
  reverse the selected run, delete it.

Layers run through the whole animation: add, delete, move, rename, hide or group a layer in any frame and it
is so in every frame, each keeping its own pixels of it. See [Layers and elements](topic:layers).

## Linked cels

A **linked cel** makes a layer's cel in several frames one cel: a stroke in any of them lands in all, as it is
drawn. **+ Linked** duplicates a frame that way, and a layer row's menu links or unlinks a single layer.

## Playback

`Enter` plays and stops. **Speed** previews at half speed to judge timing, or double to see the motion.
Playback is a function of elapsed time, so a dropped frame costs nothing and scrubbing lands on exactly
what playing showed. While it plays the canvas shows the frame the clock says; stopping is what changes
which frame the tools act on.

## Onion skin

`O` turns on **onion skin**: the frames either side drawn faint under the live one, with their own tints.
Its settings (under the **v** beside it) choose how many frames before and after, and whether the
neighbours come from the frame list or from the selected cycle, so they are what will play either side of
this one. The corner **preview** (`P`) shows the sprite at 1x to 4x against any of eight backdrops.

## Cycles

A **cycle** is a named run of frames, "walk" or "hurt", with its own loop mode, saved with the file. It is a
*sequence*, not a subset: a step names a frame, and two steps may name the same one, so a four-frame walk
drawn once plays `0 1 2 1`.

1. Press **New** beside *Cycle* and name it.
2. Add steps: pick a frame and press the add button, in the order they should play.
3. Choose **when it ends**: loop, play once, or ping-pong (there and back without repeating either end).

Play, onion skin and the exports use the selected cycle, or every frame when none is chosen. A frame carries
one dot per time a cycle plays it, and a frame no step names is dimmed, not flagged.

## Tweens

See [Rotating, scaling and the canvas](topic:transforms): set a transform in two frames, select the run and
press **Tween**.

## Palettes in time

A frame can be given its own palette, so a flash frame, or one palette per frame along the timeline, is
colour cycling for nothing. See [Colour and palettes](topic:colour).

## Getting it out

**File > Export animation...** writes the selected cycle (or every frame) as an animated GIF, animated PNG,
animated WebP or a numbered PNG sequence; **Export sheet...** lays the frames into one image. See
[Opening, saving and exporting](topic:files).
