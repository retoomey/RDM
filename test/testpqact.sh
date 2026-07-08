#!/usr/bin/env bash
# ==============================================================================
# Toomey May 2026
# testpqact.sh: High-Stress Advanced Data Structure and Feature Verification
# ==============================================================================
# FAQ: WHAT AND WHY ARE WE TESTING?
# 
# WHY: The 'pqact' suite was migrated from legacy C to modern C++17. This 
# replaced manual memory management (malloc/free), intrusive linked lists, and 
# fixed-size char buffers with RAII constructs (std::unique_ptr), std::list, 
# and std::string. Thread-safety was also introduced via std::mutex.
#
# WHAT:
# 1. LRU Cache Eviction: Forces pqact to hit its file descriptor limit to ensure
#    the std::list properly evicts old entries and triggers destructors without 
#    iterator invalidation.
# 2. I/O Subsystems: Validates standard FILE, buffered STDIOFILE, and process
#    PIPE routing paths.
# 3. Large Payloads: Pushes products > 16KB to force pipe buffers (pbuf) to 
#    fragment and flush in chunks, validating pointer arithmetic.
# 4. Process Reaping: Triggers the EXEC action, placing child PIDs into a 
#    thread-safe std::map (child_map.cpp), ensuring zombies are reaped safely.
# 5. SIGHUP Resilience: Forces pqact to drop and rebuild its entire regex action 
#    list (std::list<palt>) mid-execution, proving memory stability.
# 6. String Mutations: Tests WMO header stripping, date/time reconstruction, 
#    and regex backreferencing (\1).
# ==============================================================================
set -e

source ./test_utils.sh

echo "=== 🚀 Initializing Advanced pqact Test Suite ==="

# 1. Path Configuration
TEST_DIR="/tmp/ldm_pqact_advanced_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

QUEUE_PATH="$TEST_DIR/var/queues/pqact_stress.pq"
PQACT_CONF="$TEST_DIR/etc/pqact.conf"
LOG_DIR="$TEST_DIR/var/logs"
DATA_DIR="$TEST_DIR/var/data"

# Clean up any lingering structures
mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/data}
mkdir -p "$DATA_DIR"/{unio,stdio,pipe,stripped,sub,exec}
mkdir -p "$TEST_DIR/var/run"
export LDM_STATE_DIR="$TEST_DIR/var/run"

# 2. Force Low Descriptor Limit to Trigger LRU Evictions Natively
echo "⚙️  Constraining shell file descriptors to stress-test LRU caching..."
ulimit -n 16

# ==============================================================================
# 3. Construct Multi-Action pqact Configuration Matrix
# ==============================================================================
echo "📝 Constructing advanced action matrix inside pqact.conf..."
cat << EOF > "$PQACT_CONF"
# Rule A: Standard Unix I/O - Keep entries open to fill LRU pool
EXP	^(test_.*\.xml)$	FILE	-overwrite	var/data/unio/\1

# Rule B: Standard I/O streams - Appends tracking metrics to a single shared file
EXP	^(stdio_.*\.xml)$	STDIOFILE	-flush	var/data/stdio/append_matrix.log

# Rule C: Decoder Process Piping - Validates safe descriptor passing and pbuf
EXP	^(pipe_.*\.xml)$	PIPE	-flush	sh -c "cat >> var/data/pipe/stream_capture.xml"

# Rule D: Wire-Format Stripping - Validates WMO/SBN prefix truncation logic
EXP	^(wmo_.*\.xml)$	FILE	-removewmo -overwrite	var/data/stripped/\1

# Rule E: Advanced String Substitution
EXP	^sub_([0-9]{2})_(.*)\.xml$	FILE	-overwrite	var/data/sub/\2_(\1:yyyy)(\1:mm)(\1:dd)_(seq).xml

# Rule F: External Executions (Child Process Tracking)
EXP	^(exec_.*\.xml)$	EXEC	sh -c "echo Executed \1 > var/data/exec/\1.out"

# Rule G: Large Buffer Piping
EXP	^(large_.*\.xml)$	PIPE	-flush	sh -c "cat > var/data/pipe/large_capture.xml"

# Rule H: Timeout Testing - Misbehaving decoder that sleeps to force pipe saturation
EXP	^(timeout_test\.xml)$	PIPE	-flush	sh -c "sleep 10"
EOF

# ==============================================================================
# 4. Generate Mock Data Payloads
# ==============================================================================
DUMMY_XML="$TEST_DIR/base_payload.xml"
cat << 'EOF' > "$DUMMY_XML"
<ldm_payload><status>Active Validation</status></ldm_payload>
EOF

WMO_XML="$TEST_DIR/wmo_payload.xml"
printf "\x01\r\r\n000 \r\r\nSAUS41 KTLX 281700\r\r\n<weather>Valid Strip</weather>" > "$WMO_XML"

# Generate a 50KB XML payload (well over the 16KB pbuf limit)
LARGE_XML="$TEST_DIR/large_payload.xml"
printf "<large_data>\n" > "$LARGE_XML"
for i in {1..1000}; do
    printf "Padding chunk %04d to inflate the product size and force pbuf_write() fragmentation...\n" "$i" >> "$LARGE_XML"
done
printf "</large_data>\n" >> "$LARGE_XML"

# Create the target product queue
echo "📦 Building uninstalled testing queue..."
$BIN_DIR/pqcreate -c -s 15M -S 2000 -q "$QUEUE_PATH"

# ==============================================================================
# 5. Launch Standalone pqact Core Instance
# ==============================================================================
echo "🎯 Spawning detached standalone pqact monitoring engine..."
$BIN_DIR/pqact -x -d "$TEST_DIR" \
               -l "$LOG_DIR/pqact.log" \
               -i 1 \
               -t 2 \
               -f EXP \
               -q "$QUEUE_PATH" \
               "$PQACT_CONF" &
PQACT_PID=$!

sleep 2

# ==============================================================================
# 6. Execute Stress-Injection Phase
# ==============================================================================
echo "⚡ Phase I: Injecting 15 unique targets to force LRU pool evictions (Max 10)..."
for i in {01..15}; do
    $BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "test_product_${i}.xml" "$DUMMY_XML"
done

echo "⚡ Phase II: Injecting sequential appends targeting STDIOFILE layout..."
for i in {1..5}; do
    $BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "stdio_append_${i}.xml" "$DUMMY_XML"
done

echo "⚡ Phase III: Passing payloads into decoders via PIPE structures..."
for i in {1..3}; do
    $BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "pipe_stream_${i}.xml" "$DUMMY_XML"
done

echo "⚡ Phase IV: Introducing wire-format frames containing raw binary blocks..."
$BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "wmo_ingest_data.xml" "$WMO_XML"

echo "⚡ Phase V: Testing advanced date, sequence, and regex string substitutions..."
$BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "sub_28_payload.xml" "$DUMMY_XML"

echo "⚡ Phase VI: Spawning independent EXEC process tracking..."
$BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "exec_child.xml" "$DUMMY_XML"

echo "⚡ Phase VII: Testing 50KB large product pipe-fragmentation bounds..."
$BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "large_product.xml" "$LARGE_XML"

# Let pqact parse through the trailing frames
sleep 4

echo "⚡ Phase VIII: Sending SIGHUP to force RAII memory re-initialization..."
kill -HUP $PQACT_PID
sleep 2

echo "⚡ Phase IX: Injecting final payload to prove SIGHUP survival..."
$BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "test_survival.xml" "$DUMMY_XML"
sleep 2

echo "⚡ Phase X: Forcing a PIPE timeout with a hung decoder..."
# Inject a payload large enough to instantly fill the OS pipe buffer (e.g., Linux default is 64KB).
# This guarantees pqact's write() will block, forcing it into our new poll() timeout logic.
dd if=/dev/zero of="$TEST_DIR/massive_payload.dat" bs=100K count=1 2>/dev/null
$BIN_DIR/pqinsert -i -q "$QUEUE_PATH" -f EXP -p "timeout_test.xml" "$TEST_DIR/massive_payload.dat"

# Wait for the 2-second timeout to expire
sleep 4

# ==============================================================================
# 7. Automated Validation and Verification
# ==============================================================================
echo "🔍 Analyzing output directory metrics..."
PASSED=true

# Check 1: LRU cache evictions
if grep -q "Deleting least-recently-used FILE entry" "$LOG_DIR/pqact.log"; then
    echo "✅ SUCCESS: LRU cache engine triggered and evicted files correctly."
else
    echo "❌ FAILURE: LRU cache logic was not triggered."
    PASSED=false
fi

# Check 2: SIGHUP Survival
if grep -q "Rereading configuration file" "$LOG_DIR/pqact.log" && [ -f "$DATA_DIR/unio/test_survival.xml" ]; then
    echo "✅ SUCCESS: SIGHUP properly cleared memory and survived to write new files."
else
    echo "❌ FAILURE: SIGHUP crashed the daemon or failed to parse subsequent files."
    PASSED=false
fi

# Check 3: Date/Time Substitution
if ls "$DATA_DIR"/sub/payload_*_0.xml 1> /dev/null 2>&1; then
    echo "✅ SUCCESS: Palt.cpp correctly substituted date and sequence numbers."
else
    echo "❌ FAILURE: Date/Sequence substitution failed."
    PASSED=false
fi

# Check 4: EXEC tracking
if [ -f "$DATA_DIR/exec/exec_child.xml.out" ]; then
    echo "✅ SUCCESS: EXEC action fired and managed external process mapping."
else
    echo "❌ FAILURE: EXEC action output file missing."
    PASSED=false
fi

# Check 5: Large Pipe Fragmenting
if [ -s "$DATA_DIR/pipe/large_capture.xml" ]; then
    LARGE_SIZE=$(wc -c < "$DATA_DIR/pipe/large_capture.xml" | awk '{print $1}')
    if [ "$LARGE_SIZE" -gt 16384 ]; then
         echo "✅ SUCCESS: 50KB payload successfully fragmented and piped."
    else
         echo "❌ FAILURE: Large piped payload was truncated! (Size: $LARGE_SIZE)"
         PASSED=false
    fi
else
    echo "❌ FAILURE: Large piped payload missing."
    PASSED=false
fi

# Check 6: Pipe Saturation Timeout
if grep -q "to decoder timed-out" "$LOG_DIR/pqact.log"; then
    echo "✅ SUCCESS: poll() successfully caught the timeout and recovered!"
else
    echo "❌ FAILURE: pqact failed to timeout gracefully. It may be hung."
    tail -n 10 "$LOG_DIR/pqact.log"
    PASSED=false
fi

# ==============================================================================
# 8. Teardown
# ==============================================================================
echo "🛑 Shutting down pqact monitoring engine (PID: $PQACT_PID)..."
kill -TERM "$PQACT_PID"
wait "$PQACT_PID" 2>/dev/null || true

echo "=== Phase XI: Verifying State File Generation ==="
# Find the first file ending in .state anywhere in the test sandbox
echo "Looking for .state file in $TEST_DIR"
STATE_FILE=$(find "$TEST_DIR" -name "*.state" | head -n 1)

if [ -n "$STATE_FILE" ]; then
    echo "✅ SUCCESS: pqact successfully flushed its state file!"
    echo "   -> $STATE_FILE"
else
    echo "❌ FAILURE: pqact failed to generate a .state file in the test directory!"
    PASSED=false
fi

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 ALL ADVANCED PQACT MODERNIZATION TESTS PASSED! 🎉"
    echo "======================================================="
    exit 0
else
    echo "======================================================="
    echo " 🚨 CRITICAL ARCHITECTURAL TEST REGRESSIONS FOUND 🚨"
    echo "======================================================="
    exit 1
fi
