# Contributing

Guidelines for working on Vindauga (C++20, Qt 6 Quick + QML, libfreerdp 3). Read `README.md` first.

## Layering

- `src/core/` uses QtCore/QtNetwork and QtGui (for `QImage`) only, never Qt Quick, QML or Widgets. `src/ui/` depends on `core/`, never the other way round.
- The UI is Qt Quick / QML only. Do not introduce Qt Widgets.
- Do not change the Qt or FreeRDP version, and do not add third-party libraries unless the task explicitly asks for it.

## Working rules

- One task at a time. No drive-by refactoring of unrelated code.
- Do not invent features, files or architecture beyond what the task describes. If something is genuinely unclear, pick the minimal reasonable option, leave a `// TODO:` and say so.
- Green build before committing: `cmake --build --preset debug` and `ctest --preset debug` must pass.
- Never commit tokens, passwords, `.rdp` files or traffic captures. Never log secrets; log lengths or `<redacted>`. Use the `Q_LOGGING_CATEGORY` per module.
- Do not start long-running processes or real AVD sessions on your own; they require the maintainer's MFA.

## Build

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

If CMake cannot find Qt 6.9+ or QtKeychain, set `CMAKE_PREFIX_PATH` (colon-separated) as described in `README.md`.

## Commit style

Short imperative subject line and a body that explains why. One task per commit where possible.

Internal working notes live in `internaldocs/`, which is untracked and must stay that way.
