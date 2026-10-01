# AGENTS.md

## Project

- C++20 image analyzer `Laba2`: GUI app on FLTK 1.4.5 plus a console analyzer `Laba2Scan`, both over the static library `laba2_core`.
- Supported formats: BMP, PNG, JPEG, GIF, TIFF, PCX. All parsers, decoders, and codecs are hand-written; no third-party image or compression library is used.
- `third_party/fltk-1.4.5/` holds vendored FLTK sources, added via `add_subdirectory(... EXCLUDE_FROM_ALL)`.
- CMake 4.1 or newer is required. Verified toolchain: CMake 4.3.4, Ninja 1.13.2, Apple Clang arm64, Pillow 12.3.0.
- UI text and all user-visible strings are in Russian. Code comments are forbidden unless explicitly requested.

## Layout

- `src/model` — format enum, result record, scan state, text/CSV rendering.
- `src/core` — `FileHead` lazy range reads, format detection, metadata extraction.
- `src/core/parsers` — one header parser per format; fills fields and warnings only.
- `src/core/codec` — pixel decoders, compression codecs, pixel statistics.
- `src/io` — directory traversal and thread pool.
- `src/controller` — scanning, state aggregation, CSV save.
- `src/view` — FLTK window: results table, preview canvas, field panel, status bar.
- `src/main.cpp` is the GUI entrypoint; `src/main_console.cpp` is the `Laba2Scan` entrypoint.
- `laba2_core` must stay FLTK-free so `Laba2Scan` builds without the GUI.

## Repository state

- Git repo exists on branch `main` with **zero commits and no `.gitignore`**. Everything is untracked, including `build/`, `.idea/`, `.DS_Store`. There is no diff to review and no rollback — do not assume history exists.
- `build/` is the only valid configured tree. `cmake-build-debug/` is a stale CLion tree holding both pre-rename `DraftLaba2` artifacts and newer `Laba2.app`; ignore it or delete it, never build from it.
- `.idea/` is CLion workspace metadata — edit source and CMake files instead.

## Build and verify

- Configure: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`.
- Build all: `cmake --build build`.
- Build console tool only (fastest, no FLTK): `cmake --build build --target Laba2Scan`.
- Build GUI only: `cmake --build build --target Laba2`.
- Tests: `ctest --test-dir build --output-on-failure` (7 tests).
- Console run: `./build/Laba2Scan "task/Для проверки Lab2" --deep`.
  Run it **from the repo root**. Relative paths are resolved against the CWD, so running it from `build/` silently reports `0` files with no error.
- GUI path differs per platform: macOS `build/Laba2.app/Contents/MacOS/Laba2`, Windows `build\Laba2.exe`. There is no `build/Laba2` file on macOS.
- FLTK builds with GL, forms, fluid, fltk-options and shared libs force-disabled in `CMakeLists.txt`; X11 backend is off on Apple. Do not re-enable them.
- `laba2_core` compiles with `-Wall -Wextra` (private); MSVC uses `/utf-8 /W3 /permissive-`.

## Testing quirks

- There is **no unit-test framework**. All 7 CTest entries are end-to-end `Laba2Scan` invocations, two of which use `WILL_FAIL`/`PASS_REGULAR_EXPRESSION` to assert corrupt-file behavior. Add coverage by extending fixtures or adding another `add_test`, not by introducing a test framework.
- `tests/data` holds 80 generated images across all six formats, and `tests/corrupt` holds 10 damaged/hostile files. Both are **generated**. Never hand-edit them.
- `tests/data/test.csv` is a stray artifact of an earlier `--save` run, not a fixture — it is not one of the 80 images and the scan ignores it by extension.
- `tests/corrupt/not_an_image.txt` is deliberately never scanned, because the scanner filters by extension first. The spoofed-extension case the lab asks about is covered instead by `fake_image.png` (valid `.png` extension, non-image content) — verified to report `не изображений: 1`.
- Regenerate: `python3 tests/generate_edge_cases.py` and `python3 tests/generate_tiff_cases.py`. Pillow may create reference images and write LZW TIFF, but must never be used to validate our decoders.
- No Pillow *comparison harness* is committed — the byte-parity check in `help/report.md` was performed manually. Do not go looking for a script that does not exist.
- No CI, lint, or formatter config exists. Do not invent a lint step or add tooling config unasked.

## Gotchas

- BMP RLE4 encoded mode is two bytes: count, then a value byte whose high and low nibbles alternate. Counts 0, 1, 2 are escapes (EOL, EOB, delta), so a 3-pixel literal must be emitted as single runs. Word padding is computed from the absolute stream position, not from the block length or the row start.
- Pillow's RLE4 absolute mode computes bytes as `count // 2` and drops the last pixel on odd counts; do not treat Pillow as the oracle for odd-count RLE4.
- TIFF `StripOffsets` must be computed after all IFD values are placed. Tag 338 (`ExtraSamples`) decides whether a 4-sample TIFF really has an alpha channel.
- JPEG chroma upsampling needs separate horizontal/vertical factors and a triangular filter; JPEG comparison tolerance is `maxd <= 16`.
- Progressive JPEG is intentionally recognized but not decoded to pixels; it must yield a warning, not an error.

## Reports

- `task/Лабораторная работа 2-5.pdf` is the lab requirements source of truth.
- `task/otchet_example.docx` is the reference for report structure and writing style; it is a zip archive, so extract `word/document.xml` to read it.
- The finished report belongs in `task/`, matching the example's sections and tone. `help/report.md` is the current Russian draft and the source of verified numbers.
- Put user-facing guides, configurations, and answer notes in `help/*.md`; append short follow-up answers to the relevant existing note instead of creating a new file each time.