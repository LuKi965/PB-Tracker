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

python3 - "$MAIN_CPP" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
s = s.replace('''        case EVT_POINTERUP:\n        case EVT_TOUCHUP:\n            db_log("Touch event ignored by app UI: type=%d x=%d y=%d. PocketBook tap zones remain in control.", type, par1, par2);\n            return 0;''', '''        case EVT_POINTERUP:\n        case EVT_TOUCHUP:\n            db_log("Touch event: type=%d x=%d y=%d", type, par1, par2);\n            {\n                int col = (par1 * 3) / ScreenWidth();\n                int row = (par2 * 3) / ScreenHeight();\n                if (col < 0) col = 0; if (col > 2) col = 2;\n                if (row < 0) row = 0; if (row > 2) row = 2;\n                if (row == 1 && col == 0) { go_prev_page(); return 1; }\n                if (row == 1 && col == 2) { go_next_page(); return 1; }\n                if (row == 1 && col == 1) { rebuild_pages(); draw_dashboard(); return 1; }\n            }\n            return 0;''')
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
