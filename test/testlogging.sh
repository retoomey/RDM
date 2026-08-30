#!/bin/bash
set -euo pipefail
source ./test_utils.sh

echo "=== 🚀 Setting up Logging Integration Test ==="
TEST_DIR="/tmp/ldm_logging_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

echo "=== Test 1: Explicit File Logging (-l file) ==="
FILE_LOG="$TEST_DIR/file_output.log"
QUEUE_1="$TEST_DIR/test1.pq"

# Run with -v to trigger the INFO-level "Creating..." log
"$BIN_DIR/$BIN_PQCREATE" -v -l "$FILE_LOG" -s 1M -q "$QUEUE_1"

if grep -q "Creating $QUEUE_1" "$FILE_LOG"; then
    echo "✅ SUCCESS: File sink correctly captured the log."
else
    echo "❌ FAILURE: File sink failed to capture output."
    exit 1
fi

echo "=== Test 2: Standard Error Logging (Default fallback) ==="
STDERR_LOG="$TEST_DIR/stderr_output.log"
QUEUE_2="$TEST_DIR/test2.pq"

# Redirect stderr (2>) to capture the default spdlog console output
"$BIN_DIR/$BIN_PQCREATE" -v -s 1M -q "$QUEUE_2" 2> "$STDERR_LOG"

if grep -q "Creating $QUEUE_2" "$STDERR_LOG"; then
    echo "✅ SUCCESS: Default configuration correctly routed to stderr."
else
    echo "❌ FAILURE: Default routing to stderr failed."
    exit 1
fi

echo "=== Test 3: Syslog Daemon Routing (-l syslog) ==="
QUEUE_3="$TEST_DIR/test3.pq"

# Test that the application successfully binds to the /dev/log socket without crashing
if "$BIN_DIR/$BIN_PQCREATE" -v -l syslog -s 1M -q "$QUEUE_3"; then
    echo "✅ SUCCESS: Application connected to syslog daemon without aborting."
    
    # Optional: Attempt to verify via journalctl if the environment supports it
    if command -v journalctl &> /dev/null; then
        if journalctl -t lpqcreate -n 50 | grep -q "Creating $QUEUE_3"; then
            echo "✅ SUCCESS: Log explicitly verified in the system journal."
        else
            echo "⚠️ WARNING: Command succeeded, but log wasn't found in journalctl."
            echo "   (This is normal in containers or unprivileged namespaces)."
        fi
    fi
else
    echo "❌ FAILURE: Application crashed when attempting to route to syslog."
    exit 1
fi

echo "======================================================="
echo " 🎉 ALL LOGGING SINK TESTS PASSED! 🎉"
echo "======================================================="
rm -rf "$TEST_DIR"
exit 0
