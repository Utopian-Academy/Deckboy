#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/native"
BIN="${BUILD_DIR}/deckboy-native"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" >/dev/null
cmake --build "${BUILD_DIR}" -j >/dev/null

"${BIN}" --self-check
"${BIN}" --smoke
