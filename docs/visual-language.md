# The visual language

How Fast looks, in the terms a screen is built from. The design and its reasons are in
`ui-overhaul.md`; this is the working reference: **what to reach for, and when.**

Four rules, and everything below follows from them:

1. **Name what a colour is for, never the colour.** A panel asks for
   `palette().textMuted`, not for a grey. A theme says what each role is.
2. **Ask for a component, not for a style.** A button is `widgets::button`, a
   labelled row is `widgets::field`. Forty panels look like one program because
   none of them styles anything.
3. **Every pixel count is for 100%.** Multiply a literal size by
   `theme::scale()` (the person's interface size) or take it from the font, or
   it clips at 150%.
4. **Readable is measured, not hoped for.** Text 4.5:1, marks 3:1, in every
   theme, by a test.

## Colour: the roles (`app/theme_tokens.h`, `ui/theme.h`)

A theme is a name, light or dark, and one colour per role -- 38 of them, in
seven groups. The window reads them as `theme::palette()` (the names in
`Palette`) or as `theme::current()[Token::X]`.

| Group | Roles | Use it for |
|---|---|---|
| Surfaces | `App`, `Panel`, `Raised`, `Inset`, `Canvas` | the window and the rail; a panel's background; menus, popups and dialogs (one step above the panel); an input well; the surround of the canvas |
| Text | `TextStrong`, `TextPrimary`, `TextSecondary`, `TextMuted`, `TextDisabled`, `TextOnAccent` | values and what is read first; body text; labels and units; hints and the status bar; what cannot be used; text on an accent fill |
| Lines | `Separator`, `Border`, `Focus` | a hairline between groups; the edge of a control; the ring a keyboard user follows (always drawn) |
| Controls | `Control`, `ControlHover`, `ControlPressed`, `Selected`, `SelectedText` | a control at rest, hovered, pressed; the selected row or tab |
| Accent | `Accent`, `AccentText`, `AccentHover`, `AccentPressed`, `AccentSubtle` | the accent as a fill or an indicator; the accent as *text* (a section label); its states; a tint behind something chosen |
| State | `Success`, `Warning`, `Danger`, `Info` and a `...Subtle` tint of each | a message's colour as text or icon; the wash behind a banner or badge |
| Over the canvas | `CheckerA/B`, `GridLine`, `SelectionLine`, `Guide`, `OnionPast`, `OnionFuture` | what is drawn on the work, which must show on any artwork |

**The accent is the only saturated colour in the chrome.** It marks selection,
focus, the one primary button on a surface, and section labels -- nowhere else.
Do not use `Success` or `Danger` as decoration: they mean something happened.

Use `AccentText` for accent-coloured *text* and `Accent` for fills: on a light
theme they differ, because a fill can be bright where text cannot.

A role is chosen by what the pair must do. If you add text on a surface the
checker does not know (`checkTheme`'s table in `theme_tokens.cpp`), add the pair
to the table, so every theme is held to it.

## Type (`ui/theme.h`)

One family, Inter, in two weights (regular and semibold), bundled so the program
looks the same on every machine. A panel asks for a style, not a size:

```cpp
theme::pushText(theme::TextStyle::Title);   // Caption, Label, Body, Strong, Title, Display
ImGui::TextUnformatted(title);
theme::popText();
```

| Style | Is | For |
|---|---|---|
| Caption | 12 | units, hints, the status bar |
| Label | 12 semibold | a section header (`theme::sectionHeader`, in muted capitals) |
| Body | 14 | everything a widget draws unless told otherwise |
| Strong | 14 semibold | a value read first, a row's name |
| Title | 17 | a dialog's or a panel's title |
| Display | 22 | the program's name on the start screen |

Units are muted and values primary. Section headers are `theme::sectionHeader`
(or `theme::foldingHeader` when the section folds); a hint under a control is
`theme::hint`.

## Space and size

- **Density** (Compact, Comfortable, Spacious) changes the height of a control
  and the gaps, never the type. Ask `theme::metrics()`: `rowHeight`,
  `panelPadding`, `itemSpacing`, `sectionGap`, `sidebarWidth`.
- **Interface size** (75 to 150%) scales text and every spacing, never the
  canvas. A literal in a panel is a literal *for 100%*: write `theme::px(38.f)`
  (what `38.f * theme::scale()` says, as the start screen's `k()` does).
  `theme::metrics()` is already in pixels as drawn, so it may be added to a
  width ImGui has scaled. A width that has to hold a label comes from
  `ImGui::CalcTextSize`, not from a number.
- A control that must fit a longer translation takes its width from its label
  (`widgets::field` sizes its label column from the font;
  `widgets::labelRoom(shown)` is what a control that writes its label after
  itself needs to its right; `widgets::buttonWidth(label)` is a button's own
  width).
- A row of buttons does not get widths in pixels: `widgets::buttonRow(labels, n)`
  shares the row equally and wraps two-and-two when the longest word would not fit
  its share (`widgets::beside(row, i)` before each button), and
  `widgets::sameLineIfFits(width)` keeps the next item on the row only while it
  fits. A panel is as wide as the person made it, and a word as wide as the font
  and the language make it.
- The docks are built for the size: `buildPreset` makes them 276 x the size wide
  (at most 30% of the dock space, so the canvas keeps a share) and gives the
  Layers list and the Timeline more of the height as the size grows, since the
  window is as tall in pixels at 150% as at 100%.
- A panel too narrow for its contents scrolls sideways rather than cutting a
  label: open Fast's own panels with `beginPanel` (`ui/layout.h`), not
  `ImGui::Begin(..., panelFlags())`. ImGui widens the work rect of a window that
  may scroll sideways to the widest thing it held, and a window that always may
  settles into a scrollbar with nothing to scroll; `beginPanel` asks for the bar
  only when the panel overflowed last frame and keeps what fills the width to the
  width that shows.

## Components (`ui/widgets.h`)

Window > Interface kit draws every one in every state, and is where a change to
a component is looked at first.

| Need | Use | When |
|---|---|---|
| The thing to do on a surface | `button(label, ButtonStyle::Primary)` | Create, Open, Export. **One per surface.** |
| An ordinary action | `button(label)` | the default |
| An action in a toolbar or a row | `button(label, ButtonStyle::Ghost)` | no box until hovered |
| An action that loses work | `button(label, ButtonStyle::Danger)` | Delete, Discard |
| On or off *now* | `toggle(label, &on)` | Onion, Round -- not a choice among others |
| A choice made with others | `ImGui::Checkbox` | an option in a list of options |
| One of a few | `segmented(id, &selected, options, n)` | 1x 2x 3x; Compact, Comfortable, Spacious |
| A number | `sliderInt` / `sliderFloat` | shows how far along it is; typing works |
| A labelled row | `field("name")` then the control (`##id`) | every property row: labels line up |
| A short tag | `badge(text, Tone)` | "new", "3 missing", a contrast ratio |
| A message across the width | `banner(Tone, text)` | what is true of the whole panel |
| A key | `keycap(text)` | in a shortcut |
| Nothing to show | `emptyState(title, hint)` | what a list is for and what to do |
| A modal | `beginDialog` ... `dialogButtons` ... `endDialog` | one width scale, one padding, the primary on the right, Escape closes it |

Every component has rest, hover, pressed, focus, selected and disabled; a
disabled control says why (a tooltip). Labels go through `tr()` and are what UI
scripts click by.

A window that is not modal (Preferences, the shortcut list) closes with Escape
when it is focused and nothing is being typed.

## Where a panel lives: docked, or a drawer

Most panels dock (drag a tab to the side of another to dock beside it, onto it to
share its place, out over the canvas to float) and Window > Layouts rearranges
them. The **Timeline** is the one that comes and goes (`ui/timeline_drawer.h`):

- **A drawer** by default. A small tab at the bottom edge of the canvas says which
  frame is showing and whether it plays. Resting the pointer on it opens the strip
  over the bottom of the canvas, and it tucks away when the pointer leaves;
  clicking the tab, or pressing **T**, keeps it open until T, Escape or Hide. It
  takes no room: the canvas does not resize when it comes and goes.
- **Pinned** (the Pin button, or Window > Pin the timeline): an ordinary docked
  panel under the canvas, which takes its height. The Animation layout pins it;
  Drawing and Compact leave it a drawer. Both are kept in the layout file and in
  a saved layout.

A panel that should behave this way takes the same shape: a tab that says enough
to be worth looking at, a short dwell before it opens and a longer one before it
tucks (so a slip of the pointer neither opens nor closes it), stays open while a
field or a drag inside it is in use, and a Pin that makes it an ordinary panel.

## Icons (`theme::drawIcon`)

One set, Lucide (ISC), compiled in as SVG and drawn as strokes, so an icon is
sharp at any size and any colour: `theme::drawIcon(draw, Icon::Pencil, at, size,
colour)`. Take the colour from a role (`TextSecondary` at rest, `Accent` when it
is the chosen tool). Never a bitmap, never an icon font. To add one, see
`assets/icons/lucide/README.md`.

## Themes

- Nine ship: Slate, Graphite, Midnight, OLED, High contrast dark; Paper, Warm
  paper, Famicom, High contrast light. The two high-contrast ones are held to
  7:1 and 4.5:1.
- A theme is a `.lstheme` text file: `name`, `dark`, `enhanced`, `base = slate`
  and `role = #rrggbb[aa]` lines. It starts from a built-in one and says only
  what it changes; a changed `accent` brings its hover, pressed, tint and focus
  with it unless the file sets them. Read defensively: unknown keys are skipped
  and counted, bad colours kept as they were, size and line counts bounded.
- Preferences > Appearance: theme, accent (nine presets and a picker, moved only
  as far as needed to be seen and to carry text), interface size, density, a
  readability panel naming each pair that misses its target with a **Fix them**
  button, and every role with its picker, hex and reset. Edits show at once on
  the real window and are an unsaved working copy until saved as a theme.
- Layouts: Window > Layouts has Drawing, Animation and Compact, and the person's
  own, saved by name.

## How it is checked

- **Core tests**: every built-in theme meets the targets
  (`tests/theme_tokens_tests.cpp`); the file format, hostile input; the icons parse
  (`tests/vector_icon_tests.cpp`).
- **UI scripts** (`tests/ui`): one per interaction, including `appearance`,
  `layouts` and `gallery_smoke`. The script language is in
  `src/ui/ui_script.h`; `for-each theme`, `theme`, `scale` and `shot x.png` are
  what the gallery is made of.
- **The gallery**: `.\tools\gallery.ps1` runs `tests/gallery/gallery.txt` and
  writes every screen in every theme at 100% and 150% (some 270 images), and
  one contact sheet per screen with the nine themes side by side, plus an
  `index.html` to scroll them. **Look at it before calling a visual change
  done.** It is how the start screen's footer and the docked panels were found
  clipped at 150%.
- **Fit at every size** (`tests/ui/panels_fit.txt`): `expect fits all` fails when
  anything in a panel is wider than the panel or a sideways scrollbar has nothing
  to scroll, at 75, 100 and 150% in English and Spanish, in the three layouts and
  with eight tools in hand; `expect scrolls tool` is the other half.
- **Spanish** (`assets/lang/es.txt`) is updated with every string, so a longer
  label is seen to fit.

## Known gaps (from the gallery, 2026-10-18)

- A dock keeps the widths it had when the interface size is changed
  afterwards, and Window > Layouts builds the arrangement again for the new one
  (Preferences > Appearance says so under the size). The panels still show
  everything: one too narrow for its contents scrolls sideways. Changing the
  size does not rebuild a person's own arrangement, which would undo how they
  had moved things.
- A tab group of four (Animation and Compact put Elements, Properties,
  Transform and Puppet in one) scrolls its tab strip at 100% as at 150%: the
  tabs together are wider than a dock, and a strip that scrolls is ImGui's
  answer.
- Dialog footers written the older way (`beginModal`) have not all moved onto
  `dialogButtons`.
