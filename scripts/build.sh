#!/usr/bin/env bash
# Run a PlatformIO build without starving the interactive session.
#
# A full ESP-IDF build saturates all cores on this 4-core Pi: load hits ~8,
# and the VS Code server (and the SSH session carrying it) stops getting
# scheduled, so the editor disconnects mid-build. Two knobs fix that:
#
#   nice -n 15   compile work yields to anything interactive
#   -j N-1       leave one core for the editor, shell and network stack
#
# ionice too: the link step and the component manager are IO-heavy, and IO
# starvation stalls the editor just as effectively as CPU starvation.
#
# Usage: scripts/build.sh [-e env] [...pio run args]
set -euo pipefail

# One build at a time. Two concurrent `pio run`s put ~2x the core count of
# compilers on the machine and the editor drops regardless of nice level —
# that is what actually caused the disconnects this script exists to stop.
# They also race on .pio/, so the second can read half-written objects.
#
# Default: wait for the running build (usually what you want — the second
# invocation is the one you care about). BUILD_NOWAIT=1 fails fast instead.
LOCK="${TMPDIR:-/tmp}/.pio-build-$(id -u)-$(basename "$PWD").lock"
exec 9>"$LOCK"
if ! flock -n 9; then
  if [ "${BUILD_NOWAIT:-0}" = "1" ]; then
    echo "==> another build is running (BUILD_NOWAIT=1) — aborting" >&2
    exit 1
  fi
  echo "==> another build is running; waiting for it to finish..."
  flock 9
fi

jobs=$(( $(nproc) - 1 ))
[ "$jobs" -lt 1 ] && jobs=1

cmd=(pio run -j "$jobs" "$@")
command -v ionice >/dev/null 2>&1 && cmd=(ionice -c3 "${cmd[@]}")

echo "==> nice -n 15, -j $jobs (of $(nproc) cores)"
nice -n 15 "${cmd[@]}"
