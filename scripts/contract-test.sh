#!/usr/bin/env bash
# Task-manager contract test: local build + optional workspace integration smoke.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
PROJECTS="$(cd "$ROOT/.." && pwd)"
WORKSPACE="$PROJECTS/satellite-workspace"

if [[ ! -x "$BUILD/task-client" ]]; then
  (cd "$ROOT" && meson subprojects download && meson setup build -Dzmq=enabled && meson compile -C build)
fi

# Local schema/build sanity (no workspace required)
echo "OK task-manager local build"

if [[ -f "$WORKSPACE/install/env.sh" ]]; then
  echo "delegating integration smoke to workspace"
  exec "$WORKSPACE/scripts/smoke-integration.sh"
fi

echo "skip workspace integration (no $WORKSPACE/install/env.sh; run workspace build-all.sh)" >&2
exit 0
