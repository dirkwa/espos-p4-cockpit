#!/usr/bin/env bash
# One build at a time per machine, niced, TMPDIR off the RAM disk: espOS's wrapper owns all of that.
exec "$(dirname "${BASH_SOURCE[0]}")/../espos/scripts/build.sh" "$@"
