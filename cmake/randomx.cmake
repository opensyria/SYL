# Copyright (c) 2025 The OpenSY developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# RandomX library integration for ASIC-resistant Proof-of-Work
# RandomX is used by Monero and provides CPU-optimized mining
# Documentation: https://github.com/tevador/RandomX

include(FetchContent)

message(STATUS "Configuring RandomX library...")

# SECURITY FIX [F-01]: Use URL with cryptographic hash verification instead of GIT_TAG
# This prevents supply-chain attacks where a compromised upstream could inject malicious code.
# Hash verified: curl -sL https://github.com/tevador/RandomX/archive/refs/tags/v1.2.1.tar.gz | shasum -a 256
FetchContent_Declare(
    randomx
    URL            https://github.com/tevador/RandomX/archive/refs/tags/v1.2.1.tar.gz
    URL_HASH       SHA256=2e6dd3bed96479332c4c8e4cab2505699ade418a07797f64ee0d4fa394555032
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Disable building RandomX tests and benchmarks
set(RANDOMX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RANDOMX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(RANDOMX_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# Fetch and build RandomX
FetchContent_MakeAvailable(randomx)

# Verify RandomX target is available
if(NOT TARGET randomx)
    message(FATAL_ERROR "RandomX target not found after FetchContent")
endif()

# Make RandomX headers available as SYSTEM includes to suppress warnings in downstream code
FetchContent_GetProperties(randomx)
# Remove any existing include directories and re-add as SYSTEM
get_target_property(_randomx_includes randomx INTERFACE_INCLUDE_DIRECTORIES)
if(_randomx_includes)
    set_target_properties(randomx PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
    target_include_directories(randomx SYSTEM INTERFACE ${_randomx_includes})
endif()
target_include_directories(randomx SYSTEM PUBLIC ${randomx_SOURCE_DIR}/src)

# Suppress documentation warnings from RandomX (upstream issue with @param comments)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(randomx PRIVATE -Wno-documentation)
endif()

message(STATUS "RandomX library configured successfully")
