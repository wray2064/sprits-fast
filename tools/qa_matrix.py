# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 the Sprit's'fast authors
"""Writes docs/qa-matrix.md: what the tests cover, found rather than remembered.

    python tools/qa_matrix.py            # rewrite docs/qa-matrix.md
    python tools/qa_matrix.py --check    # exit 1 if it is out of date (for CI)

Three tables, each from the code itself:

  menus      every menu item in src/ui/app_window.cpp, and the UI scripts that
             click it (`click-on LABEL`) -- or `key CHORD` for its shortcut
  commands   every named command in app/keymap.cpp (tools and the rest), and the
             scripts that choose the tool (`tool NAME`), press its key, or run it
  modules    every src/app module and the core test that exercises it

A row with nothing against it is a gap: `docs/qa-matrix.md` lists them at the top,
and tests/ui or tests/ gets a script or a case for each. The matrix says what a
test *touches*, not that it checks everything about it: it is the list of what no
test touches at all.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'docs', 'qa-matrix.md')


def read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def scripts():
    """name -> text, for every UI script."""
    folder = os.path.join(ROOT, 'tests', 'ui')
    return {n: read(os.path.join(folder, n)) for n in sorted(os.listdir(folder)) if n.endswith('.txt')}


def core_tests():
    folder = os.path.join(ROOT, 'tests')
    return {n: read(os.path.join(folder, n)) for n in sorted(os.listdir(folder)) if n.endswith('_tests.cpp')}


def menu_items():
    """[trail] for every named menu item, in the order the menu bar has them: the
    labels from the top menu down, ['File', 'Export PNG', 'Indexed, the palette as its
    table']. Items whose labels are made at run time (the scales, the minutes) are not
    here; the scripts that click them are what covers them."""
    text = read(os.path.join(ROOT, 'src', 'ui', 'app_window.cpp'))
    start = text.index('void drawMenuBar(')
    end = text.index('void drawSheetImportPanel(')
    body = text[start:end]
    items = []
    stack = []
    for match in re.finditer(r'(BeginMenu|MenuItem)\(tr\("([^"]+)"\)|ImGui::EndMenu\(\)', body):
        if match.group(0).startswith('ImGui::EndMenu'):
            if stack:
                stack.pop()
            continue
        kind, label = match.group(1), match.group(2)
        if kind == 'BeginMenu':
            stack.append(label)
            items.append(list(stack))
        else:
            items.append(stack + [label])
    return items


def commands():
    """[(id, label, group, keys)] from the keymap's command list."""
    text = read(os.path.join(ROOT, 'src', 'app', 'keymap.cpp'))
    start = text.index('commandList()')
    body = text[start:text.index('return commands;', start)] if 'return commands;' in text[start:] else text[start:start + 9000]
    out = []
    for m in re.finditer(r'\{\s*"([a-z.\-]+)",\s*"([^"]+)",\s*"([^"]+)",\s*\{(.*?)\}\s*(?:,\s*(?:true|false))?\s*\}', body):
        out.append((m.group(1), m.group(2), m.group(3), m.group(4).strip()))
    return out


def chord_script_form(keys):
    """'ctrl("S", true)' -> 'ctrl+shift+s'; 'key("B")' -> 'b'; 'shift("B")' -> 'shift+b'."""
    forms = []
    for m in re.finditer(r'(key|ctrl|shift)\("([^"]+)"(?:,\s*(true|false))?\)', keys):
        kind, k, flag = m.group(1), m.group(2).lower(), m.group(3)
        if kind == 'ctrl':
            forms.append('ctrl+' + ('shift+' if flag == 'true' else '') + k)
        elif kind == 'shift':
            forms.append('shift+' + k)
        else:
            forms.append(k)
    return forms


def clean(label):
    return label.replace('...', '').replace(' >', '').strip()


def mentions(texts, needles):
    """The scripts (by name) whose text has any of the needles as a script line."""
    found = []
    for name, text in texts.items():
        for line in text.splitlines():
            line = line.split(' #')[0].strip()
            if any(n and n in line for n in needles):
                found.append(name)
                break
    return found


def menu_paths(texts):
    """[(script, [label, ...])] for every `menu A > B > C` line."""
    paths = []
    for name, text in texts.items():
        for line in text.splitlines():
            line = line.strip()
            if line.startswith('menu '):
                labels = [re.sub(r' @\d+$', '', part.strip()) for part in line[5:].split(' > ')]
                paths.append((name, labels))
    return paths


def clicked_in_menu(paths, trail):
    """The scripts with a `menu` path that ends in, or goes through, this trail of labels."""
    found = []
    for name, labels in paths:
        for start in range(len(labels) - len(trail) + 1):
            if labels[start:start + len(trail)] == trail:
                found.append(name)
                break
    return found


# What cannot be reached by a script, and why. A row here is not a gap, it is a
# decision: each needs a reason, and the matrix prints it.
EXPLAINED = {
    'Edit > Paste as reference':
        'enabled only when the system clipboard holds a picture; a script run keeps its '
        'own clipboard, so there is none to paste',
}

# Commands that have a menu item of another name, by command id.
COMMAND_MENU = {
    'edit.fill': ['Edit', 'Fill selection'],
    'edit.stroke': ['Edit', 'Stroke selection'],
    'edit.paste-layer': ['Edit', 'Paste as new layer'],
    'file.library': ['File', 'Library...'],
    'file.close': ['File', 'Close tab'],
    'file.quit': ['File', 'Quit'],
    'select.invert': ['Select', 'Invert'],
    'selection.flip-v': ['Edit', 'Flip vertically'],
    'view.preview': ['View', 'Preview'],
    'view.snap': ['View', 'Snap to grid'],
    'view.zoom-out': ['View', 'Zoom out'],
}


def main():
    ui = scripts()
    tests = core_tests()
    paths = menu_paths(ui)

    rows_menu = []
    for trail in menu_items():
        label = trail[-1]
        # A script clicks a menu item by a `menu` path or, in the older ones, by its
        # own label after the menu's.
        hits = clicked_in_menu(paths, trail)
        if not hits and len(trail) > 1 and len(label) > 3:
            hits = mentions(ui, ['click-on ' + clean(label)])
        rows_menu.append((' > '.join(trail), hits))

    rows_cmd = []
    for ident, label, group, keys in commands():
        needles = []
        if ident.startswith('tool.'):
            needles.append('tool ' + ident.split('.', 1)[1])
        for chord in chord_script_form(keys):
            needles.append('key ' + chord)
        needles.append('click-on ' + clean(label))
        hits = mentions(ui, needles)
        if ident in COMMAND_MENU:
            hits = sorted(set(hits + clicked_in_menu(paths, COMMAND_MENU[ident])))
        # The keymap itself, and what a command does, may be exercised by a core test.
        in_core = [n for n, t in tests.items() if ('"' + ident + '"') in t]
        rows_cmd.append((group, ident, label, keys, hits, in_core))

    modules = sorted(f[:-4] for f in os.listdir(os.path.join(ROOT, 'src', 'app')) if f.endswith('.cpp'))
    rows_mod = []
    for module in modules:
        candidates = [n for n in tests if n == module + '_tests.cpp' or n.startswith(module + '_')]
        # Or any test that includes the module's header.
        included = [n for n, t in tests.items() if ('app/' + module + '.h') in t]
        rows_mod.append((module, sorted(set(candidates + included))))

    gaps_menu = [p for p, h in rows_menu if not h and p not in EXPLAINED]
    gaps_cmd = [f'{i} ({l})' for g, i, l, k, h, c in rows_cmd if not h and not c]
    gaps_mod = [m for m, t in rows_mod if not t]
    explained = [(p, EXPLAINED[p]) for p, h in rows_menu if not h and p in EXPLAINED]

    lines = []
    w = lines.append
    w('# QA matrix')
    w('')
    w('Made by `python tools/qa_matrix.py` from the code: do not edit by hand. It says what the tests')
    w('*touch*, not that they check everything: it is the list of what **no test touches at all**, and')
    w('each gap gets a script or a test case until the list is empty (or the item is explained).')
    w('')
    w(f'- **Menu items:** {len(rows_menu) - len(gaps_menu) - len(explained)} of {len(rows_menu)} are clicked by a UI script'
      + (f' ({len(explained)} more cannot be, and say why below).' if explained else '.'))
    w(f'- **Commands:** {len(rows_cmd) - len(gaps_cmd)} of {len(rows_cmd)} are chosen, pressed or run by a script or a core test.')
    w(f'- **Core modules:** {len(rows_mod) - len(gaps_mod)} of {len(rows_mod)} have a core test.')
    w('')
    w('## Gaps')
    w('')
    w('**Menu items no script clicks**')
    w('')
    for g in gaps_menu or ['(none)']:
        w('- ' + g)
    w('')
    if explained:
        w('**Menu items a script cannot click, and why**')
        w('')
        for p, why in explained:
            w(f'- {p}: {why}')
        w('')
    w('**Commands no script or core test touches**')
    w('')
    for g in gaps_cmd or ['(none)']:
        w('- ' + g)
    w('')
    w('**Core modules with no test of their own**')
    w('')
    for g in gaps_mod or ['(none)']:
        w('- ' + g)
    w('')
    w('## Menus')
    w('')
    w('| Menu item | Scripts |')
    w('|---|---|')
    for path, hits in rows_menu:
        w(f'| {path} | {", ".join(hits[:3]) + (" +%d" % (len(hits) - 3) if len(hits) > 3 else "") if hits else "**none**"} |')
    w('')
    w('## Commands')
    w('')
    w('| Group | Command | Label | Scripts | Core tests |')
    w('|---|---|---|---|---|')
    for group, ident, label, keys, hits, in_core in rows_cmd:
        w(f'| {group} | `{ident}` | {label} | {", ".join(hits[:3]) + (" +%d" % (len(hits) - 3) if len(hits) > 3 else "") if hits else "none"} | {", ".join(in_core) if in_core else "none"} |')
    w('')
    w('## Core modules')
    w('')
    w('| Module | Tests |')
    w('|---|---|')
    for module, t in rows_mod:
        w(f'| `{module}` | {", ".join(t) if t else "**none**"} |')
    w('')

    text = '\n'.join(lines)
    if '--check' in sys.argv:
        current = read(OUT) if os.path.exists(OUT) else ''
        if current != text:
            print('docs/qa-matrix.md is out of date: run python tools/qa_matrix.py')
            return 1
        print('docs/qa-matrix.md is current')
        return 0
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('wrote', os.path.normpath(OUT))
    print('gaps: %d menu items, %d commands, %d modules' % (len(gaps_menu), len(gaps_cmd), len(gaps_mod)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
