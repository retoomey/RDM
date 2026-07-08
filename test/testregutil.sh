#!/bin/bash
set -e

source ./test_utils.sh

echo "=== 🚀 Setting up regutil Integration Test Environment ==="

TEST_DIR="/tmp/ldm_regutil_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

REG_DIR="$TEST_DIR/etc"

# Prepare test directories
mkdir -p "$REG_DIR"
mkdir -p "$TEST_DIR/var/logs"

echo "=== Phase I: Initializing Registry ==="
# -c creates the registry if it doesn't exist
$BIN_DIR/regutil -d "$REG_DIR" -c

echo "=== Phase II: Populating Registry Parameters ==="
$BIN_DIR/regutil -d "$REG_DIR" -s "foo value" /foo_key
$BIN_DIR/regutil -d "$REG_DIR" -b TRUE /fooBool_key
$BIN_DIR/regutil -d "$REG_DIR" -u 42 /fooInt_key
$BIN_DIR/regutil -d "$REG_DIR" -s "string value 2" /subkey/string_key
$BIN_DIR/regutil -d "$REG_DIR" -s "to_be_deleted" /temp_key

echo "=== Phase III: Deleting a Parameter ==="
$BIN_DIR/regutil -d "$REG_DIR" -r /temp_key

echo "=== Phase IV: Verifying Output ==="
ACTUAL_OUTPUT="$TEST_DIR/var/logs/regutil_actual.txt"
EXPECTED_OUTPUT="$TEST_DIR/var/logs/regutil_expected.txt"

# Dump the entire registry and sort it to ensure consistent diffing
$BIN_DIR/regutil -d "$REG_DIR" | sort > "$ACTUAL_OUTPUT"

# Create our expected baseline
cat << 'EOF' | sort > "$EXPECTED_OUTPUT"
/fooBool_key : TRUE
/fooInt_key : 42
/foo_key : foo value
/subkey/string_key : string value 2
EOF

PASSED=true
if diff -u "$EXPECTED_OUTPUT" "$ACTUAL_OUTPUT"; then
    echo "✅ SUCCESS: Registry contents match expectations perfectly!"
else
    echo "❌ FAILURE: Registry contents differ from expectations."
    PASSED=false
fi

# Test quiet failure on a missing key
if $BIN_DIR/regutil -d "$REG_DIR" -q /does_not_exist; then
    echo "❌ FAILURE: regutil should have exited with a non-zero status for a missing key."
    PASSED=false
else
    echo "✅ SUCCESS: regutil correctly identified a missing key."
fi

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 ALL REGUTIL TESTS PASSED! 🎉"
    echo "======================================================="
    rm -rf "$TEST_DIR"
    exit 0
else
    echo "======================================================="
    echo " 🚨 REGUTIL TEST FAILED 🚨"
    echo "======================================================="
    exit 1
fi
