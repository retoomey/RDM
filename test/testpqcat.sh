#!/bin/bash
set -e

source ./test_utils.sh

echo "=== 🚀 Setting up pqcat Integration Test Environment ==="

TEST_DIR="/tmp/ldm_pqcat_test"
sandbox_ldm "$TEST_DIR"

QUEUE_PATH="$TEST_DIR/var/queues/pqcat.pq"
LOG_DIR="$TEST_DIR/var/logs"

# Clean and prepare test directories
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/data}

DUMMY_PAYLOAD_1="$TEST_DIR/var/data/payload_1.txt"
DUMMY_PAYLOAD_2="$TEST_DIR/var/data/payload_2.txt"

echo "Product 1: Hello from pqcat test!" > "$DUMMY_PAYLOAD_1"
echo "Product 2: Second product in the queue." > "$DUMMY_PAYLOAD_2"

echo "=== Phase I: Creating Product Queue ==="
$BIN_DIR/pqcreate -c -s 5M -q "$QUEUE_PATH"

echo "=== Phase II: Inserting Test Products ==="
# Note: Using default pqinsert behavior to generate MD5 from the data body
$BIN_DIR/pqinsert -q "$QUEUE_PATH" -f EXP -p "pqcat_test_001.txt" "$DUMMY_PAYLOAD_1"
$BIN_DIR/pqinsert -q "$QUEUE_PATH" -f EXP -p "pqcat_test_002.txt" "$DUMMY_PAYLOAD_2"

echo "=== Phase III: Running pqcat (Data Extraction & MD5 Check) ==="
PQCAT_LOG="$LOG_DIR/pqcat_run.log"
PQCAT_DATA="$LOG_DIR/pqcat_output.dat"

# Run WITHOUT '-s' to ensure data is written to stdout
if $BIN_DIR/pqcat -v -c -q "$QUEUE_PATH" -f EXP -l "$PQCAT_LOG" > "$PQCAT_DATA"; then
    echo "✅ SUCCESS: pqcat extraction executed successfully."
else
    echo "❌ FAILURE: pqcat extraction failed."
    cat "$PQCAT_LOG"
    exit 1
fi

echo "=== Phase IV: Running pqcat (Sanity Check) ==="
PQCAT_SANITY_LOG="$LOG_DIR/pqcat_sanity.log"

# Run WITH '-s' to ensure the tally functionality works
if $BIN_DIR/pqcat -v -s -q "$QUEUE_PATH" -f EXP -l "$PQCAT_SANITY_LOG" > /dev/null; then
    echo "✅ SUCCESS: pqcat sanity check executed successfully."
else
    echo "❌ FAILURE: pqcat sanity check failed."
    cat "$PQCAT_SANITY_LOG"
    exit 1
fi

echo "=== Phase V: Verifying Outputs ==="
PASSED=true

# 1. Verify Data Extraction
if grep -q "Hello from pqcat test!" "$PQCAT_DATA" && grep -q "Second product in the queue." "$PQCAT_DATA"; then
    echo "✅ SUCCESS: pqcat correctly extracted the product data to stdout!"
else
    echo "❌ FAILURE: The expected product data was not found in stdout."
    echo "--- Dump of pqcat stdout ---"
    cat "$PQCAT_DATA"
    PASSED=false
fi

# 2. Verify Sanity Check Log
if grep -q "consistent with value in queue" "$PQCAT_SANITY_LOG"; then
    echo "✅ SUCCESS: Queue sanity check (-s) passed successfully!"
else
    echo "❌ FAILURE: Queue sanity check did not log success message."
    PASSED=false
fi

# 3. Verify MD5 Signatures
if grep -q "signature mismatch" "$PQCAT_LOG"; then
    echo "❌ FAILURE: pqcat (-c) reported an MD5 signature mismatch!"
    PASSED=false
else
    echo "✅ SUCCESS: MD5 signatures verified flawlessly."
fi

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 ALL PQCAT TESTS PASSED! 🎉"
    echo "======================================================="
    rm -rf "$TEST_DIR"
    exit 0
else
    echo "======================================================="
    echo " 🚨 PQCAT TEST FAILED 🚨"
    echo "======================================================="
    cat "$PQCAT_LOG"
    exit 1
fi
