#!/usr/bin/env bash
set -euo pipefail

source ./test_utils.sh

echo "=== 🚀 Setting up $BIN_LDMSEND Integration Test ==="
TEST_DIR="/tmp/ldm_ldmsend_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

# Define file paths
SERVER_QUEUE="$TEST_DIR/var/queues/server.pq"
SERVER_CONF="$TEST_DIR/etc/ldmd.conf"
SERVER_LOG="$TEST_DIR/var/logs/ldmd.log"
LDMSEND_LOG="$TEST_DIR/var/logs/ldmsend.log"
DATA_DIR="$TEST_DIR/var/data"
LDM_PORT=38805

mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/data,var/run}

echo "=== Phase I: Initializing Product Queue ==="
"$BIN_DIR/$BIN_PQCREATE" -s 5M -q "$SERVER_QUEUE"

echo "=== Phase II: Creating Server Configuration ==="
cat << EOF > "$SERVER_CONF"
# Allow the local machine to push data via the HIYA protocol
ACCEPT ANY .* localhost
EOF

echo "=== Phase III: Launching $BIN_LDMD Server Daemon ==="
"$BIN_DIR/$BIN_LDMD" -P $LDM_PORT -q "$SERVER_QUEUE" -l "$SERVER_LOG" "$SERVER_CONF" &
SERVER_PID=$!

# Ensure LDM shuts down even if the script fails and exits early
cleanup() {
    echo "=== Shutting down processes ==="
    kill -TERM $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT

# Give the daemon a moment to bind to the port
sleep 2

echo "=== Phase IV: Generating Test Payload ==="
TEST_FILE="$DATA_DIR/dummy_payload.txt"
echo "This is a test payload sent from modern C++ $BIN_LDMSEND." > "$TEST_FILE"

echo "=== Phase V: Launching C++ $BIN_LDMSEND to Push Data ==="
"$BIN_DIR/$BIN_LDMSEND" -h localhost -P $LDM_PORT -f EXP "$TEST_FILE" > "$LDMSEND_LOG" 2>&1

# Give the product time to traverse the RPC/TCP network socket
sleep 1

echo "=== Phase VI: Verifying Ingestion Success ==="
PQCAT_OUT="$DATA_DIR/pqcat_out.txt"
# Using pqcat to extract the payload we just pushed
"$BIN_DIR/$BIN_PQCAT" -q "$SERVER_QUEUE" -f EXP -p "dummy_payload" > "$PQCAT_OUT"

PASSED=true

# Check if pqcat successfully extracted the file contents
if grep -q "This is a test payload sent from modern C++ $BIN_LDMSEND." "$PQCAT_OUT"; then
    echo "✅ SUCCESS: The payload was successfully transmitted and retrieved from the server queue."
else
    echo "❌ FAILURE: Product missing from the server's product queue or data corrupted."
    PASSED=false
fi

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 ALL $BIN_LDMSEND TESTS PASSED! 🎉"
    echo "======================================================="
    
    # Detach trap and manually clean up on success
    trap - EXIT
    cleanup
    rm -rf "$TEST_DIR"
    exit 0
else
    echo "======================================================="
    echo " 🚨 $BIN_LDMSEND TEST FAILED 🚨"
    echo "--- Dump of $BIN_LDMSEND log ---"
    cat "$LDMSEND_LOG"
    echo "======================================================="
    exit 1
fi
