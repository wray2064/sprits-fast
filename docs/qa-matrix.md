# QA matrix

Made by `python tools/qa_matrix.py` from the code: do not edit by hand. It says what the tests
*touch*, not that they check everything: it is the list of what **no test touches at all**, and
each gap gets a script or a test case until the list is empty (or the item is explained).

- **Menu items:** 87 of 88 are clicked by a UI script (1 more cannot be, and say why below).
- **Commands:** 71 of 71 are chosen, pressed or run by a script or a core test.
- **Core modules:** 52 of 52 have a core test.

## Gaps

**Menu items no script clicks**

- (none)

**Menu items a script cannot click, and why**

- Edit > Paste as reference: enabled only when the system clipboard holds a picture; a script run keeps its own clipboard, so there is none to paste

**Commands no script or core test touches**

- (none)

**Core modules with no test of their own**

- (none)

## Menus

| Menu item | Scripts |
|---|---|
| File | canvas_fit.txt, canvas_fit.txt, keys_more.txt +34 |
| File > New... | menu_file.txt |
| File > Autosave | menu_file.txt, menu_file.txt, menu_file.txt +4 |
| File > Autosave > On | menu_file.txt, menu_file.txt |
| File > Autosave > Write a copy now | menu_file.txt |
| File > Library... | menu_file.txt |
| File > Import reference... | menu_file.txt |
| File > Import sprite sheet... | menu_file.txt |
| File > Open... | menu_file.txt |
| File > Open recent | menu_file.txt, menu_file.txt |
| File > Open recent > Clear | menu_file.txt |
| File > Start screen | canvas_fit.txt, canvas_fit.txt, keys_more.txt +1 |
| File > Save | menu_file.txt, menu_file.txt |
| File > Save as... | menu_file.txt |
| File > Save a copy... | menu_file.txt |
| File > Export PNG | menu_file.txt, menu_file.txt, menu_file.txt +5 |
| File > Export PNG > Indexed, the palette as its table | menu_file.txt, menu_file.txt |
| File > Export sheet... | menu_file.txt |
| File > Export animation... | menu_file.txt |
| File > Export again | menu_file.txt |
| File > Close tab | menu_file.txt, quit.txt |
| File > Quit | quit.txt, quit.txt |
| Edit | menu_edit.txt, menu_edit.txt, menu_edit.txt +12 |
| Edit > History... | menu_edit.txt |
| Edit > Preferences... | appearance.txt, escape_closes.txt, extension_roles.txt +2 |
| Edit > Cut | menu_edit.txt |
| Edit > Copy | menu_edit.txt |
| Edit > Paste | menu_edit.txt |
| Edit > Paste as new layer | menu_edit.txt |
| Edit > Paste as reference | **none** |
| Edit > Delete | menu_edit.txt |
| Edit > Fill selection | menu_edit.txt |
| Edit > Stroke selection | menu_edit.txt |
| Edit > Brush from selection | menu_edit.txt |
| Edit > Flip horizontally | menu_edit.txt |
| Edit > Flip vertically | menu_edit.txt |
| Edit > Rotate 90 clockwise | menu_edit.txt |
| Edit > Rotate 90 anticlockwise | menu_edit.txt |
| Edit > Rotate 180 | menu_edit.txt |
| Edit > Rotate freely | menu_edit.txt |
| Edit > Scale freely | scale_freely.txt |
| Sprite | menu_sprite.txt, menu_sprite.txt, menu_sprite.txt +19 |
| Sprite > New tilemap layer... | tilemap.txt |
| Sprite > Canvas size... | menu_sprite.txt |
| Sprite > Crop to selection | menu_sprite.txt |
| Sprite > Trim | menu_sprite.txt, menu_sprite.txt |
| Sprite > Same layers in every frame | menu_sprite.txt, menu_sprite.txt |
| Sprite > Adjust colours... | menu_sprite.txt, menu_sprite.txt, menu_sprite.txt |
| Sprite > Sprite size... | menu_sprite.txt |
| Sprite > Enlarge | menu_sprite.txt, menu_sprite.txt, menu_sprite.txt +1 |
| Sprite > Reduce | menu_sprite.txt, menu_sprite.txt, menu_sprite.txt |
| Sprite > Rotate canvas 90 clockwise | menu_sprite.txt |
| Sprite > Rotate canvas 90 anticlockwise | menu_sprite.txt |
| Sprite > Rotate canvas 180 | menu_sprite.txt |
| Sprite > Flip canvas horizontally | menu_sprite.txt |
| Sprite > Flip canvas vertically | menu_sprite.txt |
| Select | menu_select.txt, menu_select.txt, menu_select.txt +6 |
| Select > All | menu_select.txt |
| Select > Deselect | menu_select.txt, menu_select.txt |
| Select > Reselect | menu_select.txt |
| Select > Invert | menu_select.txt, menu_select.txt |
| Select > Modify | menu_select.txt, menu_select.txt, menu_select.txt |
| View | guides.txt, guides.txt, guides.txt +27 |
| View > Zoom in | menu_view.txt |
| View > Zoom out | menu_view.txt |
| View > Fit to window | menu_view.txt |
| View > Reset view | menu_view.txt |
| View > Pixel grid | menu_view.txt, menu_view.txt |
| View > Rulers | menu_view.txt, menu_view.txt |
| View > Guides | guides.txt, guides.txt, menu_view.txt +1 |
| View > Clear guides | guides.txt |
| View > Slices... | menu_view.txt |
| View > Snap to grid | menu_view.txt, menu_view.txt |
| View > Tile grid | menu_view.txt, menu_view.txt, menu_view.txt +1 |
| View > Tile grid > Show | menu_view.txt, menu_view.txt |
| View > Tile grid > Isometric | menu_view.txt, menu_view.txt |
| View > Tiled mode | menu_view.txt, menu_view.txt, menu_view.txt +1 |
| View > Symmetry across | menu_view.txt, menu_view.txt |
| View > Symmetry down | menu_view.txt, menu_view.txt |
| View > Preview | menu_view.txt, menu_view.txt |
| Window | menu_window.txt, menu_window.txt, menu_window.txt +7 |
| Window > Pin the timeline | menu_window.txt, menu_window.txt |
| Window > Preview | menu_window.txt, menu_window.txt |
| Window > Interface kit | kit.txt |
| Window > Layouts | menu_window.txt, menu_window.txt, menu_window.txt |
| Window > Layouts > Save current layout... | layouts.txt |
| Window > Layouts > Delete a saved layout | layouts.txt |
| Window > Reset layout | menu_window.txt |

## Commands

| Group | Command | Label | Scripts | Core tests |
|---|---|---|---|---|
| Tools | `tool.pencil` | Pencil | cel_opacity.txt, command_search.txt, custom_brush.txt +21 | keymap_tests.cpp |
| Tools | `tool.spray` | Spray | fill_and_keys.txt, panels_fit.txt | none |
| Tools | `tool.eraser` | Eraser | cel_opacity.txt, command_search.txt, copy_shapes.txt +26 | keymap_tests.cpp |
| Tools | `tool.bucket` | Fill | fill_and_keys.txt, panels_and_menus.txt, panels_fit.txt +2 | none |
| Tools | `tool.gradient` | Gradient | fill_and_keys.txt, panels_fit.txt, rotate_and_fill.txt | none |
| Tools | `tool.picker` | Pick colour | fill_and_keys.txt | none |
| Tools | `tool.rectangle` | Rectangle | copy_shapes.txt, edit_and_view.txt, panels_fit.txt +2 | none |
| Tools | `tool.ellipse` | Ellipse | command_search.txt, shapes.txt | none |
| Tools | `tool.line` | Line | gizmo.txt, shapes.txt | none |
| Tools | `tool.contour` | Contour | paths_and_text.txt, selection.txt | none |
| Tools | `tool.text` | Text | paths_and_text.txt | none |
| Tools | `tool.select` | Select | copy_shapes.txt, custom_brush.txt, edit_and_view.txt +10 | none |
| Tools | `tool.select-ellipse` | Select ellipse | selection.txt | none |
| Tools | `tool.lasso` | Lasso | selection.txt | none |
| Tools | `tool.polygon-lasso` | Polygon lasso | paths_and_text.txt | none |
| Tools | `tool.polygon` | Polygon | paths_and_text.txt | none |
| Tools | `tool.curve` | Curve | paths_and_text.txt | none |
| Tools | `tool.slice` | Slice | canvas_fit.txt, cel_opacity.txt, command_search.txt +32 | none |
| Tools | `tool.wand` | Magic wand | panels_fit.txt, selection.txt | none |
| Tools | `tool.move` | Move | copy_shapes.txt, edit_and_view.txt, fill_and_keys.txt +6 | none |
| Tools | `tool.hand` | Hand | start_screen.txt | none |
| Tools | `tool.zoom` | Zoom | edit_and_view.txt | none |
| Brush | `brush.bigger` | Brush larger | edit_and_view.txt, keys_more.txt | none |
| Brush | `brush.smaller` | Brush smaller | keys_more.txt | none |
| Brush | `colour.swap` | Swap the two colours | help.txt | none |
| File | `file.new` | New | element_and_new.txt, start_screen.txt, tilemap.txt | none |
| File | `file.open` | Open | kit.txt, menu_file.txt | none |
| File | `file.save` | Save | appearance.txt, command_search.txt, export_again.txt +4 | none |
| File | `file.save-as` | Save as | appearance.txt, extension_roles.txt | none |
| File | `file.library` | Library | keys_more.txt, menu_file.txt | none |
| File | `file.export-again` | Export again | export_again.txt | none |
| Help | `app.search` | Search commands | command_search.txt | none |
| Help | `app.guide` | User guide | start_welcome.txt, user_guide.txt, user_guide_es.txt | none |
| File | `file.close` | Close tab | keys_more.txt, menu_file.txt, quit.txt | none |
| File | `file.quit` | Quit | quit.txt | none |
| Edit | `edit.undo` | Undo | edit_and_view.txt, element_and_new.txt, fill_and_keys.txt +11 | keymap_tests.cpp |
| Edit | `edit.redo` | Redo | pencil.txt, scale_freely.txt | none |
| Edit | `edit.cut` | Cut | copy_shapes.txt | none |
| Edit | `edit.copy` | Copy | copy_shapes.txt, edit_and_view.txt, keys_more.txt | none |
| Edit | `edit.paste` | Paste | copy_shapes.txt, edit_and_view.txt | none |
| Edit | `edit.paste-layer` | Paste as new layer | keys_more.txt, menu_edit.txt | none |
| Edit | `edit.brush` | Brush from selection | custom_brush.txt | none |
| Edit | `edit.fill` | Fill the selection | menu_edit.txt | none |
| Edit | `edit.stroke` | Stroke the selection | menu_edit.txt | none |
| Edit | `selection.flip-h` | Flip horizontally | edit_and_view.txt | none |
| Edit | `selection.flip-v` | Flip vertically | keys_more.txt, menu_edit.txt | none |
| Select | `select.all` | Select all | cel_opacity.txt, element_and_new.txt, gizmo.txt +5 | none |
| Select | `select.none` | Deselect | custom_brush.txt, edit_and_view.txt, keys_more.txt +4 | none |
| Select | `select.invert` | Invert selection | keys_more.txt, menu_select.txt | none |
| Layers | `layer.duplicate` | Duplicate layer | fill_and_keys.txt | none |
| Layers | `layer.merge` | Merge down | keys_more.txt | none |
| Layers | `layer.group` | Group layers | keys_more.txt | none |
| Layers | `layer.ungroup` | Ungroup | keys_more.txt | none |
| Layers | `layer.raise` | Raise layer | keys_more.txt | none |
| Layers | `layer.lower` | Lower layer | keys_more.txt | none |
| Palette | `palette.swap` | Next palette | keys_more.txt | none |
| Animation | `anim.play` | Play or stop | cel_opacity.txt, command_search.txt, copy_shapes.txt +11 | none |
| Animation | `anim.previous` | Previous frame | keys_more.txt | none |
| Animation | `anim.next` | Next frame | none | keymap_tests.cpp |
| Animation | `anim.onion` | Onion skin | keys_more.txt | none |
| Animation | `anim.duplicate` | Duplicate frame | fill_and_keys.txt, frame_menu.txt | none |
| View | `view.timeline` | Timeline | frame_menu.txt, keys_more.txt, panels_and_menus.txt +5 | none |
| View | `view.preview` | Preview | canvas_fit.txt, keys_more.txt, menu_view.txt | none |
| View | `view.references` | References | panels_dock.txt | none |
| View | `view.reset-layout` | Reset layout | layouts.txt, panels_dock.txt | none |
| View | `view.zoom-in` | Zoom in | edit_and_view.txt | none |
| View | `view.zoom-out` | Zoom out | keys_more.txt, menu_view.txt | none |
| View | `view.fit` | Fit to window | canvas_fit.txt, edit_and_view.txt, frame_menu.txt +14 | none |
| View | `view.snap` | Snap to grid | keys_more.txt, menu_view.txt | none |
| View | `view.next-tab` | Next tab | keys_more.txt | none |
| View | `view.previous-tab` | Previous tab | keys_more.txt | none |

## Core modules

| Module | Tests |
|---|---|
| `adjust` | adjust_tests.cpp |
| `animation` | anim_export_tests.cpp, animation_tests.cpp, aseprite_tests.cpp, batch_tests.cpp, canvas_ops_tests.cpp, import_tests.cpp, layers_tests.cpp, palette_tests.cpp, puppet_tests.cpp, sheet_tests.cpp, slices_tests.cpp, system_tests.cpp, tilemap_tests.cpp, tracks_tests.cpp |
| `batch` | batch_tests.cpp |
| `brush` | brush_tests.cpp |
| `bucket` | bucket_tests.cpp, turning_tests.cpp |
| `canvas_ops` | canvas_ops_tests.cpp, puppet_tests.cpp, slices_tests.cpp, text_tests.cpp, tilemap_tests.cpp, turning_tests.cpp |
| `clip_image` | clip_image_tests.cpp |
| `dither` | adjust_tests.cpp, dither_tests.cpp, palette_io_tests.cpp, palette_tests.cpp, sheet_tests.cpp, system_tests.cpp, turning_tests.cpp |
| `document` | adjust_tests.cpp, anim_export_tests.cpp, animation_tests.cpp, aseprite_tests.cpp, batch_tests.cpp, canvas_ops_tests.cpp, clip_image_tests.cpp, document_tests.cpp, element_tests.cpp, file_io_tests.cpp, import_tests.cpp, ink_tests.cpp, layers_tests.cpp, palette_io_tests.cpp, palette_tools_tests.cpp, puppet_tests.cpp, recovery_tests.cpp, reference_tests.cpp, selection_tests.cpp, sheet_tests.cpp, slices_tests.cpp, system_tests.cpp, templates_tests.cpp, text_tests.cpp, tilemap_tests.cpp, tracks_tests.cpp |
| `element` | adjust_tests.cpp, aseprite_tests.cpp, canvas_ops_tests.cpp, clip_image_tests.cpp, dither_tests.cpp, element_tests.cpp, import_tests.cpp, ink_tests.cpp, palette_tools_tests.cpp, selection_tests.cpp, shape_tests.cpp, text_tests.cpp, turning_tests.cpp |
| `export_anim` | anim_export_tests.cpp |
| `export_png` | export_tests.cpp, reference_tests.cpp |
| `file_io` | anim_export_tests.cpp, batch_tests.cpp, element_tests.cpp, export_tests.cpp, file_io_tests.cpp, i18n_tests.cpp, ink_tests.cpp, layers_tests.cpp, palette_io_tests.cpp, recent_files_tests.cpp, recovery_tests.cpp, reference_tests.cpp, shape_tests.cpp, sheet_tests.cpp, slices_tests.cpp, system_tests.cpp, text_tests.cpp, theme_store_tests.cpp, tracks_tests.cpp, vector_icon_tests.cpp |
| `floating` | clip_image_tests.cpp, ink_tests.cpp, selection_tests.cpp |
| `grid_snap` | grid_snap_tests.cpp |
| `guide` | guide_tests.cpp |
| `guides` | slices_tests.cpp |
| `i18n` | i18n_tests.cpp |
| `image_io` | anim_export_tests.cpp, batch_tests.cpp, clip_image_tests.cpp, export_tests.cpp, import_tests.cpp, palette_tools_tests.cpp, recovery_tests.cpp, reference_tests.cpp |
| `import_aseprite` | aseprite_tests.cpp |
| `import_image` | import_tests.cpp, turning_tests.cpp |
| `ink` | adjust_tests.cpp, anim_export_tests.cpp, batch_tests.cpp, canvas_ops_tests.cpp, clip_image_tests.cpp, import_tests.cpp, ink_tests.cpp, layers_tests.cpp, palette_tools_tests.cpp, selection_tests.cpp, text_tests.cpp, tilemap_tests.cpp, tracks_tests.cpp, transform_tests.cpp, turning_tests.cpp |
| `keymap` | keymap_tests.cpp |
| `layers` | aseprite_tests.cpp, layers_tests.cpp, puppet_tests.cpp, tilemap_tests.cpp, tracks_tests.cpp |
| `library` | recovery_tests.cpp, reference_tests.cpp |
| `paint` | adjust_tests.cpp, anim_export_tests.cpp, animation_tests.cpp, batch_tests.cpp, canvas_ops_tests.cpp, clip_image_tests.cpp, dither_tests.cpp, element_tests.cpp, export_tests.cpp, ink_tests.cpp, layers_tests.cpp, paint_tests.cpp, palette_io_tests.cpp, palette_tools_tests.cpp, puppet_tests.cpp, recovery_tests.cpp, reference_tests.cpp, robustness_tests.cpp, selection_tests.cpp, sheet_tests.cpp, system_tests.cpp, text_tests.cpp, tilemap_tests.cpp, tracks_tests.cpp, transform_tests.cpp, turning_tests.cpp |
| `palette` | adjust_tests.cpp, aseprite_tests.cpp, clip_image_tests.cpp, element_tests.cpp, import_tests.cpp, ink_tests.cpp, palette_io_tests.cpp, palette_tests.cpp, palette_tools_tests.cpp, selection_tests.cpp, shape_tests.cpp, system_tests.cpp, text_tests.cpp |
| `palette_io` | palette_io_tests.cpp, palette_tools_tests.cpp, system_tests.cpp |
| `palette_tools` | palette_tools_tests.cpp, templates_tests.cpp |
| `pixel_font` | text_tests.cpp |
| `preferences` | preferences_tests.cpp, recent_colours_tests.cpp |
| `puppet` | puppet_tests.cpp |
| `recent_colours` | recent_colours_tests.cpp |
| `recent_files` | recent_files_tests.cpp |
| `recovery` | recovery_tests.cpp |
| `reference` | recovery_tests.cpp, reference_tests.cpp |
| `selection` | clip_image_tests.cpp, ink_tests.cpp, selection_tests.cpp |
| `shape` | animation_tests.cpp, canvas_ops_tests.cpp, element_tests.cpp, ink_tests.cpp, layers_tests.cpp, palette_io_tests.cpp, puppet_tests.cpp, selection_tests.cpp, shape_tests.cpp, sheet_tests.cpp, system_tests.cpp, turning_tests.cpp |
| `sheet` | sheet_tests.cpp, slices_tests.cpp, system_tests.cpp |
| `slices` | slices_tests.cpp |
| `templates` | templates_tests.cpp |
| `text` | text_tests.cpp, text_util_tests.cpp |
| `text_util` | text_util_tests.cpp |
| `theme_store` | theme_store_tests.cpp |
| `theme_tokens` | preferences_tests.cpp, theme_extension_tests.cpp, theme_tokens_tests.cpp |
| `tilemap` | aseprite_tests.cpp, tilemap_tests.cpp |
| `tips` | templates_tests.cpp |
| `tracks` | puppet_tests.cpp, tracks_tests.cpp |
| `transform` | bucket_tests.cpp, canvas_ops_tests.cpp, dither_tests.cpp, palette_tests.cpp, selection_tests.cpp, tracks_tests.cpp, transform_tests.cpp, turning_tests.cpp |
| `ui_state` | ui_state_tests.cpp |
| `vector_icon` | vector_icon_tests.cpp |
| `webp` | webp_tests.cpp |
