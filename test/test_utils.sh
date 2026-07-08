#!/bin/bash
# test_utils.sh - Common definitions and functions 

# Halt on errors
set -e

# Where the executables are.  If you used autogen.sh then
# there are binaries in BUILD/bin.  Otherwise set to the
# path/location of your binaries.
export BIN_DIR="$(pwd)/../BUILD/bin"

sandbox_ldm() {
    local test_dir="$1"
    export LDMHOME="$test_dir"
    mkdir -p "$test_dir"/{etc,var/queues,var/logs,var/data,var/run}
}
