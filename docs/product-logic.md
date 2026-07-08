# Reading Stats product logic

This app should not be a dump of every number we can compute. It should answer a few useful reading questions quickly.

## Product goal

Reading Stats should answer:

1. What am I reading now?
2. Am I reading regularly?
3. Which books am I progressing through?
4. Which time values were actually tracked by this app?

Kobo reading stats are a useful reference, but not a blueprint to copy blindly.

## Core rule: progress and time are different sources

PocketBook native progress and Reading Stats tracked time are not the same thing.

Native PocketBook progress:

- comes from PocketBook's system database;
- can exist before Reading Stats was installed;
- may show that a book is already started, for example 83% complete;
- must be read-only from our app's perspective.

Reading Stats tracked time:

- comes from our own daemon/session tracking;
- starts only when this app begins tracking;
- must not be invented for books read before installation.

Therefore a book can honestly appear as:

- 83% complete;
- 0 min tracked by Reading Stats.

This is correct and preferable to fake historical time.

## File safety model

The app may read PocketBook system files and databases, but must not modify them.

Read-only sources include:

- /mnt/ext1/system/explorer-3/explorer-3.db
- /system/explorer-3/explorer-3.db
- /system/state/lastopen.txt
- /tmp/.current

Writable app-owned location remains:

- /system/pbreadstats/reading_stats.db
- /system/pbreadstats/debug.log
- /system/pbreadstats/config.cfg if the user creates it

No automatic edits should be made to:

- /system/config/desktop/view.json
- /system/language/*.txt
- PocketBook's explorer-3.db

## Implemented data direction

The app imports native PocketBook progress into its own database.

It stores native progress cache fields in the `books` table:

- native_progress
- native_cpage
- native_npage
- native_completed
- imported_native
- native_last_seen

It also stores progress changes in:

- progress_snapshots

This makes already-started books visible in Reading Stats without pretending that their old reading time was tracked.

## Screen strategy

Keep the app to three useful screens:

1. Overview
2. Activity
3. Library

### Overview

Job: answer `how am I doing right now?`

Show:

- current or most recent book;
- PocketBook progress;
- tracked time today;
- tracked time this week;
- average tracked session;
- current streak.

### Activity

Job: answer `am I reading regularly?`

Show:

- last 14 days total;
- active reading days;
- best day;
- simple daily bars.

### Library

Job: answer `what am I reading or finishing?`

Show in future UI iterations:

- in-progress books from PocketBook native progress;
- recently tracked books from Reading Stats sessions;
- finished books from native completion/tracked state.

## UX wording

Prefer labels that reveal the source of truth:

- PocketBook progress
- Tracked time
- Since Reading Stats was installed
- In progress
- Recently tracked
- Finished

Avoid labels that imply fake precision:

- total lifetime read time for a book if it existed before tracking;
- estimated finish time before enough tracked data exists;
- pages per minute unless page deltas are reliably tracked.
