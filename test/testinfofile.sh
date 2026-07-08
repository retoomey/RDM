set -e

source ./test_utils.sh

echo "=== 🚀 Setting up LDM State Resume Integration Test ==="

TEST_DIR="/tmp/ldm_state_resume_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

UP_DIR="$TEST_DIR/upstream"
DOWN_DIR="$TEST_DIR/downstream"

mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/data,var/run}
mkdir -p "$DOWN_DIR"/{etc,var/queues,var/logs,var/data,var/run}

DUMMY_1="$TEST_DIR/dummy_1.xml"
DUMMY_2="$TEST_DIR/dummy_2.xml"
echo "<data>Product 1 Before Outage</data>" > "$DUMMY_1"
echo "<data>Product 2 During Outage</data>" > "$DUMMY_2"

UP_CONF="$UP_DIR/etc/ldmd.conf"
echo "ALLOW ANY ^127\.0\.0\.1$|^localhost$ ^(test_.*\.xml)$" > "$UP_CONF"

DOWN_PQACT_CONF="$DOWN_DIR/etc/pqact.conf"
printf 'EXP\t^(test_.*\\.xml)$\tFILE\t-overwrite -close\t%s/var/data/\\1\n' "$DOWN_DIR" > "$DOWN_PQACT_CONF"

DOWN_CONF="$DOWN_DIR/etc/ldmd.conf"
echo "REQUEST EXP \"^(test_.*\.xml)$\" 127.0.0.1:6000" > "$DOWN_CONF"
echo "EXEC \"$BIN_DIR/pqact -x -d $DOWN_DIR -l $DOWN_DIR/var/logs/pqact.log -o 3600 -i 1 -f EXP -q $DOWN_DIR/var/queues/down.pq $DOWN_PQACT_CONF\"" >> "$DOWN_CONF"

echo "=== Creating Product Queues ==="
$BIN_DIR/pqcreate -c -s 10M -q "$UP_DIR/var/queues/up.pq"
$BIN_DIR/pqcreate -c -s 10M -q "$DOWN_DIR/var/queues/down.pq"

echo "=== [Phase 1] Starting Upstream & Downstream (Ports 6000/6001) ==="
$BIN_DIR/ldmd -x -P 6000 -q "$UP_DIR/var/queues/up.pq" -l - "$UP_CONF" > "$UP_DIR/var/logs/ldmd.log" 2>&1 &
UP_PID=$!

# FIX: Run from the base downstream directory so relative "var/run" resolves correctly
cd "$DOWN_DIR" || exit 1
$BIN_DIR/ldmd -x -P 6001 -q "$DOWN_DIR/var/queues/down.pq" -l - "$DOWN_CONF" > "$DOWN_DIR/var/logs/ldmd_run1.log" 2>&1 &
DOWN_PID=$!
cd - > /dev/null

sleep 4

echo "=== [Phase 2] Inserting Product 1 ==="
$BIN_DIR/pqinsert -q "$UP_DIR/var/queues/up.pq" -f EXP -p "test_state_1.xml" "$DUMMY_1"
kill -CONT -$UP_PID 2>/dev/null || true
sleep 4

if [ ! -f "$DOWN_DIR/var/data/test_state_1.xml" ]; then
    echo "❌ FAILURE: Downstream never received Product 1!"
    kill -TERM -$DOWN_PID -$UP_PID 2>/dev/null || true
    exit 1
fi
echo "✅ Verified: Product 1 received downstream."

echo "=== [Phase 3] Simulating Downstream Outage ==="
kill -TERM -$DOWN_PID 2>/dev/null || true
wait $DOWN_PID 2>/dev/null || true

# Look for any file ending in .info within your test target directory
#STATE_FILE=$(ls "$DOWN_DIR/var/run"/.*.info 2>/dev/null | head -n 1)
STATE_FILE=$(ls "$DOWN_DIR/var/run"/*_*.info 2>/dev/null | head -n 1)

if [ -z "$STATE_FILE" ]; then
    echo "❌ FAILURE: No state file was generated upon shutdown!"
    kill -TERM -$UP_PID 2>/dev/null || true
    exit 1
fi
echo "✅ Verified: State file generated during outage ($STATE_FILE)."

echo "=== [Phase 4] Inserting Product 2 (While Downstream is DEAD) ==="
$BIN_DIR/pqinsert -q "$UP_DIR/var/queues/up.pq" -f EXP -p "test_state_2.xml" "$DUMMY_2"
kill -CONT -$UP_PID 2>/dev/null || true
sleep 2

echo "=== [Phase 5] Restarting Downstream to Resume Feed ==="
# FIX: Run from the base downstream directory
cd "$DOWN_DIR" || exit 1
$BIN_DIR/ldmd -x -P 6001 -q "$DOWN_DIR/var/queues/down.pq" -l - "$DOWN_CONF" > "$DOWN_DIR/var/logs/ldmd_run2.log" 2>&1 &
DOWN_PID_2=$!
cd - > /dev/null
sleep 6

echo "=== [Phase 6] Validating Resume Behavior ==="
PASSED=true

if [ -f "$DOWN_DIR/var/data/test_state_2.xml" ]; then
    echo "✅ SUCCESS: Downstream successfully recovered Product 2 after restarting!"
else
    echo "❌ FAILURE: Downstream failed to recover Product 2."
    PASSED=false
fi

if grep -q "SIG=" "$DOWN_DIR/var/logs/ldmd_run2.log"; then
    echo "✅ SUCCESS: Found 'SIG=' injected into the connection request. State file was successfully parsed and used!"
else
    echo "❌ FAILURE: Downstream did not use the state file to resume. It likely fell back to max_latency."
    echo "--- Dump of run 2 log ---"
    cat "$DOWN_DIR/var/logs/ldmd_run2.log"
    PASSED=false
fi

echo "=== Shutting down LDMs ==="
kill -TERM -$DOWN_PID_2 -$UP_PID 2>/dev/null || true
wait $DOWN_PID_2 2>/dev/null || true
wait $UP_PID 2>/dev/null || true

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 LDM Offline Resume & State Tracking Test Passed! 🎉"
    echo "======================================================="
    exit 0
else
    echo "======================================================="
    echo " 🚨 LDM Offline Resume Test Failed 🚨"
    echo "======================================================="
    exit 1
fi
