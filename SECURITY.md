# Security

Sprit's'fast has no network code, no accounts and no server, so the realistic risks are
in what it **reads**: a `.lsprite`, `.aseprite`, PNG, GIF, JPEG, BMP, palette, theme or
settings file crafted to crash it, hang it or make it misbehave. Every one of those
readers is fuzz-tested under AddressSanitizer (`docs/fuzzing.md`).

## Reporting a problem

If you find a file that crashes or hangs Fast, or any way a file could make it touch
something it should not, please report it:

- **Preferred:** open a private report with GitHub's *Report a vulnerability* button on the
  [Security tab](https://github.com/wray2064/sprits-fast/security/advisories/new).
- Or write to <manictrashpanda@gmail.com> with "Sprit's'fast security" in the subject.

Attach the file that triggers it if you can. A crash with no security impact is still a
bug: a normal [issue](https://github.com/wray2064/sprits-fast/issues) is fine for that.

You can expect an answer within a week. A fix that came from a report goes into the
regression corpus (`tests/fuzz/regressions/`) so it cannot come back.

## Supported versions

The latest release. Fast is small and moves forward; older versions are not patched.
