#!/usr/bin/env bash
# T3: interactive regression — defaults preserve original behavior
#
# Manual test: must be run with a display attached. Verifies that GUI loads,
# real-time pacing is observed (no FTRT speedup), and joystick control still
# works.

set -euo pipefail
cd "$(dirname "$0")/../.."

echo "T3: launching interactive CRRCSim via 'just run' (which builds first;"
echo "compile time on a clean tree can be substantial). Verify manually:"
echo "  1. GUI window opens once the build finishes."
echo "  2. Frame rate is locked to wall-clock (~60 FPS, not unbounded)."
echo "  3. Joystick / keyboard input controls the aircraft."
echo "  4. The 'smol SIL config' line shows realtime_throttle=1."
echo ""
echo "Press Ctrl+C to stop."
echo ""
just run
