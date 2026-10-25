# Cutout puppets

Cutout animation, the way a hand does it: a character built from parts, each turning about its own joint,
posed by hand and captured to a frame.

## Make the parts

1. Draw each limb or piece on a **layer of its own**.
2. In the **Puppet** panel, make each layer a **part**. It leaves the frame and becomes a piece of its own,
   drawn exactly where it was.
3. Hang each part from the one it moves with: an arm from the torso, a forearm from the arm. It hangs where
   it is drawn, so the character does not change, pixel for pixel.

## Draw part

In **Draw part** the canvas shows one part, with every tool. Its **joint** (where it turns) and its
**sockets** (where other parts hang) are marks you click into place or drag. A joint is put in the middle
of a pixel, so a quarter turn sends every pixel to a pixel and a limb posed at 90 degrees is the limb turned,
exactly.

## Pose

In **Pose** the canvas shows the whole puppet.

- Click a part to pick it. The **ring** round its joint turns it; `Shift` gives steps of 15 degrees, so a limb
  stops on 90.
- The **square** on the root moves the whole puppet by whole pixels.
- **Capture pose** makes a frame of it after the one showing, and the next capture goes after that:
  pose, capture, pose, capture.

A captured frame is the parts' own shapes placed where the pose put them, not a picture of them, and after
that a frame like any other, to fix by hand. Each part is a row of the timeline, the same in every capture.

## Good to know

- Turning or flipping the canvas turns the puppet and every capture with it.
- A puppet is built from the engine's own pivots, sockets and attachments, and the pose each frame was captured
  from is written on it.
- A document may have up to 256 parts.

See also [Animation](topic:animation) for what to do with the frames you capture.
