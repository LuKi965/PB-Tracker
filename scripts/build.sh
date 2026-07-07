#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/pocketbook"
DIST_DIR="${ROOT_DIR}/dist"
APP_NAME="ReadingStats.app"
ICON_NAME="ReadingStats.app.bmp"
SOURCE_ICON_NAME="Reading Stats.app.bmp"
MAIN_CPP="${ROOT_DIR}/pb-reading-tracker/src/main.cpp"

if [[ -z "${POCKETBOOK_TOOLCHAIN:-}" ]]; then
  echo "Set POCKETBOOK_TOOLCHAIN to your PocketBook CMake toolchain file." >&2
  echo "Example:" >&2
  echo "  export POCKETBOOK_TOOLCHAIN=/path/to/arm-obreey-linux-gnueabi.cmake" >&2
  exit 1
fi

# Temporary diagnostic safety: do not hard-map observed InkPad Color 3 key
# values 24/25 to page navigation yet. The previous debug log proved that
# key events arrive, but not which physical button produced which code.
python3 - "$MAIN_CPP" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
s = s.replace(
    'if (key == IPC3_KEY_NEXT || key == KEY_RIGHT || key == KEY_NEXT || key == KEY_NEXT2) {',
    'if (key == KEY_RIGHT || key == KEY_NEXT || key == KEY_NEXT2) {'
)
s = s.replace(
    'if (key == IPC3_KEY_PREV || key == KEY_LEFT || key == KEY_PREV || key == KEY_PREV2) {',
    'if (key == KEY_LEFT || key == KEY_PREV || key == KEY_PREV2) {'
)
s = s.replace(
    'db_log("Key down: type=%d key=%d", type, key);',
    'db_log("Key diagnostic: type=%d key=%d", type, key);'
)
p.write_text(s)
PY

cmake -S "${ROOT_DIR}/pb-reading-tracker" \
      -B "${BUILD_DIR}" \
      -DCMAKE_TOOLCHAIN_FILE="${POCKETBOOK_TOOLCHAIN}" \
      -DCMAKE_BUILD_TYPE=Release

cmake --build "${BUILD_DIR}"

mkdir -p "${DIST_DIR}"
cp "${BUILD_DIR}/pbreadstats" "${DIST_DIR}/${APP_NAME}"

if [[ -f "${ROOT_DIR}/assets/${ICON_NAME}" ]]; then
  cp "${ROOT_DIR}/assets/${ICON_NAME}" "${DIST_DIR}/${ICON_NAME}"
elif [[ -f "${ROOT_DIR}/assets/${ICON_NAME}.base64" ]]; then
  base64 -d "${ROOT_DIR}/assets/${ICON_NAME}.base64" > "${DIST_DIR}/${ICON_NAME}"
elif [[ -f "${ROOT_DIR}/assets/${SOURCE_ICON_NAME}" ]]; then
  cp "${ROOT_DIR}/assets/${SOURCE_ICON_NAME}" "${DIST_DIR}/${ICON_NAME}"
elif [[ -f "${ROOT_DIR}/assets/${SOURCE_ICON_NAME}.base64" ]]; then
  base64 -d "${ROOT_DIR}/assets/${SOURCE_ICON_NAME}.base64" > "${DIST_DIR}/${ICON_NAME}"
fi

if [[ -f "${DIST_DIR}/${ICON_NAME}" ]]; then
  cp "${DIST_DIR}/${ICON_NAME}" "${DIST_DIR}/ReadingStats.bmp"
fi

echo "Built: ${DIST_DIR}/${APP_NAME}"
if [[ -f "${DIST_DIR}/${ICON_NAME}" ]]; then
  echo "Icon:  ${DIST_DIR}/${ICON_NAME}"
fi
