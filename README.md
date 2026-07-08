# RDM: Modern C++ LDM (Local Data Manager)

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-%3E%3D3.10-green.svg)](https://cmake.org/)

> **A Note on Project Origins:** 
> RDM was born out of necessity to support the cloud migration and deployment requirements of MRMS, WDSS2, HMET, and RAPIO. By transitioning from legacy C to modern C++, we proactively addressed the security vulnerabilities and technical debt associated with aging codebases and outdated libraries. This is a living project—as community interest grows, expanded documentation and detailed attribution will follow.

**RDM (Remote Data Manager)** is a high-performance, object-oriented modernization of the classic Unidata Local Data Manager (LDM). Rewritten from the ground up in **C++17**, RDM brings decades of proven event-driven data distribution into the modern era. It provides robust memory safety, a flexible plugin architecture, and blazing-fast performance while maintaining compatibility with legacy data-sharing topologies.

## 🚀 Why RDM? (The C++ Advantage)

The original LDM has been a workhorse for the meteorological and scientific communities for years. RDM takes that battle-tested design and supercharges it with modern software engineering principles:

* **Memory Safety & RAII:** Legacy C-style memory leaks and dangling pointers are eradicated. RDM heavily utilizes `std::unique_ptr` and `std::shared_ptr` for deterministic memory management and strict lifecycle control.
* **Plugin-Based Architecture:** The core engine is completely decoupled from storage and networking. 
    * **Storage (`rdmstorepq`):** The memory-mapped Product Queue (PQ) is now an implementation of the `IProductStore` interface, dynamically loaded at runtime.
    * **Networking (`rdmnetsunrpc`):** Legacy Sun RPC networking is abstracted behind `IServer` and `IClient` interfaces, allowing future integrations (e.g., gRPC, ZeroMQ, or WebSockets) without touching the core engine.
* **Modern Logging:** Replaced the legacy logging infrastructure with **spdlog**, providing thread-safe, extremely fast, and highly configurable logging capabilities.
* **Type Safety & OOP:** Brittle bitmasks, global variables, and raw macros have been refactored into robust C++ classes (e.g., `FeedType`, `Timestamp`, `Signature`, `ProdClass`), eliminating entire classes of runtime errors.
* **Enhanced Concurrency:** Utilizes modern C++ threading primitives (`std::mutex`, `std::atomic`) while maintaining strict POSIX locks for safe inter-process communication (IPC) across the memory-mapped queues.

## 🏗️ System Architecture

RDM is divided into modular components to ensure maintainability and scalability:

* **`rdmcore`:** The heart of the system. Contains all core abstractions, signal management, access control lists (ACL), process tracking, and the XML-backed `RegistryEngine`.
* **`rdmstorepq`:** The classic memory-mapped Product Queue engine, modernized with boundary checks, clean memory mapping strategies (`FileRegionMapper`, `ChunkedMmapMapper`), and safe locking wrappers.
* **`rdmnetsunrpc`:** The TI-RPC backed networking layer that handles `FEEDME`, `NOTIFYME`, and `HEREIS` protocols.
* **Applications:** Familiar CLI tools completely rebuilt in C++, including:
    * `ldmd`: The main server daemon.
    * `pqact`: The downstream action and file-dispatching engine.
    * `pqinsert` / `pqcat` / `pqcheck` / `pqmon`: Queue inspection and manipulation tools.
    * `ldmping` / `feedme` / `notifyme`: Network verification and data-request utilities.

## 🛠️ Getting Started

### Prerequisites

To build RDM, you will need a Linux/Unix environment with the following dependencies:

* **Compiler:** GCC or Clang with C++17 support
* **Build System:** CMake >= 3.10
* **Libraries:**
    * `libtirpc` (Transport Independent RPC)
    * `libxml2` (For the Registry Engine)
    * `openssl` (For MD5 Signature generation)
    * `zlib`
    * `CUnit` (For building the test suite)

### Building from Source

RDM uses a standard CMake out-of-source build workflow. You can either use our quick-start script or run the CMake configuration manually.

#### Option 1: Quick Build (via autogen.sh)
The easiest way to build and install RDM is using the provided `autogen.sh` script. This automates the CMake configuration, creates the `BUILD` directory, and compiles the code using all available CPU cores:

```bash
# 1. Clone the repository
git clone https://github.com/yourusername/rdm.git
cd rdm

# 2. Run the auto-generation script
./autogen.sh
```

#### Option 2: Manual CMake Build
If you prefer granular control over the build process and configuration flags:

```bash
# 1. Clone the repository
git clone https://github.com/yourusername/rdm.git
cd rdm

# 2. Create a build directory
mkdir BUILD && cd BUILD

# 3. Configure the project
cmake .. -DCMAKE_BUILD_TYPE=Release -DRDM_BUILD_TESTS=ON

# 4. Compile (using all available cores)
make -j$(nproc)

# 5. Install (may require sudo depending on your install prefix)
make install
```

## 🧪 Testing

RDM includes a comprehensive modernization test suite utilizing **CUnit** for unit testing and bash scripts for integration testing. The test suite aggressively targets concurrency, queue state integrity, regex pattern matching, and memory leaks.

There are two ways to run the tests:

**1. CMake Integrated Unit Tests (CTest)**
To run the underlying CUnit tests with verbose output, execute `ctest` from within your `BUILD` directory:

```bash
cd BUILD
ctest -V
```

**2. Full Integration Test Suite**
To run the complete suite—including the multi-process pipeline, networking checks, and queue corruption tests—use the provided script in the `test/` directory:

```bash
cd test
./testall.sh
```

## 🤝 Contributing

Contributions are welcome! Whether it's porting legacy functionality, improving the CMake build system, or adding new storage/network plugins, please feel free to open an issue or submit a Pull Request. 

## 📜 License

RDM is released under the [Apache License 2.0](LICENSE).
