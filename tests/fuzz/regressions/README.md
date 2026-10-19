# Fuzz regressions

An input that ever crashed, hung or broke an invariant is kept here, in a folder
named for its target (`aseprite/`, `theme/`, ...), under a name that says what it
was (`aseprite/oob-cel-width.bin`). `fast_fuzz_driver --regressions` runs every
one on every test run, so a bug fixed stays fixed.

To reproduce one by hand: `fast_fuzz_driver --target NAME --run FILE`.
