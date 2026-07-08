# Toomey May 2026
# Porting LDM core simple binaries to CMake
# We don't need things like grib2 ingest, etc.
#

cmake_minimum_required(VERSION 3.10)
project(RDM C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Forces Position Independent Code across the board for dynamic linking
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# C++17 removed the 'register' keyword. Legacy C headers (like Sun RPC) use it.
# This macro safely strips the keyword out during C++ compilation.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dregister=")

include(GNUInstallDirs)

# Add an option to easily turn testing on or off (defaults to OFF for now)
option(RDM_BUILD_TESTS "Build the LDM test suite" ON)

# Route all compiled outputs to a unified directory structure
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Ensure C99 standard compliance
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # Allows standard GNU extensions if needed

# Find required external packages
find_package(Threads REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED) 
find_package(PkgConfig REQUIRED)
pkg_check_modules(TIRPC REQUIRED libtirpc)

# Use a modern logging library to purge the log c custom stuff.
include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.12.0 # Use a stable release
)
FetchContent_MakeAvailable(spdlog)

# ==============================================================================
# Global Configuration Variables (from configure.ac)
# ==============================================================================

set(CONFIG_H 1)

# Tells glibc to expose modern POSIX.1-2008 standard APIs
add_compile_definitions(_XOPEN_SOURCE=700)
add_compile_definitions(PACKAGE_VERSION="${CMAKE_PROJECT_VERSION}")

# ==============================================================================
# System Checks (Headers, Functions, Types)
# ==============================================================================

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)

# ==============================================================================
# Global Include Directories
# ==============================================================================

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${LIBXML2_INCLUDE_DIR}
    ${spdlog_SOURCE_DIR}/include 
)

# ==============================================================================
# RDM Common Library 
# ==============================================================================
add_library(rdmcore SHARED
    core/Log.cpp
    core/SignalManager.cpp
    core/Signature.cpp
    core/FileUtil.cpp
    core/PrivilegeManager.cpp
    core/AutoShifter.cpp
    core/ProcessUtil.cpp
    core/Pattern.cpp
    core/Wordexp.cpp
    core/Registry.cpp
    core/ServiceAddr.cpp
    core/NetworkUtils.cpp
    core/Timestamp.cpp
    core/ULDB.cpp
    core/FeedType.cpp
    core/SemaphoreRWLock.cpp
    core/ConfParser.cpp
    core/NetworkFactory.cpp
    core/StorageFactory.cpp
    core/ProcessManager.cpp
    core/AclManager.cpp
    core/UpFilter.cpp
    core/ProdInfo.cpp
    core/ProdClass.cpp
    core/ProdSpec.cpp
)

target_include_directories(rdmcore PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${XML2_INCLUDE_DIRS} 
)

target_include_directories(rdmcore PUBLIC
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(rdmcore PRIVATE 
    ${XML2_LIBRARIES}
    ${CMAKE_DL_LIBS}   # Link libdl to satisfy dlopen() in NetworkFactory
    ${TIRPC_LIBRARIES}     
    OpenSSL::Crypto 
    m                      
    rt                     
    pthread                
    xml2                   
    spdlog::spdlog
)

# ==============================================================================
# Product Queue Library 
# ==============================================================================
add_library(rdmstorepq MODULE
    storage/pq/PqPlugin.cpp
    storage/pq/ProductQueue.cpp
    storage/pq/QueueCursor.cpp
    storage/pq/QueueEntry.cpp
    storage/pq/QueueIndexManager.cpp
    storage/pq/FreeBlock.cpp
    storage/pq/TimeQueue.cpp
    storage/pq/RegionList.cpp
    storage/pq/RegionManager.cpp
    storage/pq/SignatureIndex.cpp
    storage/pq/IndexLayer.cpp
    storage/pq/PqLocks.cpp
    storage/pq/PqFile.cpp
)

target_include_directories(rdmstorepq PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/storage/pq
)

target_link_libraries(rdmstorepq PRIVATE rdmcore)

# ==============================================================================
# Sun RPC Wrapper (Dynamically Loaded Plugin)
# ==============================================================================
add_library(rdmnetsunrpc MODULE
    network/sunrpc/SunRpcPlugin.cpp
    network/sunrpc/SunRpcClient.cpp
    network/sunrpc/SunRpcServer.cpp
    network/sunrpc/SunRpcDispatcher.cpp
    network/sunrpc/SunRpcXdr.cpp
    network/sunrpc/SunRpcSerializer.cpp
)

target_include_directories(rdmnetsunrpc PRIVATE
    ${CMAKE_BINARY_DIR}                                
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/network/sunrpc
    ${TIRPC_INCLUDE_DIRS}
)

# Link the module to the shared core to ensure they share the same memory footprint
target_link_libraries(rdmnetsunrpc PRIVATE rdmcore ${TIRPC_LIBRARIES})

# ==============================================================================
# Common Function for LDM Binaries
# ==============================================================================
function(addLDMBinary TARGET_NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "addLDMBinary requires SOURCES to be specified.")
    endif()

    add_executable(${TARGET_NAME} ${ARG_SOURCES})

    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${TIRPC_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/tools/pqact 
    )

    if(ARG_INCLUDES)
        target_include_directories(${TARGET_NAME} PRIVATE ${ARG_INCLUDES})
    endif()

    target_link_libraries(${TARGET_NAME} PRIVATE rdmcore)

    install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endfunction()

# ==============================================================================
# Subdirectories and Post-Build Steps
# ==============================================================================

if(RDM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

add_subdirectory(apps)

# Install rules to copy libraries and plugins to their proper destinations
install(TARGETS rdmcore rdmnetsunrpc rdmstorepq
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Standard folder skeleton for the system
install(DIRECTORY DESTINATION var/logs)
install(DIRECTORY DESTINATION var/queues)
install(DIRECTORY DESTINATION var/data)
install(DIRECTORY DESTINATION var/run)
