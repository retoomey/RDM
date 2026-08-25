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

echo "Product 1: Hello from $BIN_PQCAT test!" > "$DUMMY_PAYLOAD_1"
echo "Product 2: Second product in the queue." > "$DUMMY_PAYLOAD_2"

echo "=== Phase I: Creating Product Queue ==="
$BIN_DIR/$BIN_PQCREATE -c -s 5M -q "$QUEUE_PATH"

echo "=== Phase II: Inserting Test Products ==="
# Note: Using default pqinsert behavior to generate MD5 from the data body
$BIN_DIR/$BIN_PQINSERT -q "$QUEUE_PATH" -f EXP -p "pqcat_test_001.txt" "$DUMMY_PAYLOAD_1"
$BIN_DIR/$BIN_PQINSERT -q "$QUEUE_PATH" -f EXP -p "pqcat_test_002.txt" "$DUMMY_PAYLOAD_2"

echo "=== Phase III: Running $BIN_PQCAT with -d (Full Payload Data) ==="
PQCAT_DATA_LOG="$LOG_DIR/pqcat_data_run.log"
PQCAT_DATA_OUT="$LOG_DIR/pqcat_data_output.dat"

# Run WITH '-d' to dump raw product payload to stdout
if $BIN_DIR/$BIN_PQCAT -d -v -c -q "$QUEUE_PATH" -f EXP -l "$PQCAT_DATA_LOG" > "$PQCAT_DATA_OUT"; then
    echo "✅ SUCCESS: $BIN_PQCAT (-d) full data dump executed successfully."
else
    echo "❌ FAILURE: $BIN_PQCAT (-d) full data dump failed."
    cat "$PQCAT_DATA_LOG"
    exit 1
fi

echo "=== Phase IV: Running $BIN_PQCAT without -d (Metadata Only) ==="
PQCAT_META_LOG="$LOG_DIR/pqcat_meta_run.log"
PQCAT_META_OUT="$LOG_DIR/pqcat_meta_output.dat"

# Run WITHOUT '-d' to ensure ONLY metadata summaries are dumped to stdout
if $BIN_DIR/$BIN_PQCAT -v -c -q "$QUEUE_PATH" -f EXP -l "$PQCAT_META_LOG" > "$PQCAT_META_OUT"; then
    echo "✅ SUCCESS: $BIN_PQCAT metadata-only dump executed successfully."
else
    echo "❌ FAILURE: $BIN_PQCAT metadata-only dump failed."
    cat "$PQCAT_META_LOG"
    exit 1
fi

echo "=== Phase V: Running $BIN_PQCAT (Sanity Check) ==="
PQCAT_SANITY_LOG="$LOG_DIR/pqcat_sanity.log"

# Run WITH '-s' to ensure the tally functionality works
if $BIN_DIR/$BIN_PQCAT -v -s -q "$QUEUE_PATH" -f EXP -l "$PQCAT_SANITY_LOG" > /dev/null; then
    echo "✅ SUCCESS: $BIN_PQCAT sanity check executed successfully."
else
    echo "❌ FAILURE: $BIN_PQCAT sanity check failed."
    cat "$PQCAT_SANITY_LOG"
    exit 1
fi

echo "=== Phase VI: Verifying Outputs ==="
PASSED=true

# 1. Verify Full Payload Extraction (-d flag)
if grep -q "Hello from $BIN_PQCAT test!" "$PQCAT_DATA_OUT" && grep -q "Second product in the queue." "$PQCAT_DATA_OUT"; then
    echo "✅ SUCCESS: $BIN_PQCAT -d correctly extracted the product data to stdout!"
else
    echo "❌ FAILURE: The expected product data payload was not found in stdout."
    echo "--- Dump of $BIN_PQCAT -d stdout ---"
    cat "$PQCAT_DATA_OUT"
    PASSED=false
fi

# 2. Verify Metadata-Only Extraction (no -d flag)
if grep -q "Hello from $BIN_PQCAT test!" "$PQCAT_META_OUT" || grep -q "Second product in the queue." "$PQCAT_META_OUT"; then
    echo "❌ FAILURE: $BIN_PQCAT without -d accidentally dumped binary payload to stdout!"
    PASSED=false
elif grep -q "pqcat_test_001.txt" "$PQCAT_META_OUT" && grep -q "pqcat_test_002.txt" "$PQCAT_META_OUT"; then
    echo "✅ SUCCESS: $BIN_PQCAT without -d extracted metadata headers without dumping product payloads!"
else
    echo "❌ FAILURE: $BIN_PQCAT without -d failed to print product metadata identifiers to stdout."
    echo "--- Dump of $BIN_PQCAT metadata stdout ---"
    cat "$PQCAT_META_OUT"
    PASSED=false
fi

# 3. Verify Sanity Check Log
if grep -q "consistent with value in queue" "$PQCAT_SANITY_LOG"; then
    echo "✅ SUCCESS: Queue sanity check (-s) passed successfully!"
else
    echo "❌ FAILURE: Queue sanity check did not log success message."
    PASSED=false
fi

# 4. Verify MD5 Signatures
if grep -q "signature mismatch" "$PQCAT_DATA_LOG" || grep -q "signature mismatch" "$PQCAT_META_LOG"; then
    echo "❌ FAILURE: $BIN_PQCAT (-c) reported an MD5 signature mismatch!"
    PASSED=false
else
    echo "✅ SUCCESS: MD5 signatures verified flawlessly."
fi

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 ALL $BIN_PQCAT TESTS PASSED! 🎉"
    echo "======================================================="
    rm -rf "$TEST_DIR"
    exit 0
else
    echo "======================================================="
    echo " 🚨 $BIN_PQCAT TEST FAILED 🚨"
    echo "======================================================="
    cat "$PQCAT_DATA_LOG"
    exit 1
fi
