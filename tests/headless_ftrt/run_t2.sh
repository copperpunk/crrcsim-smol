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

# Wait for the listener to bind (up to 5 attempts of 0.5s each).
for i in 1 2 3 4 5; do
    if grep -q "listening on 127.0.0.1:$PORT" "$LOG" 2>/dev/null; then
        break
    fi
    sleep 0.5
done

echo "Sending throw byte to 127.0.0.1:$PORT ..."
echo -n "x" | nc -q 1 127.0.0.1 "$PORT"

wait "$SIM_PID"
EXIT_CODE=$?

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
