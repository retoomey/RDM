#!/bin/bash
# Local Network Integration test for rdmpull.
# Purpose: Spins up a local upstream feeder on an unprivileged port, 
# tests rdmpull for full payload retrieval (feedme), and tests 
# rdmpull -m for metadata-only retrieval (notifyme).

set -e

source ./test_utils.sh

echo "=== 🚀 Setting up rdmpull Integration Test Environment ==="
TEST_DIR="/tmp/ldm_rdmpull_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

UP_DIR="$TEST_DIR/upstream"
LDM_PORT="6005"

mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/run,var/db}

# UPSTREAM configuration
UP_CONF="$UP_DIR/etc/ldmd.conf"
# Use a wildcard host pattern (.*) to bypass reverse-DNS/localhost quirks!
echo "ALLOW ANY .* .*" > "$UP_CONF"

echo "=== Creating Upstream Product Queue ==="
$BIN_DIR/pqcreate -c -s 5M -q "$UP_DIR/var/queues/up.pq"

echo "=== Starting Upstream LDM Daemon (Port $LDM_PORT) ==="
cd "$UP_DIR" || exit 1
$BIN_DIR/ldmd -x -P $LDM_PORT -q "var/queues/up.pq" -l - "etc/ldmd.conf" > "var/logs/ldmd.log" 2>&1 &
UP_PID=$!
cd - > /dev/null

# Ensure LDM shuts down even if the script fails and exits early
cleanup() {
    echo "=== Shutting down processes ==="
    kill -TERM $UP_PID 2>/dev/null || true
    wait $UP_PID 2>/dev/null || true
}
trap cleanup EXIT

# Give the daemon a moment to bind to the port
sleep 1

# Create some dummy payloads
FULL_PAYLOAD_XML="$TEST_DIR/dummy_full.xml"
META_PAYLOAD_XML="$TEST_DIR/dummy_meta.xml"

cat << 'EOF' > "$FULL_PAYLOAD_XML"
<?xml version="1.0" encoding="UTF-8"?>
<weather_data><status>FULL_PAYLOAD_SUCCESS</status></weather_data>
EOF

cat << 'EOF' > "$META_PAYLOAD_XML"
<?xml version="1.0" encoding="UTF-8"?>
<weather_data><status>META_PAYLOAD_SHOULD_BE_HIDDEN</status></weather_data>
EOF

echo ""
echo "======================================================="
echo " TEST 1: Full Pull (-m omitted, equivalent to feedme)"
echo "======================================================="
FULL_OUT="$TEST_DIR/pull_full.out"
FULL_ERR="$TEST_DIR/pull_full.err"

# Run without -m, WITH -v, and using a wide offset to avoid timing misses
$BIN_DIR/rdmpull -v -o 3600 -h localhost -P $LDM_PORT -p "TEST_FULL" > "$FULL_OUT" 2> "$FULL_ERR" &
PULL_FULL_PID=$!

# Give ldmd time to accept the TCP connection and fork the downstream feeder
sleep 1

# Insert the dummy XML file into the queue. Condvars will immediately wake the feeder.
echo "Inserting FULL test payload..."
$BIN_DIR/pqinsert -v -q "$UP_DIR/var/queues/up.pq" -p "TEST_FULL_PRODUCT" "$FULL_PAYLOAD_XML"

# Give the product time to traverse the RPC/TCP network socket
sleep 1

# Terminate the background rdmpull process
kill -TERM $PULL_FULL_PID 2>/dev/null || true
wait $PULL_FULL_PID 2>/dev/null || true

# Verify the output
if grep -q "FULL_PAYLOAD_SUCCESS" "$FULL_OUT"; then
    echo "✅ SUCCESS: rdmpull successfully downloaded the full payload to stdout!"
else
    echo "❌ FAILURE: rdmpull failed to output the full payload."
    echo "--- stdout dump ---"
    cat "$FULL_OUT"
    echo "--- stderr dump ---"
    cat "$FULL_ERR"
    exit 1
fi

echo ""
echo "======================================================="
echo " TEST 2: Metadata Only (-m flag, equivalent to notifyme)"
echo "======================================================="

META_OUT="$TEST_DIR/pull_meta.out"
META_ERR="$TEST_DIR/pull_meta.err"

# Run WITH -m, WITH -v, and using a wide offset
$BIN_DIR/rdmpull -m -v -o 3600 -h localhost -P $LDM_PORT -p "TEST_META" > "$META_OUT" 2> "$META_ERR" &
PULL_META_PID=$!

sleep 1

echo "Inserting META test payload..."
$BIN_DIR/pqinsert -v -q "$UP_DIR/var/queues/up.pq" -p "TEST_META_PRODUCT" "$META_PAYLOAD_XML"

sleep 1

kill -TERM $PULL_META_PID 2>/dev/null || true
wait $PULL_META_PID 2>/dev/null || true

# Verify the output: Payload should NOT be in stdout, but the ID should be in stderr
if grep -q "META_PAYLOAD_SHOULD_BE_HIDDEN" "$META_OUT"; then
    echo "❌ FAILURE: rdmpull -m accidentally downloaded and printed the full payload."
    exit 1
elif grep -q "TEST_META_PRODUCT" "$META_ERR"; then
    echo "✅ SUCCESS: rdmpull -m successfully retrieved metadata without downloading the payload!"
else
    echo "❌ FAILURE: rdmpull -m failed to log the product metadata."
    echo "--- stderr dump ---"
    cat "$META_ERR"
    exit 1
fi

echo ""
echo "======================================================="
echo " 🎉 ALL RDMPULL TESTS PASSED! 🎉"
echo "======================================================="

rm -rf "$TEST_DIR"
# The 'trap' takes care of the final LDM daemon shutdown
exit 0
