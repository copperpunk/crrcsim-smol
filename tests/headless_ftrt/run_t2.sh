#!/usr/bin/env bash
# T2: hand-launch via TCP command port
#
# Pre-req: smol-SIL paired (see T1 header). Without it, CRRCSim blocks at
# actuator recv and the throw never gets seen by the FDM.

set -euo pipefail
cd "$(dirname "$0")/../.."

PORT=9100
SEED=42
DURATION=30
CONFIG=configs/smol_test_handlaunch.xml
LOG=/tmp/t2_run.log

./crrcsim --headless --no-realtime --rng-seed "$SEED" \
          --duration "$DURATION" --launch-mode hand \
          --command-port "$PORT" -g "$CONFIG" > "$LOG" 2>&1 &
SIM_PID=$!

# Wait for the TCP port to actually accept connections. Polling stdout for
# the listener banner is unreliable because stdout is fully buffered when
# redirected to a file — the line may sit in the buffer long after the port
# is ready. nc -z probes the port directly.
for i in 1 2 3 4 5 6 7 8 9 10; do
    if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
        break
    fi
    sleep 0.5
done

echo "Sending throw byte to 127.0.0.1:$PORT ..."
echo -n "x" | nc -q 1 127.0.0.1 "$PORT"

# Disable -e around the wait so a non-zero CRRCSim exit doesn't skip the
# diagnostic block below; we want the tail-of-log on failure.
set +e
wait "$SIM_PID"
EXIT_CODE=$?
set -e

if [ "$EXIT_CODE" -ne 0 ]; then
    echo "T2 FAIL: CRRCSim exit code $EXIT_CODE"
    tail -30 "$LOG"
    exit 1
fi

if ! grep -q "throw fired by command-port byte" "$LOG"; then
    echo "T2 FAIL: throw never fired"
    tail -30 "$LOG"
    exit 1
fi

if ! grep -q "Sim duration .* reached" "$LOG"; then
    echo "T2 FAIL: duration-bounded exit never fired"
    tail -30 "$LOG"
    exit 1
fi

echo "T2 PASS"
