#!/usr/bin/env bash
# ==============================================================================
# ALPHA (I haven't used this yet so it probably needs work/debugging).
#        FIXME: 1.  Make sure it works
#               2.  Install with cmake say as ldmadmin or rdmadmin, etc.
# First pass at an ldmadmin script replacement.
# 
# Perl is getting long in the tooth and is not allowed on some of our deployment
# To be honest for things like this wrapping binaries, basic shell works.
# File:        ldmadmin
# Description: Modern Bash Orchestrator for C++ LDM Binaries
# ==============================================================================
set -euo pipefail

# --- Installation Directory Bounds ---
# Respect the user's home directory if LDMHOME isn't explicitly set
# Since we now can run non-privileged
export LDMHOME="${LDMHOME:-$HOME/ldm}"
export LDM_BIN_DIR="${LDM_BIN_DIR:-$LDMHOME/bin}"

# --- Binary Collection Configuration ---
export LDM_BIN_DIR="${LDM_BIN_DIR:-$LDMHOME/bin}"

# --- Cleaned LDM Binary Execution Constants ---
readonly CMD_REGUTIL="$LDM_BIN_DIR/regutil"
readonly CMD_LDMD="$LDM_BIN_DIR/ldmd"
readonly CMD_PQCREATE="$LDM_BIN_DIR/pqcreate"
readonly CMD_PQMON="$LDM_BIN_DIR/pqmon"
readonly CMD_PQUTIL="$LDM_BIN_DIR/pqutil"
readonly CMD_ULDBUTIL="$LDM_BIN_DIR/uldbutil"
readonly CMD_LDMPING="$LDM_BIN_DIR/ldmping"

# Expose custom bin directory to PATH for any internal utility cross-calls
export PATH="$LDM_BIN_DIR:$PATH"

# Guard check: Ensure core configuration query binary is available
if [ ! -x "$CMD_REGUTIL" ]; then
    echo "Error: Critical C++ binary not found or not executable: $CMD_REGUTIL" >&2
    exit 1
fi

# ==============================================================================
# Dynamic Registry Sync Function
# ==============================================================================
get_reg() {
    local key="$1"
    local value
    if value=$("$CMD_REGUTIL" "regpath{$key}" 2>/dev/null); then
        echo "$value"
    else
        # Fallback values if registry entries are completely missing
        case "$key" in
            QUEUE_PATH)       echo "$LDMHOME/var/queues/ldm.pq" ;;
            QUEUE_SIZE)       echo "500000000" ;;
            LDMD_CONFIG_PATH) echo "$LDMHOME/etc/ldmd.conf" ;;
            PID_FILE)         echo "$LDMHOME/var/run/ldmd.pid" ;;
            *)                echo "" ;;
        esac
    fi
}

# --- Hydrate State Variables via C++ regutil ---
export HOSTNAME=$(hostname -f)
export LDMD_CONFIG_PATH=$(get_reg "LDMD_CONFIG_PATH")
export QUEUE_PATH=$(get_reg "QUEUE_PATH")
export QUEUE_SIZE=$(get_reg "QUEUE_SIZE")
export QUEUE_SLOTS=$(get_reg "QUEUE_SLOTS")
export PID_FILE=$(get_reg "PID_FILE")
export LOCK_FILE="$LDMHOME/.ldmadmin.lck"

export IP_ADDR=$(get_reg "IP_ADDR")
export PORT=$(get_reg "PORT")
export MAX_CLIENTS=$(get_reg "MAX_CLIENTS")
export MAX_LATENCY=$(get_reg "MAX_LATENCY")
export TIME_OFFSET=$(get_reg "TIME_OFFSET")
export INSERTION_CHECK_INTERVAL=$(get_reg "INSERTION_CHECK_INTERVAL")

# Fallback defaults
: "${IP_ADDR:=0.0.0.0}"
: "${PORT:=388}"
: "${MAX_CLIENTS:=256}"
: "${MAX_LATENCY:=3600}"
: "${TIME_OFFSET:=3600}"
: "${INSERTION_CHECK_INTERVAL:=60}"

mkdir -p "$(dirname "$PID_FILE")"
cd "$LDMHOME"

# ==============================================================================
# Helper Methods & Concurrency Control
# ==============================================================================

errmsg() {
    echo -e "$*" >&2
}

get_lock() {
    exec 9>>"$LOCK_FILE"
    if ! flock -n 9; then
        errmsg "getLock(): Couldn't lock file '$LOCK_FILE'. Another ldmadmin running."
        exit 1
    fi
}

release_lock() {
    flock -u 9 2>/dev/null || true
    exec 9>&- || true
}

get_pid() {
    if [ -f "$PID_FILE" ]; then
        local p
        p=$(head -n 1 "$PID_FILE" | tr -d '[:space:]')
        if [[ "$p" =~ ^[0-9]+$ ]]; then
            echo "$p"
            return 0
        fi
    fi
    echo "-1"
}

is_running() {
    local pid
    pid=$(get_pid)
    if [ "$pid" != "-1" ]; then
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
    fi
    if [ -x "$CMD_LDMPING" ]; then
        if "$CMD_LDMPING" -l- -i 0 -t 1 "$IP_ADDR" >/dev/null 2>&1; then
            return 0
        fi
    fi
    return 1
}

# ==============================================================================
# Subcommand Execution Blocks
# ==============================================================================

start_ldm() {
    if is_running; then
        errmsg "start_ldm(): There is another LDM server running. Aborted."
        return 1
    fi

    if [ ! -f "$QUEUE_PATH" ]; then
        errmsg "start_ldm(): Product queue missing. Run 'ldmadmin mkqueue' first."
        return 1
    fi

    if [ -x "$CMD_ULDBUTIL" ]; then
        "$CMD_ULDBUTIL" -d 2>/dev/null || true
    fi

    echo "Checking LDM configuration-file ($LDMD_CONFIG_PATH)..."
    if ! "$CMD_LDMD" -I "$IP_ADDR" -P "$PORT" -M "$MAX_CLIENTS" -m "$MAX_LATENCY" \
                     -o "$TIME_OFFSET" -q "$QUEUE_PATH" -nvl- "$LDMD_CONFIG_PATH" >/dev/null 2>&1; then
        errmsg "start(): Problem identified within LDM configuration file rules."
        return 1
    fi

    # NOTE: Logging rotation is skipped entirely here. 
    # Your C++ binary utilizing spdlog will handle its own rolling log sinks.

    if [ -x "$CMD_PQUTIL" ]; then
        "$CMD_PQUTIL" -C 2>/dev/null || true
    fi

    echo "Starting the LDM server..."
    "$CMD_LDMD" -I "$IP_ADDR" -P "$PORT" -M "$MAX_CLIENTS" -m "$MAX_LATENCY" \
                -o "$TIME_OFFSET" -q "$QUEUE_PATH" "$LDMD_CONFIG_PATH" > "$PID_FILE" 2>&1 &
         
    sleep 2
    if ! is_running; then
        errmsg "start(): Could not start LDM server daemon process."
        rm -f "$PID_FILE"
        return 1
    fi
    return 0
}

stop_ldm() {
    local pid
    pid=$(get_pid)
    if [ "$pid" == "-1" ]; then
        errmsg "The LDM server isn't running or its process-ID is unavailable."
        return 1
    fi

    echo "Stopping the LDM server..."
    kill -15 "$pid"

    for i in {1..10}; do
        if ! is_running; then break; fi
        sleep 1
    done

    find . -name ".*.info" -prune ! -newer "$PID_FILE" -exec rm -f {} + 2>/dev/null || true
    rm -f "$PID_FILE"
    return 0
}

make_pq() {
    if is_running; then
        errmsg "make_pq(): There is a server running. mkqueue aborted."
        return 1
    fi

    if [ ! -x "$CMD_PQCREATE" ]; then
        errmsg "make_pq(): Execution binary missing: $CMD_PQCREATE"
        return 1
    fi

    local cmd="$CMD_PQCREATE"
    [ "${OPT_CLOBBER:-0}" -eq 1 ] && cmd="$cmd -c"
    [ "$QUEUE_SLOTS" != "" ] && [ "$QUEUE_SLOTS" != "default" ] && cmd="$cmd -S $QUEUE_SLOTS"
    
    cmd="$cmd -q $QUEUE_PATH -s $QUEUE_SIZE"
    echo "Executing: $cmd"
    eval "$cmd"
}

check_insertion() {
    if [ ! -x "$CMD_PQMON" ]; then
        errmsg "check_insertion(): Execution binary missing: $CMD_PQMON"
        return 1
    fi

    local line
    if ! line=$("$CMD_PQMON" -S -q "$QUEUE_PATH" 2>/dev/null); then
        errmsg "check_insertion(): pqmon execution error."
        return 1
    fi
    local age
    age=$(echo "$line" | awk '{print $9}')
    if [ "${age:.0f}" -gt "$INSERTION_CHECK_INTERVAL" ]; then
        errmsg "check_insertion(): Latency profile limit alert threshold breached ($age seconds ago)."
        return 1
    fi
    return 0
}

# ==============================================================================
# Router Switch Node
# ==============================================================================
CMD="${1:-usage}"
shift || true

case "$CMD" in
    start)
        while getopts "q:M:m:o:" opt; do
            case "$opt" in
                q) QUEUE_PATH="$OPTARG" ;;
                M) MAX_CLIENTS="$OPTARG" ;;
                m) MAX_LATENCY="$OPTARG" ;;
                o) TIME_OFFSET="$OPTARG" ;;
                *) ;;
            esac
        done
        get_lock; start_ldm; release_lock
        ;;
    stop)
        get_lock; stop_ldm; release_lock
        ;;
    restart)
        get_lock; stop_ldm || true; start_ldm; release_lock
        ;;
    mkqueue)
        OPT_CLOBBER=0
        while getopts "q:c" opt; do
            case "$opt" in
                q) QUEUE_PATH="$OPTARG" ;;
                c) OPT_CLOBBER=1 ;;
                *) ;;
            esac
        done
        get_lock; make_pq; release_lock
        ;;
    delqueue)
        get_lock
        if ! is_running; then rm -f "$QUEUE_PATH"; else errmsg "LDM active, cannot delete."; fi
        release_lock
        ;;
    isrunning)
        if is_running; then exit 0; else exit 1; fi
        ;;
    checkinsertion)
        check_insertion
        ;;
    config)
        "$CMD_REGUTIL" "regpath{}"
        ;;
    clean)
        if is_running; then
            errmsg "The LDM system is running! Stop it first."
            return 1
        fi
        rm -f "$PID_FILE" "$LDMHOME/MldmRpc_*"
        echo "Stale runtime lock files cleanly purged."
        ;;
    usage|*)
        echo "Usage: $0 {start|stop|restart|mkqueue|delqueue|isrunning|checkinsertion|config|clean}"
        ;;
esac
