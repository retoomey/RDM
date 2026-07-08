#!/bin/bash
set -e

source ./test_utils.sh

echo "=== 🚀 Setting up LDM IPv6 Integration Test Environment ==="
TEST_DIR="/tmp/ldm_ipv6_test_env"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

UP_DIR="$TEST_DIR/upstream"
DOWN_DIR="$TEST_DIR/downstream"

mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/data,var/run}
mkdir -p "$DOWN_DIR"/{etc,var/queues,var/logs,var/data,var/run}

DUMMY_XML="$TEST_DIR/dummy_ipv6_input.xml"
cat << 'EOF' > "$DUMMY_XML"
<?xml version="1.0" encoding="UTF-8"?>
<weather_data>
    <station>KTLX</station>
    <status>IPv6 RPC Transfer Successful</status>
</weather_data>
EOF

# Upstream ACL: Explicitly allow the IPv6 loopback address
UP_CONF="$UP_DIR/etc/ldmd.conf"
echo "ALLOW ANY ^\[::1\]$|^localhost$ ^(test_.*\.xml)$" > "$UP_CONF"

DOWN_PQACT_CONF="$DOWN_DIR/etc/pqact.conf"
printf 'EXP\t^(test_.*\\.xml)$\tFILE\t-overwrite -close\t%s/var/data/\\1\n' "$DOWN_DIR" > "$DOWN_PQACT_CONF"

# Downstream Request: Explicitly target the IPv6 loopback address using bracket notation
DOWN_CONF="$DOWN_DIR/etc/ldmd.conf"
echo "REQUEST EXP \"^(test_.*\.xml)$\" [::1]:6006" > "$DOWN_CONF"
echo "EXEC \"$BIN_DIR/pqact -x -d $DOWN_DIR -l $DOWN_DIR/var/logs/pqact.log -o 3600 -i 1 -f EXP -q $DOWN_DIR/var/queues/down.pq $DOWN_PQACT_CONF\"" >> "$DOWN_CONF"

echo "=== Creating Product Queues ==="
$BIN_DIR/pqcreate -c -s 10M -q "$UP_DIR/var/queues/up.pq"
$BIN_DIR/pqcreate -c -s 10M -q "$DOWN_DIR/var/queues/down.pq"

echo "=== Starting Upstream LDM Daemon (Port 6006) ==="
$BIN_DIR/ldmd -x -P 6006 -q "$UP_DIR/var/queues/up.pq" -l - "$UP_CONF" > "$UP_DIR/var/logs/ldmd.log" 2>&1 &
UP_PID=$!

sleep 2

echo "=== Testing ldmping over IPv6 loopback ==="
PING_LOG="$TEST_DIR/ping.log"
if $BIN_DIR/ldmping -v -i 0 -P 6006 -h ::1 > "$PING_LOG" 2>&1; then
    echo "✅ SUCCESS: ldmping successfully detected the active server via IPv6 [::1]."
else
    echo "❌ FAILURE: ldmping failed over IPv6."
    cat "$PING_LOG"
    kill -TERM $UP_PID
    exit 1
fi

echo "=== Starting Downstream LDM Daemon (Port 6007) ==="
cd "$DOWN_DIR/var/run" || exit 1
$BIN_DIR/ldmd -x -P 6007 -q "$DOWN_DIR/var/queues/down.pq" -l - "$DOWN_CONF" > "$DOWN_DIR/var/logs/ldmd.log" 2>&1 &
DOWN_PID=$!
cd - > /dev/null

sleep 4

echo "=== Inserting XML into UPSTREAM via pqinsert ==="
$BIN_DIR/pqinsert -x -l "$UP_DIR/var/logs/pqinsert.log" \
          -q "$UP_DIR/var/queues/up.pq" \
          -f EXP \
          -p "test_ipv6_weather.xml" \
          "$DUMMY_XML"

kill -CONT -$UP_PID
sleep 3

echo "=== Verifying IPv6 RPC Transfer Output ==="
EXPECTED_OUTPUT="$DOWN_DIR/var/data/test_ipv6_weather.xml"
PASSED=true

if [ -f "$EXPECTED_OUTPUT" ]; then
    echo "✅ SUCCESS: The XML file was successfully processed and written to disk over an IPv6 stream!"
    diff -u "$DUMMY_XML" "$EXPECTED_OUTPUT" && echo "✅ SUCCESS: File contents match perfectly!"
else
    echo "❌ FAILURE: The file was not transferred/written to disk."
    PASSED=false
    echo "Check Upstream log:   cat $UP_DIR/var/logs/ldmd.log"
    echo "Check Downstream log: cat $DOWN_DIR/var/logs/ldmd.log"
fi

echo "=== Shutting down LDMs ==="
kill -TERM -$DOWN_PID -$UP_PID 2>/dev/null || true
wait $DOWN_PID 2>/dev/null || true
wait $UP_PID 2>/dev/null || true

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 LDM IPv6 multi-server test passed! 🎉"
    echo "======================================================="
    exit 0
else
    echo "======================================================="
    echo " 🚨 LDM IPv6 multi-server test failed 🚨"
    echo "======================================================="
    exit 1
fi
