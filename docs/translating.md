# Translating Sprit's'fast

The interface is written in English. Every string that goes through `tr()`
(see `src/app/i18n.h`) is looked up in the catalogue for the language chosen in
*Edit > Preferences > Language*, and a string the catalogue lacks simply stays
English, so a partial translation is a working one.

## A catalogue

A catalogue is a plain UTF-8 text file in `assets/lang/`, named for its
language: `es.txt`, `fr.txt`, `pt-BR.txt`. The build copies the folder beside
the program as `lang/`, which is where Fast looks.

```
# Comments start with a hash.
File = Archivo
Save as... = Guardar como...
Two\nlines = Dos\nlíneas
a \= b = a igual a b
```

One `English = Translation` per line. The English side must be the string as
the code has it, capitals and dots included. `\n` is a line break and `\=` an
equals sign inside either side.

## What is translated so far

- the menu bar and every menu item;
- the headings of every panel section, and the panels' titles;
- the tool names on the toolbar;
- the command names in *Preferences > Keys*.

Buttons, field labels, tooltips and status-bar messages are still English
only. Wrapping them is mechanical -- a literal `"Save"` becomes `tr("Save")` --
and the extraction script used for the Spanish catalogue lists what is wrapped.
Keep new strings translatable by writing them through `tr()` from the start.

## Starting a new language

Copy `assets/lang/es.txt`, rename it, and replace the right-hand side of each
line. Run Fast, choose the language in Preferences; the change is immediate.
