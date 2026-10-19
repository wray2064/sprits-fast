# Microsoft Store submission kit

Everything the Store needs of Fast, ready. What is *not* here is what only the
publisher can do: the Partner Center account, the reserved name, accepting the
terms, and pressing Submit. This file is the order to do it in.

| In this folder | What it is |
|---|---|
| `AppxManifest.xml.in` | the package manifest, with the identity left to fill in |
| `assets/` | every image the manifest names, at every scale (made by `tools/make_store_assets.py`) |
| `listing/en-us.md`, `listing/es.md` | the listing text, field by field, in both languages |
| `listing/*.png` | app tile icon, box art, poster art, hero art |
| `screenshots/` | six screenshots at 1440 x 900, in upload order (made by the gallery) |
| `certification-notes.md` | what to tell the certifiers, the age rating answers, the checks already run |

## 1. Reserve the name (once)

In [Partner Center](https://partner.microsoft.com/dashboard) create an app and reserve
**Sprit's'fast**. Then, under *Product management > Product identity*, copy three values:

- **Package/Identity/Name** (like `12345YourName.SpritsFast`)
- **Package/Identity/Publisher** (like `CN=XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX`)
- **Package/Properties/PublisherDisplayName** (the name shown on the listing)

## 2. Build the package

From the project folder in PowerShell (it needs the Windows SDK and the Visual Studio
build tools, both already used to build Fast):

```powershell
.\tools\make-msix.ps1 -IdentityName "12345YourName.SpritsFast" `
    -Publisher "CN=XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" `
    -PublisherDisplayName "Your name"
```

That builds the release, lays out the package, makes `resources.pri` and packs
`dist\SpritsFast-1.0.0.0-x64.msix`. The Store signs it, so it needs no certificate of
yours. (To try the package on a machine you control, add `-TestSign`; read what that
does in the script's header first -- it trusts nothing on its own.)

## 3. Submit

*Create a new submission* and fill it in from the files here:

1. **Pricing and availability**: free; all markets; no trial.
2. **Properties**: category *Photo & video* (or *Developer tools*, as you prefer);
   the privacy policy URL, `https://github.com/wray2064/sprits-fast/blob/main/PRIVACY.md`
   (the repository is public, so that address works as it stands).
3. **Age ratings**: the answers are in `certification-notes.md` (all "no": rated 3+).
4. **Packages**: upload the `.msix`.
5. **Store listings**: English (United States) and Spanish: paste the fields from
   `listing/en-us.md` and `listing/es.md`; upload `listing/*.png` and `screenshots/`.
6. **Submission options**: paste `certification-notes.md` into *Notes for certification*.

Certification usually takes a day or two. If it fails, the report says which check; the
usual causes are in `certification-notes.md`.

## Updating later

Raise the version in `CMakeLists.txt` (`project(sprits_fast VERSION ...)`; the Store wants
the fourth number to stay 0), rebuild with the same identity, and submit an update.
