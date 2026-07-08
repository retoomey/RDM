#!/bin/bash
# Local Network Integration test for modernized LDM tools.
# Purpose: Spins up a local upstream feeder on an unprivileged port, 
# subscribes to it using our upgraded notifyme client, and triggers a product.

set -e

source ./test_utils.sh

echo "=== Setting up Local Network Test Environment ==="

TEST_DIR="/tmp/ldm_local_net_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

UP_DIR="$TEST_DIR/upstream"

mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/data,var/run}

# 1. Create a dummy XML file to insert
DUMMY_XML="$TEST_DIR/dummy_rpc_input.xml"
cat << 'EOF' > "$DUMMY_XML"
<?xml version="1.0" encoding="UTF-8"?>
<weather_data>
    <station>KTLX</station>
    <status>Local RPC Transfer Successful</status>
</weather_data>
EOF

# 2. UPSTREAM configuration 
UP_CONF="$UP_DIR/etc/ldmd.conf"
# FIX: Use a wildcard host pattern (.*) to bypass reverse-DNS/localhost quirks!
echo "ALLOW ANY .* ^(test_.*\.xml)$" > "$UP_CONF"

# 3. Create the product queue
echo "=== Creating Product Queue ==="
$BIN_DIR/pqcreate -c -s 10M -q "$UP_DIR/var/queues/up.pq"

# 4. Start the Upstream LDM Daemon on custom port 6000
echo "=== Starting Upstream LDM Daemon (Port 6000) ==="
cd "$UP_DIR/var/run" || exit 1
$BIN_DIR/ldmd -x -P 6000 -q "$UP_DIR/var/queues/up.pq" -l - "$UP_CONF" > "$UP_DIR/var/logs/ldmd.log" 2>&1 &
UP_PID=$!
cd - > /dev/null

sleep 2 

# 5. Start our upgraded NOTIFYME client on the custom port
echo "=== Subscribing via NOTIFYME (Port 6000) ==="
NOTIFY_LOG="$UP_DIR/var/logs/notifyme.log"
# -T 10 ensures it will automatically shut itself down after 10 seconds. 
# -t 5 ensures individual RPC timeouts are safely smaller than the total timeout.
#$BIN_DIR/notifyme -P 6000 -h localhost -f EXP -v -T 10 -t 5 > "$NOTIFY_LOG" 2>&1 &
$BIN_DIR/notifyme -P 6000 -h localhost -f EXP -v -o 3600 -T 10 -t 5 > "$NOTIFY_LOG" 2>&1 &
NOTIFY_PID=$!

sleep 2

# 6. Insert the dummy XML file into the queue
echo "=== Inserting XML via pqinsert ==="
$BIN_DIR/pqinsert -x -q "$UP_DIR/var/queues/up.pq" -f EXP -p "test_weather.xml" "$DUMMY_XML"

# Wake up the upstream feeder
kill -CONT $UP_PID

echo "Waiting for notifyme to complete its lifecycle..."
wait $NOTIFY_PID 2>/dev/null || true

# 7. Verify the output
echo -e "\n=== Verifying NOTIFYME Reception ==="
if grep -q "test_weather.xml" "$NOTIFY_LOG"; then
    echo "✅ SUCCESS: notifyme successfully connected to Port 6000 and received the product metadata!"
else
    echo "❌ FAILURE: notifyme did not log the product."
    echo -e "\n--- NOTIFYME LOG DUMP ---"
    cat "$NOTIFY_LOG"
    echo -e "\n--- LDMD LOG DUMP ---"
    cat "$UP_DIR/var/logs/ldmd.log"
fi

# 8. Tear down
echo "=== Shutting down LDM ==="
kill -TERM $UP_PID
wait $UP_PID 2>/dev/null || true

echo "=== Test Complete ==="
