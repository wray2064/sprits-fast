# The UI overhaul

Status: **approved 2026-10-18; building in stages.** Section 5 records the
decisions. Progress:

- [x] **Stage 1, foundation.** `app/theme_tokens` (38 roles, the `.lstheme`
      file format, eight built-in themes, derived accent roles, WCAG contrast
      checker) with the `fast_theme_tokens` suite, which fails if any built-in
      theme misses a target. The window's colours now come from the theme in
      force; Preferences > Look lists every theme; `view.theme` keeps the id.
      Panels still carry their own literals until stage 3, so a few elements
      (the toolbar, some borders) do not yet follow a theme.
- [x] **Stage 2, component kit, typography, density, the font.** Inter
      (Regular and SemiBold, OFL, `assets/fonts/`) is compiled in; the type
      scale (caption 12, body 14, title 17, display 22) is `theme::pushText`;
      three densities (`view.density`, Preferences > Interface); `ui/widgets.h`
      has buttons (primary, secondary, ghost, danger), the switch, the
      segmented control, the label-left `field` row, badges, banners, key caps,
      the empty state and the dialog frame, and `Window > Interface kit` (or
      `--kit`) draws every one in every state for review. File > New is the
      first dialog on the new frame; the rest move in stage 3.
- [ ] Stage 3, panel by panel. Done so far: **3a, the chrome** -- the tool
      rail (four groups with hairlines, a button that follows the font and
      density, tooltips with key caps, secondary-text icons that light to the
      accent), the status bar (caption type, an accent dot for unsaved work,
      a right-hand group that is measured, not given 580 px), and the canvas's
      origin on a whole screen pixel. **3b, the controls every panel shares**
      (which is most of the Tool and Palette panels): the section heading is a
      small semibold label in secondary text (it was amber, putting the accent
      on every heading), every slider is the kit's (a well, a fill up to the
      value, a thin handle, the value written in it), a hint is a circled
      question mark that brightens, a hint no longer pushes its row off the
      panel, palette swatches follow the font, and the palette has SHADES,
      RECENT and PALETTE headings. **3c, the frame and the layout:** every
      control has a hairline in the border colour (a control that is a fill a
      few percent off the panel cannot be seen in the light themes), except
      the tool rail, headings and ghost buttons; the default layout gives the
      sidebars 276 px and the Palette more height (Tool 40%, Palette 60%;
      Layers 30%) so the palette no longer needs a scroll. A saved layout is
      kept, so Window > Reset layout shows the new one. **3d, the timeline:** Play
      is the primary (Stop an ordinary button), Delete is a danger button,
      Hold and Speed have labels, Onion is a switch, the totals are muted, and
      the controls wrap onto a second row instead of running into each other
      when the panel is narrow; the default timeline is taller (214 px).
      **3e, the dialogs:** every modal in the program is on the one frame
      (`widgets::beginModal`, a drop-in for BeginPopupModal: padding, the
      title above a hairline, Escape), and About is a real kit dialog with the
      program's name in the display size. Their footers, still written the
      older way, move onto `dialogButtons` as each dialog is next touched.
      Stage 3 is therefore done except those footers and Preferences'
      labels-to-the-left rows, which wait for stage 4 (the Appearance tab is
      rebuilt then).
- [x] **Stage 4, Appearance in Preferences.** A tab beside General and Keys
      holds the theme chooser (built-in and the person's own, dark first), nine
      accent presets and a free picker (the accent is moved only as far as it
      must be to be seen and to carry text: `applyAccent`), the interface size
      and density, a readability panel that names each pair that misses its
      target with its numbers and a *Fix them* button (`fixContrast`), and every
      role in seven groups with a colour picker, its hex, a reset to the base
      theme and a danger badge where it fails. Every change shows at once on the
      real window and is held as an unsaved working copy until it is saved as a
      theme (`<settings>/themes/<id>.lstheme`), updated, discarded, exported or
      deleted; a theme can be imported from a file. Built-ins are never written
      over. Canvas colours that were still the old theme's follow the new one.
      The store (`app/theme_store`) is bounded and defensive; a scripted run
      keeps its themes in `ui_selftest_themes`, not the person's settings.
- [x] **Stage 5, icons and named layouts.** The icon set is **Lucide 1.59.0**
      (ISC; MIT for the few it took from Feather): 24 SVGs, 13 KB with the
      licence, in `assets/icons/lucide` with a README of sources and hashes,
      and a NOTICE entry. They are compiled in and parsed by `app/vector_icon`
      (paths with arcs and curves, lines, polylines, polygons, rects, circles,
      ellipses) into strokes, then drawn by the one `theme::drawIcon` call as
      round-capped polylines -- vectors at every size, never bitmaps -- so the
      hand-drawn placeholders are gone. Layouts: Window > Layouts builds three
      arrangements itself (Drawing, Animation, Compact, with `DockBuilder`) and
      keeps the person's own under a name (`<settings>/layouts/<id>.ini`, ImGui's
      own layout text with which panels are open): Save current layout, choose
      one to put it back, Delete a saved layout. A saved layout is read only as
      a layout -- bounded to 32 and 64 KiB, by a plain id that is a file name --
      and goes in between frames. A scripted run keeps them in
      `ui_selftest_layouts`. `tests/ui/layouts.txt` saves, switches away,
      restores and deletes, checking panel heights come back as saved.
- [ ] Stage 6, the Pract seam, `visual-language.md`, the gallery.

Why this is critical: Pract is Fast's window run as a library. Everything
Pract draws -- its timeline, References, Arranger, Practipuppets -- is built
from Fast's widgets, colours and spacing. A visual language made once here is
Pract's for free; a patchwork here is a patchwork multiplied there.

## 1. What is wrong now (audited 2026-10-18, from screenshots and the code)

**Readability**
- Text is one weight and, for labels, one dim grey. Disabled buttons (Delete,
  Reverse, Tween), hints, the status bar and section headers are near the
  floor of legibility; none has been measured against a contrast target.
- Controls are often unlabelled or labelled by position alone: the timeline's
  "100 ms" and "1.00x", the orange slider bars with a number in the middle, the
  bare `x` and `v` buttons.
- Labels sit to the right of a control in Preferences and to the left in
  Properties. Sliders, checkboxes and combos each look like a different
  family.
- Fixed pixel widths clip: "size 1 ?" and the hint paragraphs in the Tool
  panel are cut off at the default width, and several panels break past 150%.

**Hierarchy**
- Every button is the same weight, so the one that matters (Create, Open,
  Export) does not read as the one that matters.
- Panel titles, section headers and labels are three near-identical small
  sizes. Nothing says what is a group, a row or a value.
- The default layout stacks Tool and Palette into a narrow column that must
  scroll, while the canvas column has room to spare.

**Consistency**
- Dialogs (New document, Preferences, About, Shortcuts, Start) each pick their
  own padding, width and button placement.
- 67 `IM_COL32` and 17 `ImVec4` literals, 166 `ImVec2` literals and 61 fixed
  `SetNextItemWidth` values sit in the panels outside the theme, so a change of
  look cannot happen in one place, which is what `theme.h` says it should.
- Icons are placeholders drawn from lines and rectangles (`icons-needed.md`).

**Themes and customisation**
- Two fixed palettes (Slate, Paper), 15 colours each, and one fixed accent.
  No user colours, no import/export, no high-contrast theme, no density
  choice, and nothing Pract could extend without editing Fast's source.
- The font is whatever the platform ships (Segoe UI here, DejaVu or the ImGui
  bitmap font elsewhere), so the program looks different on every machine.

## 2. The design

### 2.1 Principles
1. **The canvas is the subject.** Chrome recedes; colour is rationed to state
   (selected, focused, primary, danger). The accent stays the only saturated
   colour in the chrome.
2. **Readable before pretty.** Contrast is a tested number, not a taste: text
   4.5:1, controls and icons 3:1, in *every* theme, checked by a test.
3. **One place for every decision.** No colour, size or spacing literal in a
   panel: it names a token or uses a component.
4. **A person can change it, Pract can extend it.** Themes are text files;
   Pract adds tokens and themes through the same seam, not by patching Fast.
5. **Fixed layout that bends.** Widths come from the font and the density, so
   a larger interface or a longer Spanish label never clips.

### 2.2 Tokens (`ui/tokens.h`)
Semantic, not literal -- named for what they are for.
- **Surfaces**, four elevations: `app` (the window), `panel`, `raised`
  (popups, menus, dialogs), `canvas`; plus `inset` (input wells).
- **Text**: `primary`, `secondary`, `muted`, `disabled`, `onAccent`.
- **Lines**: `separator` (hairline), `border`, `focus` (a ring that is always
  visible to the keyboard).
- **Controls**: `control`, `hover`, `pressed`, `selected`, `selectedText`.
- **Accent**: `accent`, `accentHover`, `accentPressed`, `accentSubtle`.
- **State**: `success`, `warning`, `danger`, `info`, each with a `subtle`
  background for banners and badges.
- **Canvas aids**: `checkerA`, `checkerB`, `gridLine`, `selectionAnts`,
  `guide`, `onion` tints -- the colours drawn *over* the work.
- **Scales**: spacing on a 4 px step (`s1..s8`), radii (`none/sm/md/lg`),
  type (`caption 12`, `body 14`, `strong 14 bold`, `title 17`, `display 22`),
  control heights per density, icon sizes (`12, 16, 20, 24`).
- **Density**: Compact, Comfortable (default), Spacious: one multiplier on
  control height and gap, independent of the interface size.

### 2.3 Themes and custom colours
- A theme is a small text file (`*.lstheme`): a name, whether it is light or
  dark, a base theme to inherit from, and `token = #rrggbb[aa]` lines. Read
  defensively like every file here (unknown tokens skipped, bad colours
  refused, bounded).
- **Built in:** Slate (dark, today's), Paper (light, today's), and the ones
  chosen in section 5, plus **High contrast** dark and light.
- **Custom colours** in Preferences > Appearance: pick a base theme, change
  any token or just the accent (a handful of accent presets plus a free
  picker; the hover/pressed/subtle variants are derived), see it applied live,
  save it as a theme, import and export theme files. A **reset** per token and
  for the whole theme.
- **Safety net:** when a custom theme fails the contrast check the editor says
  which pair and by how much, and offers the nearest passing value -- it warns,
  it never forbids.
- **Pract:** `AppExtension` gains a way to register extra tokens (Pract's
  timeline track colours, its reference tints) with defaults per base theme,
  and extra built-in themes, so a Fast theme file styles Pract's windows too.

### 2.4 Typography
- One family, two weights (regular, semibold), a monospace for numbers and hex
  where alignment matters. Sizes from the scale above; line height 1.4.
- Decision needed (section 5): bundle an open-licence font so the program
  looks the same everywhere and Spanish and other accents are always covered,
  or keep the system face.
- Numbers in tables and fields use tabular figures; units are muted, values
  primary.

### 2.5 Components (`ui/widgets.h`)
A small kit every panel uses, state-aware (rest, hover, pressed, focus,
selected, disabled, error), each with a tooltip slot and a script name:
- Buttons: **primary** (accent fill, one per surface), secondary (default),
  ghost (toolbars), danger; icon-only buttons always with a tooltip.
- Toggle (switch) and checkbox; segmented control (for 1x/2x/3x, round/square);
  radio group.
- Field row: label left (fixed column), control right, unit and help after;
  sliders show value and unit and accept typing; steppers; colour swatch with
  hex; combo.
- Section (header + hairline, collapsible), panel header, property table,
  list row (selected, hover, trailing actions), tab, chip, badge, banner,
  empty state, key-cap for shortcuts.
- Dialog frame: title, body, footer with the primary on the right and Cancel
  beside it; one width scale (S/M/L); the same padding everywhere.
- Status bar and toolbar with consistent item rhythm.

### 2.6 Layout
- A default dock layout designed for the work: tools on the left rail (icons
  only, with a tooltip and the key), Palette and Layers on one side where they
  have room, Properties/Transform below Elements, Timeline under the canvas.
- Panel minimum widths from the font, so nothing clips; a panel narrower than
  its minimum scrolls horizontally instead of cutting a label.
- Saved layouts by name (Drawing, Animation, Compact) and Reset layout.

### 2.7 Icons
Replace the drawn placeholders with one set, monochrome alpha masks, 16 px
grid, one stroke weight, tinted by the theme (as `icons-needed.md` already
specifies). Licence must impose nothing on a fork (CC0, MIT, Apache-2.0 or
commissioned). Until a set is chosen the drawn icons stay, but go through the
same `icon()` call so swapping is one change.

### 2.8 Accessibility
Contrast targets above; a focus ring on every focusable control; full
keyboard reach (Tab order, Enter/Space, arrows in lists); no information by
colour alone (state also has shape or text); a reduce-motion setting (there
are no animations to remove today, and the rule is that it stays so); the
interface size and density settings.

## 3. How it is built, in stages

Each stage lands on its own (committed, pushed, all tests and scripts green)
so Pract can rebase onto any of them.

1. **Foundation (core, no window).** `tokens`, the theme file format, the
   built-in themes, the contrast checker. Tests: file parse and round trip,
   hostile input, every built-in theme meets the contrast targets, derived
   accent variants stay in range.
2. **Component kit.** `widgets.h` and its UI scripts, built on the tokens;
   `theme.cpp`'s style application moves onto them. Typography and density
   land here (and the font, if bundled).
3. **Panel by panel,** each with a before/after screenshot and a script for
   every interaction it has: Menus and status bar; Tool; Palette; Layers and
   Elements; Properties and Transform; Timeline; the dialogs (New, Preferences,
   About, Shortcuts, Start, Export). The literal count in `panels.cpp` goes to
   zero as each moves.
4. **Appearance in Preferences.** Theme chooser with live preview, accent
   presets, the token editor, density, interface size, import/export, the
   contrast warnings.
5. **Layout and icons.** The designed default layout, named layouts, the icon
   set if one is chosen.
6. **Pract seam and documentation.** The extension API for tokens and themes,
   `docs/visual-language.md` (the tokens, components and when to use each),
   and a gallery script that renders every screen in every built-in theme to
   images, so a change of look is reviewed by eye in one pass.

## 4. How it is checked
- Contrast and token tests in core (stage 1), run on every theme.
- A UI script per interaction, as everywhere; the existing 30 stay green
  throughout (they find widgets by label, so labels change only with their
  script).
- The gallery (every screen x every theme x 100% and 150%) captured and looked
  at before each stage is called done; I will show you the images.
- Spanish catalogue updated with every string, so a longer label is seen to fit.

## 5. Decisions (made 2026-10-18)
1. **Visual direction:** refined pro dark, as designed above.
2. **Font:** bundle an open-licence face (SIL OFL, Inter). The download is made
   at stage 2 and asked for there, naming the file, source and size.
3. **Themes that ship (eight):**
   - dark: **Slate** (today's), **Graphite** (neutral grey, no blue cast, blue
     accent), **Midnight** (deep blue-black), **OLED** (true black app and
     canvas, near-black panels, no large bright areas, so unlit pixels stay
     off), **High contrast dark**;
   - light: **Paper** (today's), **Warm paper** (sepia), **Famicom** (cream
     and crimson, added at the user's request), **High contrast light**.
4. **Accent:** amber stays the default (Graphite and the high-contrast themes
   carry their own), with presets and a free picker in Appearance.
