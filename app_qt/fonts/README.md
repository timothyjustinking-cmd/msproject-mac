# Bundled font — Carlito (SIL Open Font License 1.1)

Operator decision 13 (`docs/ui-parity-spec.md`): Carlito is **bundled on all
three platforms**, because MS Project's reference typography is Calibri 11 pt and
Carlito is the metrically identical open substitute.  The shell must never
depend on the host having the font, and the offscreen CI runners (Qt 6 ships no
fonts) must measure the same text metrics as a desktop.

The four faces are compiled into each executable as Qt resources
(`app_qt/CMakeLists.txt`, `qt_add_resources` per target) and registered by
`configure_application()` in `app_qt/main_window.cpp`.

## Provenance

Fetched 2026-10-23 from the upstream OFL repository
<https://github.com/googlefonts/carlito>, tag `main`:

| file | bytes | sha256 |
|---|---|---|
| `Carlito-Regular.ttf` | 628,032 | `f6418f708baede9789daef5d458c0f53d2a888af9820e8062934e504fedc6595` |
| `Carlito-Bold.ttf` | 682,468 | `bb5d20f79b82599ec72983597437373a80f2d2085fa91fc144fd74e876a594db` |
| `Carlito-Italic.ttf` | 615,236 | `0b019225e58d702bfedcbd35c21696769f8ee115cb6343f84c2f240312450d1c` |
| `Carlito-BoldItalic.ttf` | 808,508 | `b32928186c119599e03ca6a1ffc680fdcb7fac95772f4b95d989cf6cd3861517` |
| `OFL.txt` | 4,424 | `58402f82a7c332a700294988fe7554fbb0a63a8d27ccc1ee3bbc640311990a00` |
| `AUTHORS.txt` | 265 | `127a5f8f8e10214b4cce882d921c24d183656fd2220d7ad4185136f3a0eadfd3` |

The hashes are of the files as committed; `OFL.txt` and `AUTHORS.txt` are the
upstream licence and authorship files, copied verbatim.

## Licence

Carlito is Copyright 2013 The Carlito Project Authors, licensed under the SIL
Open Font License, Version 1.1 — see `OFL.txt`.  The OFL permits bundling and
redistribution with software, including commercially, provided the font is not
sold on its own and the licence accompanies it.  This port is a personal
project; the fonts are redistributed unmodified.
