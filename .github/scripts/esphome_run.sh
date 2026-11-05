#!/usr/bin/env bash
# Run an esphome command for CI: retry up to ATTEMPTS times (config validation fetches remote files and can
# hit a network hiccup), and on each failure put the end of the log into the run's annotations, which anyone
# can read (job logs need repository admin rights through the API).
#
# Usage: ATTEMPTS=2 .github/scripts/esphome_run.sh "<title>" config examples/foo.yaml
set -o pipefail
title="$1"
shift
attempts="${ATTEMPTS:-1}"
for ((attempt = 1; attempt <= attempts; attempt++)); do
  if esphome "$@" 2>&1 | tee /tmp/esphome.log; then
    exit 0
  fi
  level=error
  if ((attempt < attempts)); then
    level=warning
  fi
  tail -n 60 /tmp/esphome.log | sed -e 's/\x1b\[[0-9;]*m//g' -e 's/%/%25/g' | tr '\n' '\r' |
    sed -e 's/\r/%0A/g' -e "s/^/::${level} title=${title} (attempt ${attempt}\/${attempts})::/"
  echo
done
exit 1
