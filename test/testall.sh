#!/bin/bash
# I haven't put the 'tests' into the ctest suite yet because
# I'm running them constantly and they are breaking a LOT as
# I move c to c++.  LOL.

# Stop the entire mega-script if any individual script fails
set -e

# Define the array of test scripts
scripts=(
#    "./testldmping.sh"
    "./testrdmpull.sh"
    "./testpqact.sh"
    "./testpqcat.sh"
    "./testinfofile.sh"
    "./testregutil.sh"
    "./testldm.sh"
    "./testrpc.sh"
    "./testipv6.sh"
    "./testpqutil.sh"
    "./testsigusr2.sh"
    "./testpqsend.sh"
    "./testrdmsend.sh"
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

# Loop through the array
for script in "${scripts[@]}"; do
    # Extract just the filename for a cleaner printout
    script_name=$(basename "$script")
    
    echo "🚀 Running: $script_name..."
    
    # Execute the script
    $script
    
    echo "✅ Finished: $script_name successfully."
    echo "--------------------------------------"
done

echo "All tests completed successfully!"
