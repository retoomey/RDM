# test/CMakeLists.txt

# 1. Find CUnit
find_path(CUNIT_INCLUDE_DIRS NAMES CUnit/CUnit.h)
find_library(CUNIT_LIBRARIES NAMES cunit)

if(NOT CUNIT_INCLUDE_DIRS OR NOT CUNIT_LIBRARIES)
    message(FATAL_ERROR "CUnit not found! Ensure CUnit-devel is installed.")
endif()

# 2. Create a helper function to automatically name and link tests
function(addLDMTest)
    # Parse the arguments looking for the "SOURCES" keyword
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "addLDMTest requires SOURCES to be specified.")
    endif()

    # Grab the first source file in the list (e.g., "testPQ.cpp")
    list(GET ARG_SOURCES 0 FIRST_SRC)

    # Strip the directory and the .cpp extension to create the target name (e.g., "testPQ")
    get_filename_component(TARGET_NAME ${FIRST_SRC} NAME_WE)

    # Generate a stub file for missing executable-level symbols
    set(STUB_FILE ${CMAKE_CURRENT_BINARY_DIR}/test_stubs.cpp)
    if(NOT EXISTS ${STUB_FILE})
        file(WRITE ${STUB_FILE} 
            "#include <sys/types.h>\n"
            "pid_t reap(pid_t pid, int options) { return 0; }\n"
        )
    endif()

    # Create the executable using the dynamically generated target name
    add_executable(${TARGET_NAME} ${ARG_SOURCES} ${STUB_FILE})

    # Give the test access to config.h, CUnit, and the root source tree
    target_include_directories(${TARGET_NAME} PRIVATE 
        ${CMAKE_BINARY_DIR}      # Gives access to the generated config.h
        ${CMAKE_SOURCE_DIR}      # Gives access to root-level headers
        ${CMAKE_SOURCE_DIR}/core # Access to core interfaces
        ${CMAKE_SOURCE_DIR}/storage/pq # Access to pq interfaces
        ${CUNIT_INCLUDE_DIRS}    # Gives access to <CUnit/CUnit.h>
    )

    # Link against the core LDM engine and CUnit
    target_link_libraries(${TARGET_NAME} PRIVATE rdmcore ${CUNIT_LIBRARIES})

    # Register with CTest
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    
    # Ensure dlopen() can find the dynamically built libldm_sunrpc.so plugin 
    # during `make test` before it gets installed to the system.
    set_tests_properties(${TARGET_NAME} PROPERTIES 
        ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}:$ENV{LD_LIBRARY_PATH}"
    )
endfunction()

# ==============================================================================
# LDM Test Suite Executables
# ==============================================================================

# Product Queue Engine Tests
addLDMTest(SOURCES testPQ.cpp)

# Test file locked by self
addLDMTest(SOURCES testFileLockedBySelf.cpp)

# Test gmtime
addLDMTest(SOURCES testGMtime.cpp)

# Local Registry Module Tests
addLDMTest(SOURCES testRegistry.cpp)

# Upstream Process Database Tests
addLDMTest(SOURCES testULDB.cpp)

# Data Product Manipulation & Framing Tests
addLDMTest(SOURCES testDataProd.cpp)

# Wire-Format and PDU Printer Tests
addLDMTest(SOURCES testLDMPrint.cpp)

# Process Management & Safe Forking Tests
addLDMTest(SOURCES testLDMFork.cpp)

# Timestamp test
addLDMTest(SOURCES testTimestamp.cpp)

# LDM configuration file test
addLDMTest(SOURCES testLdmConf.cpp)

# Pqact configuration file test
# Need to organize better first
#addLDMTest(SOURCES testPqactConf.cpp)

# Prod class (protocol2)
addLDMTest(SOURCES testProdClass.cpp)

# Feed type 
addLDMTest(SOURCES testFeedType.cpp)

# Prod info test
addLDMTest(SOURCES testProdInfo.cpp)

# Origin help test
addLDMTest(SOURCES testOrigin.cpp)

# XDR Length Calculation Tests
addLDMTest(SOURCES testLDMXlen.cpp)

# Wordexp
addLDMTest(SOURCES testWordExp.cpp)

# Inetutil networking and resolution tests
addLDMTest(SOURCES testInetUtil.cpp)

# FileUtil or old fsStats test
addLDMTest(SOURCES testFileUtil.cpp)

# Integration test for the pqcheck counter metrics
addLDMTest(SOURCES testPQCheck.cpp)

# Pattern and Regex matching
addLDMTest(SOURCES testPattern.cpp)

# Test the new .info class handler
addLDMTest(SOURCES testSavedInfoFile.cpp ../apps/ldmd/InfoFile.cpp)

# Test the AutoShifter 
addLDMTest(SOURCES testAutoShifter.cpp)

# Manual tests (for now at least)
# Test pipeline speed (from testpipeline.sh)
#addLDMTest(SOURCES testPipeline.cpp)
add_executable(testPipeline testPipeline.cpp)
target_link_libraries(testPipeline PRIVATE rdmcore)

# Extreme Concurrency Stress Test
addLDMTest(SOURCES testPQConcurrency.cpp)

