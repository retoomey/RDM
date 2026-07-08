#!/bin/bash
set -e

source ./test_utils.sh

echo "=== 🚀 Setting up ldmping Integration Test Environment ==="
TEST_DIR="/tmp/ldm_ping_test"
UP_DIR="$TEST_DIR/upstream"
LDM_PORT="6002"

# Clear old artifacts first
rm -rf "$TEST_DIR"
sandbox_ldm "$UP_DIR"

#mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/run}

# Minimal config for ldmd
UP_CONF="$UP_DIR/etc/ldmd.conf"
echo "ALLOW ANY ^127\.0\.0\.1$|^localhost$ .*" > "$UP_CONF"

echo "=== Creating Product Queue ==="
$BIN_DIR/pqcreate -c -s 2M -q "$UP_DIR/var/queues/up.pq"

echo "=== Starting Upstream LDM Daemon (Port $LDM_PORT) ==="
cd "$UP_DIR/var/run" || exit 1
$BIN_DIR/ldmd -x -P $LDM_PORT -q "$UP_DIR/var/queues/up.pq" -l - "$UP_CONF" > "$UP_DIR/var/logs/ldmd.log" 2>&1 &
UP_PID=$!
cd - > /dev/null

# Give the daemon a moment to bind and start
sleep 2

echo "=== Test 1: Ping Active Server ==="
# -i 0 means one trip (exit immediately after one ping).
PING_LOG="$TEST_DIR/ping_active.log"
if $BIN_DIR/ldmping -v -i 0 -P $LDM_PORT -h localhost > "$PING_LOG" 2>&1; then
    echo "✅ SUCCESS: ldmping successfully detected the active server (Exit code 0)."
else
    echo "❌ FAILURE: ldmping failed to detect the active server."
    cat "$PING_LOG"
    kill -TERM $UP_PID
    exit 1
fi

# Verify the output contains RESPONDING (which both old and new should output)
if grep -q "RESPONDING" "$PING_LOG"; then
    echo "✅ SUCCESS: Output correctly contains 'RESPONDING' state."
else
    echo "❌ FAILURE: Output does not contain 'RESPONDING'."
    cat "$PING_LOG"
    kill -TERM $UP_PID
    exit 1
fi

echo "=== Shutting down LDM ==="
kill -TERM $UP_PID
wait $UP_PID 2>/dev/null || true

echo "=== Test 2: Ping Inactive Server ==="
PING_DEAD_LOG="$TEST_DIR/ping_dead.log"
# This SHOULD fail, so we expect a non-zero exit code.
if $BIN_DIR/ldmping -v -i 0 -P $LDM_PORT -h localhost -t 3 > "$PING_DEAD_LOG" 2>&1; then
    echo "❌ FAILURE: ldmping incorrectly reported success for a dead server."
    cat "$PING_DEAD_LOG"
    exit 1
else
    echo "✅ SUCCESS: ldmping correctly returned a failure code for a dead server."
fi

# It shouldn't say RESPONDING
if grep -q "RESPONDING" "$PING_DEAD_LOG"; then
    echo "❌ FAILURE: Output incorrectly contains 'RESPONDING' for a dead server."
    cat "$PING_DEAD_LOG"
    exit 1
else
    echo "✅ SUCCESS: Output correctly reflects the server is down."
fi

echo "======================================================="
echo " 🎉 ALL LDMPING TESTS PASSED! 🎉"
echo "======================================================="
rm -rf "$TEST_DIR"
exit 0
