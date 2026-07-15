#!/bin/bash
# I haven't put the 'tests' into the ctest suite yet because
# I'm running them constantly and they are breaking a LOT as
# I move c to c++.  LOL.

# Stop the entire mega-script if any individual script fails
set -e

# Define the array of test scripts
scripts=(
    "./testfeedme.sh"
    "./testldmping.sh"
    "./testnotifyme.sh"
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
    "./testpipeline.sh"
)

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
