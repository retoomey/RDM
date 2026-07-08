#!/bin/bash
set -e

source ./test_utils.sh

echo "=== 🚀 Setting up pqutil Interactive Integration Test ==="
TEST_DIR="/tmp/ldm_pqutil_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

QUEUE_PATH="$TEST_DIR/var/queues/pqutil.pq"
LOG_DIR="$TEST_DIR/var/logs"
DATA_DIR="$TEST_DIR/var/data"

# Prepare directories
mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/data}

DUMMY_PAYLOAD="$DATA_DIR/payload.txt"
echo -n "Hello pqutil!" > "$DUMMY_PAYLOAD"
PAYLOAD_SIZE=$(wc -c < "$DUMMY_PAYLOAD" | awk '{print $1}')

echo "=== Phase I: Creating Product Queue ==="
$BIN_DIR/pqcreate -c -s 5M -q "$QUEUE_PATH"

echo "=== Phase II: Driving pqutil via stdin (Heredoc) ==="
PQUTIL_LOG="$LOG_DIR/pqutil_run.log"

# We feed the interactive commands directly into pqutil's stdin.
# We test setting state, showing state, and assembling a product.
$BIN_DIR/pqutil "$QUEUE_PATH" << EOF > "$PQUTIL_LOG" 2>&1
stats
set feedtype EXP
show feedtype
set ident "pqutil_test_001"
new $PAYLOAD_SIZE
put $DUMMY_PAYLOAD
write
stats
quit
EOF

echo "=== Phase III: Verifying pqutil Output ==="
PASSED=true

# 1. Verify 'stats' command executed successfully
if grep -q "Maximum Bytes Used:" "$PQUTIL_LOG"; then
    echo "✅ SUCCESS: 'stats' command successfully printed queue metrics."
else
    echo "❌ FAILURE: 'stats' output not found."
    PASSED=false
fi

# 2. Verify 'set' and 'show' state persistence
if grep -q "Feedtype is EXP" "$PQUTIL_LOG"; then
    echo "✅ SUCCESS: 'set feedtype' and 'show feedtype' correctly parsed and retained state."
else
    echo "❌ FAILURE: State mutation (set/show) failed."
    PASSED=false
fi

# 3. Verify the product was actually written to the queue
echo "=== Phase IV: Verifying Product Insertion via pqcat ==="
PQCAT_LOG="$LOG_DIR/pqcat.log"
PQCAT_OUT="$DATA_DIR/pqcat_out.txt"

# We use pqcat to dump the queue and see if our manually constructed product is there
if $BIN_DIR/pqcat -v -q "$QUEUE_PATH" -f EXP -l "$PQCAT_LOG" > "$PQCAT_OUT"; then
    if grep -q "Hello pqutil!" "$PQCAT_OUT"; then
        echo "✅ SUCCESS: The 'new' -> 'put' -> 'write' command sequence successfully built and inserted the product!"
    else
        echo "❌ FAILURE: pqcat ran, but the payload 'Hello pqutil!' was missing from the queue."
        PASSED=false
    fi
else
    echo "❌ FAILURE: pqcat failed to read the queue."
    cat "$PQCAT_LOG"
    PASSED=false
fi

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 ALL PQUTIL INTERACTIVE TESTS PASSED! 🎉"
    echo "======================================================="
    rm -rf "$TEST_DIR"
    exit 0
else
    echo "======================================================="
    echo " 🚨 PQUTIL TEST FAILED 🚨"
    echo "--- Dump of pqutil output ---"
    cat "$PQUTIL_LOG"
    echo "======================================================="
    exit 1
fi
