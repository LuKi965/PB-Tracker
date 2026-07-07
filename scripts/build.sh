#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/pocketbook"
DIST_DIR="${ROOT_DIR}/dist"
APP_NAME="ReadingStats.app"
ICON_NAME="ReadingStats.app.bmp"
SOURCE_ICON_NAME="Reading Stats.app.bmp"

if [[ -z "${POCKETBOOK_TOOLCHAIN:-}" ]]; then
  echo "Set POCKETBOOK_TOOLCHAIN to your PocketBook CMake toolchain file." >&2
  echo "Example:" >&2
  echo "  export POCKETBOOK_TOOLCHAIN=/path/to/arm-obreey-linux-gnueabi.cmake" >&2
  exit 1
fi

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
