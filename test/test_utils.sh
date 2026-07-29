#!/bin/bash
# test_utils.sh - Common definitions and functions 

# Halt on errors
set -e

# Where the executables are.  If you used autogen.sh then
# there are binaries in BUILD/bin.  Otherwise set to the
# path/location of your binaries.
export BIN_DIR="$(pwd)/../BUILD/bin"

export PATH="$BIN_DIR:$PATH" # Force our newly compiled binaries

sandbox_ldm() {
    local test_dir="$1"
    export LDMHOME="$test_dir"
    mkdir -p "$test_dir"/{etc,var/queues,var/logs,var/data,var/run}
}

# Wakes up the LDM daemon.
# Under the legacy model, this sends a SIGCONT.
# Under the modern shared-memory model, this is a no-op.
# Usage: wake_ldm_daemon <pid>
wake_ldm_daemon() {
    #local target_pid=$1
    #if [ -z "$LDM_LEGACY_SYNC" ]; then
        # Modern sync natively wakes the daemon via shared memory condition variables.
        # We do nothing here.
    #    : 
   # else
        # Legacy sync requires a manual prod.
   #     kill -CONT "$target_pid" 2>/dev/null || true
   # fi
   :
}

# Stops an LDM daemon gracefully and waits for it to exit
# Usage: stop_ldm_daemon <pid>
stop_ldm_daemon() {
    local target_pid=$1
    if [ -n "$target_pid" ]; then
        kill -TERM "$target_pid" 2>/dev/null || true
        wait "$target_pid" 2>/dev/null || true
    fi
}

