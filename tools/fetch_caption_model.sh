#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch_caption_model.sh <data-dir>
#
# Bundle the English speech model for live captions into a macOS or Linux
# package's data/models, so captions work the first time with no download.
# (Windows offers it, and the other three models, as ticks in the installer.)
#
# The model the BUILD MACHINE downloaded is stripped by the packagers first --
# it is not the package's -- and this fetches the published file fresh and
# checks it against a fixed checksum, so a package never carries a partial or
# substituted model. A failure stops the packaging rather than shipping a
# build that quietly lacks it. DECKBOY_NO_CAPTION_MODEL=1 skips it on purpose.
set -euo pipefail

data_dir="${1:?usage: fetch_caption_model.sh <data-dir>}"
if [ "${DECKBOY_NO_CAPTION_MODEL:-0}" = "1" ]; then
  echo "  - caption model: skipped (DECKBOY_NO_CAPTION_MODEL=1)"
  exit 0
fi

name="ggml-base.en.bin"
url="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$name"
want="a03779c86df3323075f5e796cb2ce5029f00ec8869eee3fdfb897afe36c6d002"
cache="${DECKBOY_MODEL_CACHE:-$HOME/.cache/deckboy-models}"

sum() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1 | tr -d '\\'
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

mkdir -p "$cache" "$data_dir/models"
if [ ! -f "$cache/$name" ] || [ "$(sum "$cache/$name")" != "$want" ]; then
  echo "  - caption model: downloading $name"
  curl -sL --fail --retry 3 -o "$cache/$name.part" "$url"
  mv "$cache/$name.part" "$cache/$name"
fi
got="$(sum "$cache/$name")"
if [ "$got" != "$want" ]; then
  echo "caption model checksum mismatch: $got (want $want)" >&2
  exit 1
fi
cp "$cache/$name" "$data_dir/models/$name"
echo "  - caption model: bundled data/models/$name ($(du -m "$data_dir/models/$name" | cut -f1) MB)"
