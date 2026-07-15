#!/usr/bin/env bash
set -euo pipefail

source ./test_utils.sh

echo "=== 🚀 Setting up pqsend/Push Integration Test ==="
TEST_DIR="/tmp/ldm_push_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

# Define file paths
SERVER_QUEUE="$TEST_DIR/var/queues/server.pq"
CLIENT_QUEUE="$TEST_DIR/var/queues/client.pq"
SERVER_CONF="$TEST_DIR/etc/ldmd.conf"
SERVER_LOG="$TEST_DIR/var/logs/ldmd.log"
PQSEND_LOG="$TEST_DIR/var/logs/pqsend.log"
DATA_DIR="$TEST_DIR/var/data"

mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/data,var/run}

echo "=== Phase I: Initializing Memory-Mapped Product Queues ==="
"$BIN_DIR/pqcreate" -s 10M -q "$SERVER_QUEUE"
"$BIN_DIR/pqcreate" -s 10M -q "$CLIENT_QUEUE"

echo "=== Phase II: Creating Server Configuration ==="
cat << EOF > "$SERVER_CONF"
# Allow the local machine to push data via the HIYA protocol
ACCEPT ANY .* localhost
EOF

echo "=== Phase III: Launching LDM Server Daemon ==="
"$BIN_DIR/ldmd" -P 38800 -q "$SERVER_QUEUE" -l "$SERVER_LOG" "$SERVER_CONF" &
SERVER_PID=$!
sleep 2

echo "=== Phase IV: Populating Client Queue with Test Products ==="
SMALL_PAYLOAD="$DATA_DIR/small_payload.txt"
LARGE_PAYLOAD="$DATA_DIR/large_payload.txt"

# 1. Insert a small product
echo "Hello World! This is a small product." > "$SMALL_PAYLOAD"
"$BIN_DIR/pqinsert" -q "$CLIENT_QUEUE" -f EXP -p "TEST_SMALL_PRODUCT_001" "$SMALL_PAYLOAD"

# 2. Insert a large product (>16KB)
printf "<large_data>\n" > "$LARGE_PAYLOAD"
for i in {1..500}; do
    printf "Padding chunk %04d to inflate the product size and force COMINGSOON/BLKDATA RPC fragmentation...\n" "$i" >> "$LARGE_PAYLOAD"
done
printf "</large_data>\n" >> "$LARGE_PAYLOAD"
"$BIN_DIR/pqinsert" -q "$CLIENT_QUEUE" -f EXP -p "TEST_LARGE_PRODUCT_002" "$LARGE_PAYLOAD"

echo "=== Phase V: Launching C++ pqsend to Push Data ==="
# Connect, push, and exit (-i 0)
"$BIN_DIR/pqsend" -h localhost -P 38800 -q "$CLIENT_QUEUE" -f ANY -o 60 -i 0 -x > "$PQSEND_LOG" 2>&1

echo "=== Phase VI: Verifying Ingestion Success ==="
PQCAT_OUT="$DATA_DIR/pqcat_out.txt"
"$BIN_DIR/pqcat" -q "$SERVER_QUEUE" -f ANY -p "TEST_.*" > "$PQCAT_OUT"

PASSED=true

# Check 1: Did pqcat extract both files?
if grep -q "Hello World" "$PQCAT_OUT" && grep -q "Padding chunk 0500" "$PQCAT_OUT"; then
    echo "✅ SUCCESS: Both small and large products were successfully pushed and read from the server queue."
else
    echo "❌ FAILURE: Products missing from the server's product queue."
    PASSED=false
fi

# Check 2: Did the small product trigger HEREIS?
if grep -q "DEBUG: Firing first HEREIS" "$PQSEND_LOG"; then
    echo "✅ SUCCESS: Small product successfully routed through HEREIS."
else
    echo "❌ FAILURE: HEREIS logic was not triggered by pqsend."
    PASSED=false
fi

# Check 3: Did the large product trigger COMINGSOON?
if grep -q "DEBUG: Firing first COMINGSOON/BLKDATA" "$PQSEND_LOG"; then
    echo "✅ SUCCESS: Large product successfully routed through COMINGSOON/BLKDATA."
else
    echo "❌ FAILURE: COMINGSOON logic was not triggered by pqsend. (Is the payload too small?)"
    PASSED=false
fi

echo "=== Shutting down LDM Server ==="
kill -TERM "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 ALL PQSEND PUSH TESTS PASSED! 🎉"
    echo "======================================================="
    rm -rf "$TEST_DIR"
    exit 0
else
    echo "======================================================="
    echo " 🚨 PQSEND PUSH TEST FAILED 🚨"
    echo "--- Dump of pqsend log ---"
    cat "$PQSEND_LOG"
    echo "======================================================="
    exit 1
fi
