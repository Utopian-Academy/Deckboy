#!/usr/bin/env bash
# Run a network command, retrying a transient failure: four tries, 15/30/45 s
# apart. A runner that briefly lost DNS ("Could not resolve host: github.com",
# main 24b832e5, 2026-11-07) failed a whole build on the first try.
# A failed git clone removes its target folder itself, so a retry starts clean.
n=0
until "$@"; do
  n=$((n + 1))
  if [ "$n" -ge 4 ]; then
    echo "ci_retry: gave up after $n tries: $*" >&2
    exit 1
  fi
  echo "ci_retry: try $n failed, again in $((n * 15)) s: $*" >&2
  sleep $((n * 15))
done
