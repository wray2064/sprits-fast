# References and the library

## Drawing from a picture

**File > Import reference...** brings in an image to draw from: a photograph, a sketch, a pose sheet. It sits
over or under the canvas at any opacity. It changes no pixel: no layer holds it, no export knows it exists.

In the **References** panel (`Shift+R`; it floats, opened whenever one is added):

- Place it by the numbers, press **Fit**, or hold `Alt` and drag it on the canvas.
- Choose **behind** or **in front of** the artwork, its opacity, and lock it so dragging the canvas will not move it.
- **Show part of an image**: a sprite sheet is usually wanted for one cell of it. When importing, drag on the
  preview, type x y w h, or give a cell size and a gap and click a cell; **< Cell** and **Cell >** walk the sheet.
  The whole image stays in the file, so another part can be shown later.

A reference **travels inside the document**, so moving the work to another machine does not break it. Each is
capped at 8 MB and the document's at 16 MB, scaled down on import and stored as PNG, so a photograph cannot
quietly outweigh the artwork.

**Edit > Paste as reference** makes the image on your clipboard a reference.

## The library

**File > Library...** (`Ctrl+L`) is two folders:

- **Sprites**: the folder your work lives in, listed with a picture of each file. One click opens it (asking
  about unsaved changes the way every other route does).
- **References**: a folder of images to draw from. One click brings one into the document.

A project is just a folder: no project file, nothing to corrupt, nothing to keep in step. One you made in
Explorer already works. Fast remembers only which folder, in its settings, never in the artwork.

## Import a sprite sheet

**File > Import sprite sheet...** slices one image on a grid into frames. It guesses the grid from the sheet's
shape and says how many frames each guess makes before anything is replaced.
