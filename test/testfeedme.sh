set -e

# Common stuff
source ./test_utils.sh

echo "=== 🚀 Setting up feedme Integration Test Environment ==="
TEST_DIR="/tmp/ldm_feedme_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

UP_DIR="$TEST_DIR/upstream"
LDM_PORT="6003"

mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/run,var/db}

UP_CONF="$UP_DIR/etc/ldmd.conf"
echo "ALLOW ANY ^127\.0\.0\.1$|^localhost$ .*" > "$UP_CONF"

echo "=== Creating Upstream Product Queue ==="
$BIN_DIR/pqcreate -c -s 2M -q "$UP_DIR/var/queues/up.pq"

echo "=== Starting Upstream LDM Daemon (Port $LDM_PORT) ==="
cd "$UP_DIR" || exit 1
$BIN_DIR/ldmd -x -P $LDM_PORT -q "var/queues/up.pq" -l - "etc/ldmd.conf" > "var/logs/ldmd.log" 2>&1 &
UP_PID=$!
cd - > /dev/null

sleep 6

echo "=== Starting feedme Client ==="
FEEDME_LOG="$TEST_DIR/feedme.log"
$BIN_DIR/feedme -v -h localhost -P $LDM_PORT -p "TEST_PRODUCT" -l "$FEEDME_LOG" > "$TEST_DIR/feedme_crash.log" 2>&1 &
FEEDME_PID=$!

sleep 2

echo "=== Inserting Test Product ==="
echo "Hello, LDM World!" > "$TEST_DIR/dummy_payload.txt"
$BIN_DIR/pqinsert -v -q "$UP_DIR/var/queues/up.pq" -p "TEST_PRODUCT_123" "$TEST_DIR/dummy_payload.txt"

# Wake up the sleeping upstream ldmd and its children
kill -CONT -$UP_PID 2>/dev/null || true

sleep 2

echo "=== Shutting down processes ==="
kill -TERM $FEEDME_PID 2>/dev/null || true
kill -TERM $UP_PID 2>/dev/null || true
wait $UP_PID 2>/dev/null || true

echo "=== Verifying feedme Reception ==="
if [ -f "$FEEDME_LOG" ] && grep -q "TEST_PRODUCT_123" "$FEEDME_LOG"; then
    echo "✅ SUCCESS: feedme successfully connected and received the data product!"
else
    echo "❌ FAILURE: feedme did not receive the product."
    echo "--- feedme crash output ---"
    cat "$TEST_DIR/feedme_crash.log" 2>/dev/null || echo "(No crash log found)"
    
    echo "--- ldmd log ---"
    cat "$UP_DIR/var/logs/ldmd.log" 2>/dev/null || echo "(No ldmd log found)"
    exit 1
fi

echo "======================================================="
echo " 🎉 ALL FEEDME TESTS PASSED! 🎉"
echo "======================================================="

rm -rf "$TEST_DIR"
exit 0
