#!/bin/bash
# testBrokerPortHandoff.sh
# Purpose: Tests whether rdmd correctly passes the custom listening port 
# down to spawned brokers (preventing the unprivileged port 388 crash).

set -e

source ./test_utils.sh

# 1. Root Check: The bug is completely masked if run as root.
if [ "$(id -u)" -eq 0 ]; then
    echo "SKIP: This test must be run as a non-root user to expose the privilege bug."
    exit 0
fi

echo "=== Setting up Broker Port Handoff Test ==="

# 2. Define workspace and sandbox
TEST_DIR="/tmp/ldm_broker_test_env"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

TEST_PORT=18388
UP_CONF="$TEST_DIR/etc/ldmd.conf"
LOG_PATH="$TEST_DIR/var/logs/ldmd.log"
QUEUE_PATH="$TEST_DIR/var/queues/up.pq"

# 3. Create config with an ALLOW rule to trigger RequiresServer() == true
echo "ALLOW ANY ^127\.0\.0\.1$|^localhost$" > "$UP_CONF"

# 4. Create the product queue
echo "=== Creating Product Queue ==="
$BIN_DIR/$BIN_PQCREATE -c -s 1M -q "$QUEUE_PATH"

# 5. Start the Daemon on a custom high port
echo "=== Starting $BIN_LDMD Daemon (Port $TEST_PORT) ==="
$BIN_DIR/$BIN_LDMD -x -P "$TEST_PORT" -q "$QUEUE_PATH" -l "$LOG_PATH" "$UP_CONF" > /dev/null 2>&1 &
UP_PID=$!

sleep 2

# 6. Trigger the Broker Spawn
# Using rdmcat to connect to our custom port. This forces rdmd to evaluate the
# ALLOW rule and fork a broker to handle the connection.
echo "=== Connecting via $BIN_RDMCAT to trigger broker spawn ==="
$BIN_DIR/$BIN_RDMCAT -h localhost -P "$TEST_PORT" -T 3 >/dev/null 2>&1 || true

sleep 2

# 7. Evaluate the Results
PASSED=true
echo "=== Evaluating Logs for Privilege Crash ==="

# Check specifically for the fatal configuration error thrown during a privilege drop failure
if grep -q "CRITICAL CONFIGURATION ERROR" "$LOG_PATH"; then
    echo "❌ FAILURE: BUG EXPOSED!"
    echo "The broker crashed due to an unprivileged port fallback."
    echo "--- Log Output ---"
    grep "CRITICAL" "$LOG_PATH" || true
    echo "------------------"
    PASSED=false
else
    echo "✅ SUCCESS: No privilege crash detected. The port was passed correctly!"
fi

# 8. Tear down
echo "=== Shutting down ==="
stop_ldm_daemon $UP_PID

if [ "$PASSED" = true ]; then
    exit 0
else
    exit 1
fi
