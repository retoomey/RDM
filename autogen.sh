# Toomey May 2026
# This script will run a standard cmake build
# into the BUILD folder of this directory.  The
# test folder looks for binaries in that folder

mkdir -p BUILD
cd BUILD || exit 1

# Configure with CMake.
cmake ../. -DCMAKE_INSTALL_PREFIX=../..

# Determine number of CPU cores
CORES=$(nproc)

# Build using all available cores
# Note old ldm had a conflict with multicore building
make -j"$CORES" install
