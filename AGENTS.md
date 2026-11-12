# AGENTS.md

Guidance for AI coding agents (and humans) working in this repository. Read [CONTRIBUTING.en.md](CONTRIBUTING.en.md) for the full contributor guide and [docs/architecture.md](docs/architecture.md) for how the pieces fit together.

## What this is

GameScrcpy mirrors an Android phone to Windows and maps keyboard, mouse and gamepad input onto touches, with optional logic written as blueprints or TypeScript. The PC side is C++20 with Qt 6 / Qt Quick (`src/`), the phone side is a plain Java server and a Kotlin companion app (`android/`), and both speak the binary protocol specified in `protocol/SPEC.md`.

## Layout

- `src/` — the Windows app. Every module except `render`, `ui` and `app` is plain C++ without Qt and builds with the `core-only` preset. `src/cli` is the `gs` command line.
- `android/` — `protocol`, `server` (plain Java, no Kotlin standard library) and `companion` (Kotlin).
- `protocol/` — the specification and the golden test vectors shared by C++ and Java.
- `sdk/` — generated script API types and docs (`gs api write sdk`), and the C header for native extensions.
- `tests/` — one doctest program per module, Qt Quick Tests for the interface, test data.
- `third_party/` — pinned dependencies; configuring never goes online.

## Build and test

```powershell
$env:GS_QT_DIR = 'C:\Qt\6.10.1\msvc2022_64'
. .\tools\dev-env.ps1
cmake --workflow --preset msvc-debug
cmake --build --preset msvc-debug --target all_qmllint
ctest --preset msvc-debug -R <regex>
```

- Presets: `msvc-debug`, `msvc-release` (LTO), `msvc-asan`, `core-only`, `ci` (what CI runs). Outputs land in `build/<preset>/bin`.
- QML lint must stay at zero warnings.
- Android: `cd android; .\gradlew.bat build` (compiles, runs JUnit and lint; lint warnings are errors).
- The mirror window runs without a phone: `GameScrcpy.exe --demo-device tests\data\video\emulator_h265.media`.
- Tests that need a phone run only when `GS_ADB_TEST_SERIAL` is set.

## Rules that the code base enforces

- `python tools/check_source_rules.py` (CTest `gs_source_rules`) must pass: no parsing of `dumpsys` / `wm` output, no fixed-delay synchronization (sleeps, delayed single-shot timers, `postDelayed`; a deliberate delay carries `source-rule-allow R9: <reason>` on the same line), no `CopyFromScreen`, every QML `Text` declares `textFormat`, controls in `src/ui/controls` take colors, sizes and durations only from the theme, and every user-facing string is English in code and translated in `src/app/i18n/gamescrcpy_zh_CN.ts`.
- After changing interface text, run the `update_translations` target and translate the new entries.
- Protocol changes go through `protocol/SPEC.md` and `protocol/tools/gen_vectors.py`; never edit the generated vectors by hand. Both the C++ and the Java tests must pass.
- After changing the script API table in `src/script/src/Api.cpp`, regenerate `sdk/` with `gs api write sdk`.

## Conventions

- Let the code speak for itself: comments are kept to a short description at the top of a file; explanations belong in pull requests and documentation.
- Identifiers and commit messages are English; user-facing documentation is Simplified Chinese first, with English versions of the main documents.
- Interface colors, type sizes, spacing, radii and motion come from `src/ui/theme`, never bare numbers.
- New behavior comes with tests; a bug fix starts with a failing test.
- Do not add network downloads to configuring or building; new dependencies are vendored into `third_party/` with their version, license and checksums recorded in `third_party/README.md`.
