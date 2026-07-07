# Building and packaging

PB-Tracker is a native PocketBook / InkView app. The PocketBook SDK is not committed to this repository.

## Requirements

- CMake 3.10+
- PocketBook SDK
- A PocketBook CMake toolchain file

## Build

Set the toolchain path:

```bash
export POCKETBOOK_TOOLCHAIN=/path/to/arm-obreey-linux-gnueabi.cmake
```

Build and package:

```bash
bash scripts/build.sh
```

Output:

```text
dist/Reading Stats.app
```

If an icon exists at `assets/Reading Stats.app.bmp`, the script also copies it to:

```text
dist/Reading Stats.app.bmp
```

Copy both files to the PocketBook `applications/` directory.

## Language override

The app tries to detect the PocketBook language automatically. You can force the language with:

```text
/system/pbreadstats/config.cfg
```

Polish:

```ini
language=pl
```

English:

```ini
language=en
```

## Navigation behavior

PocketBook devices can have user-configured tap zones. This app does not use invisible left/right screen zones for navigation.

Navigation is handled through:

- PocketBook page-turn actions: `EVT_PREVPAGE` / `EVT_NEXTPAGE`,
- physical page keys: `KEY_PREV`, `KEY_NEXT`, `KEY_LEFT`, `KEY_RIGHT`,
- Back key: `KEY_BACK` closes the app.

Raw touch events are logged and left to PocketBook/system configuration.

## Debug log

The app writes debug output to:

```text
/system/pbreadstats/debug.log
```

Check this file first when testing navigation, tracking or language detection.
