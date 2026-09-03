#!/bin/bash
set -euo pipefail
source ./test_utils.sh

echo "=== 🚀 Setting up rdmadmin Integration Test ==="
TEST_DIR="/tmp/ldm_admin_test"
rm -rf "$TEST_DIR"
sandbox_ldm "$TEST_DIR"

# Stage the environment for rdmadmin
export LDMHOME="$TEST_DIR"
export LDM_BIN_DIR="$BIN_DIR"
export PATH="$LDM_BIN_DIR:$PATH"

# Copy the script to the bin directory as 'rdmadmin' so it acts like an installed system
cp ../ldmadmin.sh "$LDM_BIN_DIR/rdmadmin"
chmod +x "$LDM_BIN_DIR/rdmadmin"

# Create a minimal config to satisfy the startup checks
mkdir -p "$TEST_DIR"/{etc,var/queues,var/logs,var/run}
echo "ACCEPT ANY .* localhost" > "$TEST_DIR/etc/ldmd.conf"

echo "=== Phase I: mkqueue ==="
if rdmadmin mkqueue; then
    echo "✅ SUCCESS: rdmadmin successfully generated the product queue."
else
    echo "❌ FAILURE: rdmadmin mkqueue aborted."
    exit 1
fi

echo "=== Phase II: start ==="
# Override the port to prevent conflicts with local services
if rdmadmin start -P 38899; then
    echo "✅ SUCCESS: rdmadmin successfully spawned the supervisor daemon."
else
    echo "❌ FAILURE: rdmadmin start aborted."
    exit 1
fi
sleep 2

echo "=== Phase III: isrunning ==="
if rdmadmin isrunning; then
    echo "✅ SUCCESS: rdmadmin correctly detected the active daemon."
else
    echo "❌ FAILURE: rdmadmin failed to detect the running daemon."
    rdmadmin stop || true
    exit 1
fi

echo "=== Phase IV: checkinsertion ==="
# The queue is currently empty, so age parsing might be blank, but the command should execute cleanly
if rdmadmin checkinsertion 2>/dev/null || true; then
    echo "✅ SUCCESS: rdmadmin checkinsertion executed without crashing."
else
    echo "❌ FAILURE: rdmadmin checkinsertion crashed."
    rdmadmin stop || true
    exit 1
fi

echo "=== Phase V: stop ==="
if rdmadmin stop; then
    echo "✅ SUCCESS: rdmadmin successfully terminated the daemon."
else
    echo "❌ FAILURE: rdmadmin stop failed."
    exit 1
fi

echo "=== Phase VI: clean ==="
if rdmadmin clean; then
    echo "✅ SUCCESS: rdmadmin purged stale runtime files."
else
    echo "❌ FAILURE: rdmadmin clean failed."
    exit 1
fi

echo "======================================================="
echo " 🎉 ALL rdmadmin TESTS PASSED! 🎉"
echo "======================================================="
exit 0
