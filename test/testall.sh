#!/bin/bash
# I haven't put the 'tests' into the ctest suite yet because
# I'm running them constantly and they are breaking a LOT as
# I move c to c++.  LOL.

# NOTE: 'set -e' removed so all tests run to completion.

# Define the array of test scripts
scripts=(
    "./testldmping.sh"
    "./testrdmpull.sh"
    "./testpqact.sh"
    "./testpqcat.sh"
    "./testpqcheck.sh"
    "./testinfofile.sh"
    "./testregutil.sh"
    "./testldm.sh"
    "./testrpc.sh"
    "./testipv6.sh"
    "./testpqutil.sh"
    "./testsigusr2.sh"
    "./testpqsend.sh"
    "./testrdmsend.sh"
    "./testldmadmin.sh"
    "./testpipeline.sh"
)

# ==============================================================================
# Pre-Flight Environment Check
# ==============================================================================
REQUIRED_CMDS=("find" "diff")
MISSING_CMDS=()

for cmd in "${REQUIRED_CMDS[@]}"; do
    if ! command -v "$cmd" &> /dev/null; then
        MISSING_CMDS+=("$cmd")
    fi
done

if [ ${#MISSING_CMDS[@]} -ne 0 ]; then
    echo "======================================================="
    echo " 🚨 FATAL: MISSING SYSTEM UTILITIES 🚨"
    echo "======================================================="
    echo "The test suite requires the following missing commands:"
    for cmd in "${MISSING_CMDS[@]}"; do
        echo "  - $cmd"
    done
    echo ""
    echo "Please install the missing packages (e.g., 'dnf install findutils diffutils')"
    echo "(many containers don't have these by default)"
    echo "======================================================="
    exit 1
fi

echo "Starting test suite..."
echo "--------------------------------------"

# Track results
declare -a RESULTS_SCRIPT
declare -a RESULTS_STATUS
OVERALL_FAILED=0

# Loop through the array
for script in "${scripts[@]}"; do
    script_name=$(basename "$script")
    
    echo "🚀 Running: $script_name..."
    
    # Run script and capture exit code safely
    if "$script"; then
        echo "✅ Finished: $script_name successfully."
        RESULTS_SCRIPT+=("$script_name")
        RESULTS_STATUS+=("PASS")
    else
        exit_code=$?
        echo "❌ Failed: $script_name (exit code: $exit_code)"
        RESULTS_SCRIPT+=("$script_name")
        RESULTS_STATUS+=("FAIL ($exit_code)")
        OVERALL_FAILED=1
    fi
    echo "--------------------------------------"
done

# ==============================================================================
# Final Results Summary Table
# ==============================================================================
echo ""
echo "======================================================="
echo "                  TEST SUMMARY TABLE                   "
echo "======================================================="
printf "%-30s | %-15s\n" "Test Script" "Result"
printf "%-30s-+-%-15s\n" "------------------------------" "---------------"

for i in "${!RESULTS_SCRIPT[@]}"; do
    script_col="${RESULTS_SCRIPT[$i]}"
    status_col="${RESULTS_STATUS[$i]}"
    
    # Add status indicator icons to the table rows
    if [[ "$status_col" == "PASS" ]]; then
        printf "%-30s | \033[32m%-15s\033[0m\n" "$script_col" "✅ $status_col"
    else
        printf "%-30s | \033[31m%-15s\033[0m\n" "$script_col" "❌ $status_col"
    fi
done

echo "======================================================="

# Ensure the harness script returns non-zero if any test failed (for CI/CD)
if [ $OVERALL_FAILED -ne 0 ]; then
    echo "🚨 Test suite completed with failures."
    exit 1
else
    echo "🎉 All tests completed successfully!"
    exit 0
fi
