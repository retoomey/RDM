#!/bin/bash
# Toomey May 2026
# RPC Integration test for LDM networking.
# Purpose: Tests the LDM 6 network stack by spinning up an upstream
# feeder and a downstream requester on local, unprivileged ports.

# Exit immediately if a command exits with a non-zero status
set -e

source ./test_utils.sh

echo "=== Setting up LDM RPC Network Test Environment ==="

# 1. Define workspace and tool paths (assuming unified bin directory)
TEST_DIR="/tmp/ldm_rpc_test_env"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

UP_DIR="$TEST_DIR/upstream"
DOWN_DIR="$TEST_DIR/downstream"

# 2. Create fresh directories
mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/data,var/run}
mkdir -p "$DOWN_DIR"/{etc,var/queues,var/logs,var/data,var/run}

# 3. Create a dummy XML file to insert
DUMMY_XML="$TEST_DIR/dummy_rpc_input.xml"
cat << 'EOF' > "$DUMMY_XML"
<?xml version="1.0" encoding="UTF-8"?>
<weather_data>
    <station>KTLX</station>
    <status>RPC Transfer Successful</status>
</weather_data>
EOF

# 4. UPSTREAM configuration
UP_CONF="$UP_DIR/etc/ldmd.conf"
# Allow localhost to request ANY feedtype matching our XML pattern
# Syntax: ALLOW <feedtype> <host_pattern> <product_pattern>
echo "ALLOW ANY ^127\.0\.0\.1$|^localhost$ ^(test_.*\.xml)$" > "$UP_CONF"

# 5. DOWNSTREAM configurations
DOWN_PQACT_CONF="$DOWN_DIR/etc/pqact.conf"
# Save the received file into the downstream data directory
printf 'EXP\t^(test_.*\\.xml)$\tFILE\t-overwrite -close\t%s/var/data/\\1\n' "$DOWN_DIR" > "$DOWN_PQACT_CONF"

DOWN_CONF="$DOWN_DIR/etc/ldmd.conf"
# Request the data from the Upstream LDM daemon listening on port 6000
echo "REQUEST EXP \"^(test_.*\.xml)$\" 127.0.0.1:6000" > "$DOWN_CONF"
# Start the downstream pqact
echo "EXEC \"$BIN_DIR/pqact -x -d $DOWN_DIR -l $DOWN_DIR/var/logs/pqact.log -o 3600 -i 1 -f EXP -q $DOWN_DIR/var/queues/down.pq $DOWN_PQACT_CONF\"" >> "$DOWN_CONF"

# 6. Create the product queues
echo "=== Creating Product Queues ==="
$BIN_DIR/pqcreate -c -s 10M -q "$UP_DIR/var/queues/up.pq"
$BIN_DIR/pqcreate -c -s 10M -q "$DOWN_DIR/var/queues/down.pq"

# 7. Start the Daemons
echo "=== Starting Upstream LDM Daemon (Port 6000) ==="
$BIN_DIR/ldmd -x -P 6000 -q "$UP_DIR/var/queues/up.pq" -l - "$UP_CONF" > "$UP_DIR/var/logs/ldmd.log" 2>&1 &
UP_PID=$!

echo "=== Starting Downstream LDM Daemon (Port 6001) ==="
# cd into the test environment so the .info state file drops in var/run
cd "$DOWN_DIR/var/run" || exit 1

$BIN_DIR/ldmd -x -P 6001 -q "$DOWN_DIR/var/queues/down.pq" -l - "$DOWN_CONF" > "$DOWN_DIR/var/logs/ldmd.log" 2>&1 &
DOWN_PID=$!

# Return to the previous directory
cd - > /dev/null

# Give the daemons time to hand-shake and negotiate the HIYA/FEEDME protocol
sleep 4 

# 8. Insert the dummy XML file into the UPSTREAM queue
echo "=== Inserting XML into UPSTREAM via pqinsert ==="
$BIN_DIR/pqinsert -x -l "$UP_DIR/var/logs/pqinsert.log" \
          -q "$UP_DIR/var/queues/up.pq" \
          -f EXP \
          -p "test_weather.xml" \
          "$DUMMY_XML"

# Manually wake up the upstream feeder process group so it sees the new product instantly
kill -CONT -$UP_PID

# Give the network transfer and downstream pqact a moment to process
sleep 3

# 9. Verify the output made it to the DOWNSTREAM data directory
echo "=== Verifying RPC Transfer Output ==="
EXPECTED_OUTPUT="$DOWN_DIR/var/data/test_weather.xml"

PASSED=true
if [ -f "$EXPECTED_OUTPUT" ]; then
    echo "✅ SUCCESS: The XML file was successfully processed and written to disk!"
    diff -u "$DUMMY_XML" "$EXPECTED_OUTPUT" && echo "✅ SUCCESS: File contents match perfectly!"
else
    echo "❌ FAILURE: The file was not transferred/written to disk."
    PASSED=false
    echo "Check Upstream log:   cat $UP_DIR/var/logs/ldmd.log"
    echo "Check Downstream log: cat $DOWN_DIR/var/logs/ldmd.log"
    echo "Check Downstream pqact log: cat $DOWN_DIR/var/logs/pqact.log"
fi

# 10. Tear down the daemons safely
echo "=== Shutting down LDMs ==="
#kill -TERM $DOWN_PID $UP_PID
kill -TERM -$DOWN_PID -$UP_PID 2>/dev/null || true
wait $DOWN_PID 2>/dev/null || true
wait $UP_PID 2>/dev/null || true

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 LDM multi server test passed! 🎉"
    echo "Upstream log:   cat $UP_DIR/var/logs/ldmd.log"
    echo "Downstream log: cat $DOWN_DIR/var/logs/ldmd.log"
    echo "======================================================="
    exit 0
else
    echo "======================================================="
    echo " 🚨 LDM multi server test failed 🚨"
    echo "======================================================="
    exit 1
fi
