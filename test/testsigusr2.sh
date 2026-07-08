#!/bin/bash
set -e

source ./test_utils.sh

echo "=== 🚀 Setting up SIGUSR2 Log Roll Integration Test ==="
TEST_DIR="/tmp/ldm_sigusr2_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/run}

LDMD_CONF="$TEST_DIR/etc/ldmd.conf"

# FIX: Add an ALLOW rule to force ldmd into server-mode, and add the -q flag 
# to the EXEC rule so pqact doesn't immediately crash.
cat << EOF > "$LDMD_CONF"
ALLOW ANY ^127\.0\.0\.1$|^localhost$ .*
EXEC "$BIN_DIR/pqact -x -d $TEST_DIR -l $TEST_DIR/var/logs/pqact.log -q $TEST_DIR/var/queues/ldm.pq -i 15 /dev/null"
EOF

echo "=== Creating Product Queue ==="
$BIN_DIR/pqcreate -c -s 2M -q "$TEST_DIR/var/queues/ldm.pq"

echo "=== Starting LDM Daemon ==="
$BIN_DIR/ldmd -P 6005 -q "$TEST_DIR/var/queues/ldm.pq" -l - "$LDMD_CONF" > "$TEST_DIR/var/logs/ldmd.log" 2>&1 &
LDMD_PID=$!

# Give the daemon a moment to fully initialize
sleep 2 

echo "=== Sending First SIGUSR2 (INFO -> DEBUG) ==="
kill -SIGUSR2 $LDMD_PID
# Give the select() loop a second to wake up and process the g_roll_logs flag
sleep 1 

echo "=== Sending Second SIGUSR2 (DEBUG -> INFO) ==="
kill -SIGUSR2 $LDMD_PID
sleep 1

echo "=== Verifying Log Output ==="
PASSED=true

# Check for the DEBUG transition
if grep -q "Logging level rolled to DEBUG via SIGUSR2" "$TEST_DIR/var/logs/ldmd.log"; then
    echo "✅ SUCCESS: Transition to DEBUG successfully captured in the logs."
else
    echo "❌ FAILURE: DEBUG transition missing from ldmd.log."
    PASSED=false
fi

# Check for the INFO transition
if grep -q "Logging level rolled to INFO via SIGUSR2" "$TEST_DIR/var/logs/ldmd.log"; then
    echo "✅ SUCCESS: Transition back to INFO successfully captured in the logs."
else
    echo "❌ FAILURE: INFO transition missing from ldmd.log."
    PASSED=false
fi

echo "=== Shutting down LDM (PID $LDMD_PID) ==="
kill -TERM $LDMD_PID
wait $LDMD_PID 2>/dev/null || true

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 SIGUSR2 Log Roll test passed! 🎉"
    echo "======================================================="
    rm -rf "$TEST_DIR"
    exit 0
else
    echo "======================================================="
    echo " 🚨 SIGUSR2 Log Roll test failed 🚨"
    echo "--- Dump of ldmd.log ---"
    cat "$TEST_DIR/var/logs/ldmd.log"
    echo "======================================================="
    exit 1
fi
