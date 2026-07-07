# Building PB-Tracker

PB-Tracker is a native PocketBook/InkView application. The PocketBook SDK is intentionally not committed to this repository because it is large and should be installed locally by developers.

## Requirements

- CMake 3.10+
- PocketBook SDK with an ARM toolchain file
- A shell environment capable of running CMake

## Recommended local setup

Export the path to your PocketBook CMake toolchain file:

```bash
export POCKETBOOK_TOOLCHAIN=/path/to/arm-obreey-linux-gnueabi.cmake
```

Then run:

```bash
bash scripts/build.sh
```

The script builds the application and copies the result to:

```text
dist/Reading Stats.app
```

Copy this file to the PocketBook `applications/` directory.

## Manual build

```bash
cmake -S pb-reading-tracker \
      -B build/pocketbook \
      -DCMAKE_TOOLCHAIN_FILE=/path/to/arm-obreey-linux-gnueabi.cmake \
      -DCMAKE_BUILD_TYPE=Release

cmake --build build/pocketbook
cp build/pocketbook/pbreadstats "dist/Reading Stats.app"
```

## Language override

The app tries to detect the device language from PocketBook configuration. If that fails, create this file on the device:

```text
/system/pbreadstats/config.cfg
```

For Polish:

```ini
language=pl
```

For English:

```ini
language=en
```

## Debug log

The app writes debug information to:

```text
/system/pbreadstats/debug.log
```

If tracking does not work, this is the first file to inspect.

## SDK notes

Do not commit the full SDK to the repository. If we later add CI builds, the SDK should be downloaded or restored from a private/cache location according to the SDK license and distribution rules.
