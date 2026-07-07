# PB-Tracker

A native PocketBook application that gives you Kobo-style reading stats: total time read, per-book totals, session counts, reading streaks, progress, and calendar-style activity views.

It combines background session tracking with PocketBook device databases to provide reading statistics directly on the reader.

## Features

- **Books Finished Counter**: Integrates with the PocketBook `explorer-3.db` to track when you have reached the last page of a book.
- **Real Progress Bars**: The visual progress bars reflect your page progress in the book.
- **Reading Streak Calendar**: A KOReader/Kobo-style heatmap for the current month: one cell per day, shaded by how much you read that day.
- **Smart Tracking**: Uses PocketBook state files to detect when a book is opened and closed, then logs reading sessions.
- **Kobo "Reading Life" UI**: Large typography, readable stats, book thumbnails, period summaries and completion status.
- **Localization groundwork**: English fallback with Polish UI support and manual language override.

## Dashboard Pages

Tap the left/right edge of the screen, or use physical left/right keys, to move through the stats dashboard:

- **Overview** — All-time totals, current streak, month/year summaries, and recently opened books with reading percentages.
- **Calendar** — A heatmap of your daily reading habits for the current month.
- **Monthly & Yearly Summaries** — Detailed lists of which books you read in that period, with session counts, covers, and completion status.

## Installation

1. Download or build `Reading Stats.app`.
2. Connect your PocketBook to your computer via USB.
3. Copy `Reading Stats.app` into your PocketBook `applications/` folder.
4. Eject the device. The app should appear in the Applications menu.

Optional: If you want a custom icon, place a 114x114 BMP file named `Reading Stats.app.bmp` next to the app in the `applications/` folder.

## Language

The app tries to detect the PocketBook language automatically. You can force a language by creating this file on the device:

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

## Building from Source

This project is built in C++ using the PocketBook SDK and InkView. The SDK is not committed to this repository.

Recommended:

```bash
export POCKETBOOK_TOOLCHAIN=/path/to/arm-obreey-linux-gnueabi.cmake
bash scripts/build.sh
```

Manual build:

```bash
cmake -S pb-reading-tracker -B build/pocketbook -DCMAKE_TOOLCHAIN_FILE=/path/to/arm-obreey-linux-gnueabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/pocketbook
```

This produces a `pbreadstats` binary. Rename or copy it to `Reading Stats.app` and deploy it to the PocketBook `applications/` directory.

See [`docs/build.md`](docs/build.md) for more details.

## How it works under the hood

1. **Session Detection**: A background timer runs in the app and polls PocketBook state files.
2. **Metadata**: When a session closes, it is written to the app database with metadata pulled via `GetBookInfo()`.
3. **Native Progress**: The UI cross-references session data with the device's native database to retrieve page progress and completion status.

## License

MIT License
