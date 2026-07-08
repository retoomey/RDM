#!/bin/bash
# Toomey May 2026
# Integration test for the critical LDM binaries.
# Purpose: Tests the end-to-end XML ingestion pipeline to ensure
# C++ modernization efforts do not break core functionality.
#
# Binaries tested:
# - pqcreate : Allocates and initializes the local product queue (ldm.pq).
# - ldmd     : The main LDM daemon; manages child processes (like pqact).
# - pqact    : Reads from the queue and writes to disk based on regex rules.
# - pqinsert : Manually injects a dummy XML product into the queue.
#
# We use an unprivileged port for this self-contained test so developers 
# do not need root privileges or setcap configurations to run it.

LDM_PORT="6000"

# Exit immediately if a command exits with a non-zero status
set -e

source ./test_utils.sh

echo "=== 🚀 Setting up LDM Integration Test Environment ==="

# Define workspace and tool paths
TEST_DIR="/tmp/ldm_test_env"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

# Create fresh directories
mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/data,var/run}

# Create a dummy XML file to insert
DUMMY_XML="$TEST_DIR/dummy_input.xml"
cat << 'EOF' > "$DUMMY_XML"
<?xml version="1.0" encoding="UTF-8"?>
<weather_data>
    <station>KTLX</station>
    <status>All clear</status>
</weather_data>
EOF

# 4. Create the pqact configuration file on the fly
# CRITICAL: We use printf to guarantee hard tabs (\t) and properly escape the regex (\. and \$)
PQACT_CONF="$TEST_DIR/etc/pqact.conf"
# Use single quotes to protect the string from Bash expansion.
# \\. outputs \. (escapes the dot for regex)
# \\1 outputs \1 (prevents printf from evaluating it as octal 001)
printf 'EXP\t^(test_.*\\.xml)$\tFILE\t-overwrite -close\t%s/var/data/\\1\n' "$TEST_DIR" > "$PQACT_CONF"

# The EXEC action test (Explicitly invoking the shell to handle the '>' redirect)
printf 'EXP\t^(test_.*\\.xml)$\tEXEC\tsh -c "echo Processed > %s/var/data/exec_success.txt"\n' "$TEST_DIR" >> "$PQACT_CONF"

# 5. Create the ldmd configuration file on the fly
# CRITICAL: The EXEC command MUST be wrapped in double quotes!
# FIX: Added '-i 1' so pqact polls every 1 second instead of missing the SIGCONT from pqinsert.
LDMD_CONF="$TEST_DIR/etc/ldmd.conf"
echo "EXEC \"$BIN_DIR/pqact -x -d $TEST_DIR -l $TEST_DIR/var/logs/pqact.log -o 3600 -i 1 -f EXP -q $TEST_DIR/var/queues/ldm.pq $PQACT_CONF\"" > "$LDMD_CONF"

# 6. Create the product queue
echo "=== Creating Product Queue ==="
$BIN_DIR/pqcreate -c -s 10M -q "$TEST_DIR/var/queues/ldm.pq"

# 7. Start the LDM daemon
echo "=== Starting LDM Daemon on port $LDM_PORT ==="
# We use "-l -" to force ldmd to stay in the foreground of this background task,
# allowing us to reliably capture its PID for the kill command later.
$BIN_DIR/ldmd -x -P "$LDM_PORT" -q "$TEST_DIR/var/queues/ldm.pq" \
      -l - \
      "$LDMD_CONF" > "$TEST_DIR/var/logs/ldmd.log" 2>&1 &
LDMD_PID=$!

# Give the daemon and pqact a moment to fully initialize
sleep 3 

# 8. Insert the dummy XML file into the queue
echo "=== Inserting XML via pqinsert ==="
$BIN_DIR/pqinsert -x -l "$TEST_DIR/var/logs/pqinsert.log" \
          -q "$TEST_DIR/var/queues/ldm.pq" \
          -f EXP \
          -p "test_weather.xml" \
          "$DUMMY_XML"

# Give pqact a moment to wake up (polling every 1s) and write to disk
sleep 3

# 9. Verify the output
echo "=== Verifying Output ==="
EXPECTED_OUTPUT="$TEST_DIR/var/data/test_weather.xml"
EXEC_OUTPUT="$TEST_DIR/var/data/exec_success.txt"

PASSED=true
if [ -f "$EXPECTED_OUTPUT" ]; then
    echo "✅ SUCCESS: The XML file was successfully processed and written to disk!"
    diff -u "$DUMMY_XML" "$EXPECTED_OUTPUT" && echo "✅ SUCCESS: File contents match perfectly."
else
    echo "❌ FAILURE: The file was not written to disk."
    echo "Check pqact log: cat $TEST_DIR/var/logs/pqact.log"
    echo "Check ldmd log:  cat $TEST_DIR/var/logs/ldmd.log"
    PASSED=false
fi

# Check the EXEC action (This guards our child_map C++ port)
if [ ! -f "$EXEC_OUTPUT" ]; then
    echo "❌ FAILURE: The EXEC action failed to produce exec_success.txt. (Check your child_map implementation!)"
    PASSED=false
else
    echo "✅ SUCCESS: The EXEC action ran and tracked its child process properly."
fi

# 10. Tear down the daemon safely
echo "=== Shutting down LDM (PID $LDMD_PID) ==="
kill -TERM $LDMD_PID
wait $LDMD_PID 2>/dev/null || true

if [ "$PASSED" = true ]; then
    echo "======================================================="
    echo " 🎉 LDM single server test passed! 🎉"
    echo "======================================================="
    exit 0
else
    echo "======================================================="
    echo " 🚨 LDM single server test failed 🚨"
    echo "======================================================="
    exit 1
fi
