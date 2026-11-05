#!/usr/bin/env bash
# Point every remote reference to this repository at the checkout being tested, so CI validates and
# builds exactly this commit (a new component or header is not on main until this commit is promoted):
#   - sounds and the wake word model: read from the checkout (no download of our own files);
#   - external components (latter_core, latter_beacon): fetched at this exact commit.
# The dashboard_import package_import_url keeps @main (it is metadata, not part of the build).
#
# Usage: .github/scripts/pin_to_checkout.sh <commit sha>
set -euo pipefail
sha="$1"
find devices modules guis boards examples -name '*.yaml' -exec sed -i \
  -e '/package_import_url/! s#https://raw.githubusercontent.com/slampton/esphome-latter-core/main/#../#g' \
  -e '/package_import_url/! s#github://slampton/esphome-latter-core/\([^@]*\)@main#../\1#g' \
  -e "s/ref: main/ref: ${sha}/g" {} +
