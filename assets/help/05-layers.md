# Layers and elements

## Layers

The **Layers** panel lists the layers of the picture, topmost first, each with a thumbnail of
what that layer alone draws. Right-click a layer row for everything below.

- **Add** creates a layer; **Dup** (`Ctrl+J`) copies it; **Delete** removes it.
- **Reorder**: drag a row, or `Ctrl+]` and `Ctrl+[`.
- **Visibility**: the eye at the left of the row. A hidden layer is shown faint in its thumbnail.
- **Opacity** and **blend mode** (eleven, from Multiply to Difference) are above the list, and
  work on a layer or on a whole group.
- **Lock** keeps the tools off a layer.
- **Clip to below** draws a layer only where the layer under it draws: a highlight that stays
  inside the body it is on.
- **Merge down** (`Ctrl+E`) moves the upper layer's elements into the one below, each still
  what it was, with the opacity and blend folded in. Where that would change the picture
  (a transform, an outline, a hidden layer, two blend modes) it is refused and the reason is said.
- **Layer properties** (in the row's menu) gives a layer a colour tag that stripes its row, and
  notes kept with it.
- **Reference layer** shows a layer on the canvas but leaves it out of every export and out of the
  outline round the whole figure: a sketch to draw over.

## Groups

Select several layers with `Ctrl`+click and press `Ctrl+G` to group them. A group composites its
layers into one picture and *then* blends that, which is not the same as blending each, and is
what a group is for. Drop a layer on a group's row to put it in; `Ctrl`+drop on a layer groups the
two; `Ctrl+Shift+G` dissolves a group in place.

## Elements

One layer holds several **elements**: one per colour its pixels are painted in, plus any shapes,
text, fills and gradients drawn onto it. The **Elements** panel lists them, topmost first.
Select one to change it in **Properties**: what it is, its controls, then the layer's shadow and
outline (each folding away).

A layer is a **stack**: removing the top element brings back what was under it, exactly. Painting
over a colour does not cut away the old one; erasing is recorded as what was erased from which
element, so it never touches anything drawn later.

## Layers across frames

In an animation, a layer runs through every frame: add, delete, move, rename, hide, group or tag
one in any frame and it is so in all of them, each frame keeping its own pixels of it. See
[Animation](topic:animation). Older files whose frames had separate layers keep them until
**Sprite > Same layers in every frame** joins them by name, losing nothing.

## Undo

Every operation is one undo step, and **Edit > History...** lists them: click one to go back to it.
The undo depth is set in Preferences.
