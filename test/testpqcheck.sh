#!/bin/bash
set -euo pipefail
source ./test_utils.sh

# BIN_PQCHECK is not exported by test_utils.sh by default
BIN_PQCHECK="lpqcheck"

echo "=== 🚀 Setting up lpqcheck Integration Test ==="
TEST_DIR="/tmp/ldm_pqcheck_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

QUEUE_PATH="$TEST_DIR/var/queues/test.pq"
LDMD_CONF="$TEST_DIR/etc/ldmd.conf"

mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/run}
# Add a REQUEST rule so rdmd spawns a child that actively holds the queue open for writing
cat << EOF > "$LDMD_CONF"
ACCEPT ANY .* localhost
REQUEST ANY ".*" 127.0.0.1:38888
EOF

echo "=== Phase I: Clean State Verification ==="
"$BIN_DIR/$BIN_PQCREATE" -s 2M -q "$QUEUE_PATH"

if "$BIN_DIR/$BIN_PQCHECK" -q "$QUEUE_PATH"; then
    echo "✅ SUCCESS: Clean queue returned exit code 0."
else
    echo "❌ FAILURE: lpqcheck failed on a clean queue."
    exit 1
fi

echo "=== Phase II: Active Writer Detection ==="
"$BIN_DIR/$BIN_LDMD" -P 38888 -q "$QUEUE_PATH" -l "$TEST_DIR/var/logs/ldmd.log" "$LDMD_CONF" &
LDMD_PID=$!
sleep 2

# Disable set -e temporarily to safely capture the non-zero exit code
set +e
"$BIN_DIR/$BIN_PQCHECK" -q "$QUEUE_PATH"
CHECK_STATUS=$?
set -e

if [ "$CHECK_STATUS" -eq 3 ]; then
    echo "✅ SUCCESS: lpqcheck correctly detected active writers (exit code 3)."
else
    echo "❌ FAILURE: Expected exit code 3 for active writers, got $CHECK_STATUS."
    stop_ldm_daemon $LDMD_PID
    exit 1
fi

echo "=== Phase III: Forced Counter Reset (-F) ==="
if "$BIN_DIR/$BIN_PQCHECK" -F -q "$QUEUE_PATH"; then
    echo "✅ SUCCESS: lpqcheck -F successfully reset the writer count and returned exit code 0."
else
    echo "❌ FAILURE: lpqcheck -F failed to return 0."
    stop_ldm_daemon $LDMD_PID
    exit 1
fi

echo "=== Shutting down active daemon ==="
stop_ldm_daemon $LDMD_PID

echo "=== Phase IV: Corrupt Queue Detection ==="
# Purposefully mangle the queue's control header by writing zeros over the mapped region
dd if=/dev/zero of="$QUEUE_PATH" bs=1024 count=1 conv=notrunc status=none

set +e
"$BIN_DIR/$BIN_PQCHECK" -q "$QUEUE_PATH"
CORRUPT_STATUS=$?
set -e

if [ "$CORRUPT_STATUS" -eq 4 ]; then
    echo "✅ SUCCESS: lpqcheck correctly identified the corrupted queue (exit code 4)."
else
    echo "❌ FAILURE: Expected exit code 4 for corruption, got $CORRUPT_STATUS."
    exit 1
fi

echo "======================================================="
echo " 🎉 ALL $BIN_PQCHECK TESTS PASSED! 🎉"
echo "======================================================="
exit 0
