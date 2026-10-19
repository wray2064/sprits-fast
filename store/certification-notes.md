# Notes for certification, age rating, and the checks already run

## Notes for certification (paste into the submission)

Sprit's'fast is a desktop sprite editor (a full-trust Win32 application packaged as MSIX,
which is why it declares the `runFullTrust` capability). It needs no account, no sign-in
and no network: it contains no network code, and its only outbound action is asking
Windows to open a link in the browser when the user clicks one in Help > About.

To test: start the program; a start screen offers templates -- choose any and Create.
Draw with the pencil (key B) on the canvas, add a frame with "+ Frame" in the timeline
(press T to open it), and use File > Save to write a `.lsprite` file anywhere. The
program reads and writes only the files and folders the user picks, plus its own
settings (preferences, themes, recent files and unsaved-work recovery copies) in its
app storage.

Double-clicking a `.lsprite` file opens it in the program (the path arrives as the
first command-line argument).

## Age rating (IARC questionnaire)

Answer **no** to everything: no violence, no blood, no sexual content, no nudity, no
language, no controlled substances, no gambling, no user-generated content shared with
others (the program shares nothing; files stay on the device), no online interaction, no
location sharing, no purchases. Expected rating: **3+ / Everyone**.

## Policies the program satisfies

- **Privacy**: collects no data; policy in `PRIVACY.md`, linked in the listing.
- **Licences**: open source (Apache-2.0) and every third-party component is listed with
  its licence text in `THIRD_PARTY_NOTICES.txt`, shipped in the package.
- **No dependencies on the user's account or services**; nothing downloaded or updated by
  the program itself (the Store updates it).
- **Accessibility**: two high-contrast themes held to 7:1 text contrast, every colour
  pair of every theme checked by tests, the interface scalable to 150%, and a keyboard
  shortcut for every command, all rebindable.
- **Quality**: automatic recovery copies and atomic saves; every file reader fuzz-tested
  under AddressSanitizer (`docs/fuzzing.md`).

## Checks run before submission

See `docs/gold-plan.md` for the current state. Run again at submission time:

| Check | How |
|---|---|
| Package builds and its manifest validates | `tools\make-msix.ps1` (makeappx validates) |
| All tests | `.\build.bat test` (core suites, including a quick fuzz run) |
| Every screen | `.\build-gui.bat`, then each `tests\ui\*.txt`, then `.\tools\gallery.ps1` |
| Fuzzing | `.\tools\fuzz.ps1 -Seconds 600` |
| Windows App Certification Kit | `appcert.exe` (Windows Kits\10\App Certification Kit): needs the package installed on a test machine, which needs a test certificate that machine trusts. Run on a machine you control, after `make-msix.ps1 -TestSign`. Not run by the automated work, which never trusts a certificate. |

## If certification fails

- **Manifest identity mismatch**: the identity values must be exactly those in Partner
  Center (name, publisher, display name).
- **Restricted capability**: `runFullTrust` is allowed for desktop apps; the notes above
  explain why it is declared.
- **App crashes on launch in the test environment**: the runtime DLLs ship in the package;
  if the report names a missing one, add it to `build-release.bat`'s list.
