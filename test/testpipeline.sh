#!/usr/bin/env bash
set -euo pipefail

source ./test_utils.sh

# ==============================================================================
# Pre-Flight Binary Check
# ==============================================================================
if [[ ! -x "$BIN_DIR/testPipeline" ]]; then
    echo "======================================================="
    echo " ⚠️ WARNING: testPipeline binary not found!"
    echo "======================================================="
    echo "This is expected if CUnit-devel was missing during the CMake build,"
    echo "which causes the test directory compilation to be skipped."
    echo "Skipping the pipeline test gracefully."
    exit 0
fi
# ==

ITERATIONS=${1:-5000}
Q_SIZE="500M"
TEST_DIR="/tmp/ldm_pipeline_speed"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

UP_DIR="$TEST_DIR/upstream"
DOWN_DIR="$TEST_DIR/downstream"

echo "=== 🚀 Initializing Advanced Network active Pipeline Test Harness ==="
mkdir -p "$UP_DIR"/{etc,var/queues,var/logs,var/run}
mkdir -p "$DOWN_DIR"/{etc,var/queues,var/logs,var/run}

echo "ALLOW ANY ^127\.0\.0\.1$|^localhost$ .*" > "$UP_DIR/etc/ldmd.conf"
echo "REQUEST EXP \".*\" 127.0.0.1:6000" > "$DOWN_DIR/etc/ldmd.conf"

echo "=== Creating Expanded Product Queues ($Q_SIZE) ==="
"$BIN_DIR/pqcreate" -c -s "$Q_SIZE" -S 10000 -q "$UP_DIR/var/queues/up.pq"
"$BIN_DIR/pqcreate" -c -s "$Q_SIZE" -S 10000 -q "$DOWN_DIR/var/queues/down.pq"

echo "=== Starting Upstream LDM Daemon (Port 6000) ==="
"$BIN_DIR/ldmd" -x -P 6000 -q "$UP_DIR/var/queues/up.pq" -l - "$UP_DIR/etc/ldmd.conf" > "$UP_DIR/var/logs/ldmd_up_phase1.log" 2>&1 &
UP_PID=$!

# ---------------------------------------------------------
# PHASE I: TEST "HEREIS" PATHWAY
# ---------------------------------------------------------
echo "=== Starting Downstream LDM Daemon (Phase I: HEREIS) ==="
cd "$DOWN_DIR/var/run" || exit 1
# We set -H 16384. Because our test payload is 1KB, it will easily fit 
# under the 16KB threshold, guaranteeing the HEREIS pathway is exercised.
"$BIN_DIR/ldmd" -x -P 6001 -N -H 16384 -q "$DOWN_DIR/var/queues/down.pq" -l - "$DOWN_DIR/etc/ldmd.conf" > "$DOWN_DIR/var/logs/ldmd_down_phase1.log" 2>&1 &
DOWN_PID=$!
cd - > /dev/null

sleep 1

echo -e "\n🔥 Running Phase I: Small Text Observation Workload (1 KB payloads)..."
# Removed $UP_PID
"$BIN_DIR/testPipeline" "$UP_DIR/var/queues/up.pq" "$DOWN_DIR/var/queues/down.pq" "$ITERATIONS" 1

# ---------------------------------------------------------
# INTER-PHASE PURGE (OPTION A)
# ---------------------------------------------------------
echo -e "\n🛑 Tearing down Phase I and purging queues..."
kill -TERM "$DOWN_PID" "$UP_PID" 2>/dev/null || true
wait "$DOWN_PID" 2>/dev/null || true
wait "$UP_PID" 2>/dev/null || true

rm -f "$UP_DIR/var/queues/up.pq" "$DOWN_DIR/var/queues/down.pq"

echo "=== Recreating Expanded Product Queues ($Q_SIZE) ==="
"$BIN_DIR/pqcreate" -c -s "$Q_SIZE" -S 10000 -q "$UP_DIR/var/queues/up.pq"
"$BIN_DIR/pqcreate" -c -s "$Q_SIZE" -S 10000 -q "$DOWN_DIR/var/queues/down.pq"

echo "=== Restarting Upstream LDM Daemon (Port 6000) ==="
"$BIN_DIR/ldmd" -x -P 6000 -q "$UP_DIR/var/queues/up.pq" -l - "$UP_DIR/etc/ldmd.conf" > "$UP_DIR/var/logs/ldmd_up_phase2.log" 2>&1 &
UP_PID=$!

# ---------------------------------------------------------
# PHASE II: TEST "COMINGSOON / BLKDATA" PATHWAY
# ---------------------------------------------------------
echo "=== Starting Downstream LDM Daemon (Phase II: COMINGSOON) ==="
cd "$DOWN_DIR/var/run" || exit 1
# We keep -H 16384. Because our payload is 512KB, it will drastically exceed 
# the 16KB threshold, forcing the COMINGSOON -> BLKDATA transaction pathway.
"$BIN_DIR/ldmd" -x -P 6001 -H 16384 -q "$DOWN_DIR/var/queues/down.pq" -l - "$DOWN_DIR/etc/ldmd.conf" > "$DOWN_DIR/var/logs/ldmd_down_phase2.log" 2>&1 &
DOWN_PID=$!
cd - > /dev/null

sleep 1

echo -e "\n🔥 Running Phase II: Heavy Radar/Satellite Mesh Workload (512 KB payloads)..."
LARGE_ITERATIONS=$((ITERATIONS / 10))
# Removed $UP_PID
"$BIN_DIR/testPipeline" "$UP_DIR/var/queues/up.pq" "$DOWN_DIR/var/queues/down.pq" "$LARGE_ITERATIONS" 512

echo -e "\n🛑 Tearing down LDM pipeline test environments..."
kill -TERM "$DOWN_PID" "$UP_PID" 2>/dev/null || true
wait "$DOWN_PID" 2>/dev/null || true
wait "$UP_PID" 2>/dev/null || true

echo "Logs available in $UP_DIR/var/logs/ and $DOWN_DIR/var/logs/"
echo "🎉 Pipeline execution matrix completed."
