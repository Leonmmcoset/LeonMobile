# Wayland protocol sources

`input-method-unstable-v2.xml` is copied unmodified from wlroots 0.20.0,
`protocol/input-method-unstable-v2.xml`; its complete upstream copyright and
permission notice are embedded in the XML. SHA-256: `aaf68b2aec481f507a4b915ab7c624bf676495ebb2030eaf207a9c8bcac7630d`.

Text-input-v3 and xdg-shell are generated from the pinned distribution's
wayland-protocols package; their source hashes and licenses stay with Buildroot.

Unicode Unihan 17.0.0 is downloaded and hash-checked by `leon-pinyin`.
`scripts/build-pinyin.py` produces deterministic candidates without committing
downloads or generated data. The Unicode notice is in
`third_party/Unicode-LICENSE` and installed alongside the dictionary. The
small common-word supplement in the generator is original LeonMobile data.
