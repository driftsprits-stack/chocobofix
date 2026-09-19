cmake_minimum_required(VERSION 3.20)
project(trackaccess VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- Pinned OR-Tools (vendored binary distribution) -------------------------
# Override with -DORTOOLS_ROOT=/path/to/or-tools
if(NOT DEFINED ORTOOLS_ROOT)
  if(APPLE)
    set(_ort_platform "${CMAKE_SYSTEM_PROCESSOR}_macOS-15.5")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
      set(_ort_platform "x86_64_Ubuntu-24.04")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
      set(_ort_platform "aarch64_Ubuntu-24.04")
    endif()
  endif()
  file(GLOB _ort_candidates "${CMAKE_SOURCE_DIR}/third_party/or-tools_${_ort_platform}_cpp_v9.14.6206")
  if(_ort_candidates)
    list(GET _ort_candidates 0 ORTOOLS_ROOT)
  endif()
endif()
if(NOT ORTOOLS_ROOT OR NOT EXISTS "${ORTOOLS_ROOT}")
  message(FATAL_ERROR "OR-Tools not found. Run scripts/fetch_deps.sh or pass -DORTOOLS_ROOT=...")
endif()
message(STATUS "OR-Tools root: ${ORTOOLS_ROOT}")
# The binary distribution bundles its own abseil/protobuf/re2/scip/... configs.
# They must be on CMAKE_PREFIX_PATH so ortoolsConfig's find_dependency() calls
# resolve against the vendored copies rather than anything system-wide.
list(PREPEND CMAKE_PREFIX_PATH "${ORTOOLS_ROOT}")
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
find_package(ortools REQUIRED CONFIG)

add_executable(ortools_smoke tests/ortools_smoke.cpp)
target_link_libraries(ortools_smoke PRIVATE ortools::ortools)

# --- core library ----------------------------------------------------------
add_library(ta_core STATIC
  src/core/csv.cpp
  src/core/instance.cpp
  src/core/schedule.cpp
  src/validator/validator.cpp)
target_include_directories(ta_core PUBLIC src)
target_compile_options(ta_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(check_sample tests/check_sample.cpp)
target_link_libraries(check_sample PRIVATE ta_core)

add_library(ta_solver STATIC src/solver/solver.cpp)
target_include_directories(ta_solver PUBLIC src)
target_link_libraries(ta_solver PUBLIC ta_core ortools::ortools)

add_executable(trackaccess src/cli/main.cpp)
target_link_libraries(trackaccess PRIVATE ta_solver)

# --- shared-project persistence -------------------------------------------
# SQLite is the system library on both macOS and mainstream Linux. Password
# hashing uses the platform's vetted PBKDF2: CommonCrypto on Apple, OpenSSL
# elsewhere. Nothing cryptographic is implemented in this project.
find_package(SQLite3 REQUIRED)
add_library(ta_store STATIC src/service/store.cpp src/service/crypto.cpp)
target_include_directories(ta_store PUBLIC src)
target_link_libraries(ta_store PUBLIC ta_core SQLite::SQLite3)
if(APPLE)
  message(STATUS "Password hashing: CommonCrypto PBKDF2-HMAC-SHA256")
else()
  find_package(OpenSSL REQUIRED)
  target_compile_definitions(ta_store PRIVATE TA_HAVE_OPENSSL=1)
  target_link_libraries(ta_store PUBLIC OpenSSL::Crypto)
  message(STATUS "Password hashing: OpenSSL PBKDF2-HMAC-SHA256")
endif()

add_executable(test_store tests/test_store.cpp)
target_link_libraries(test_store PRIVATE ta_store)

add_executable(trackaccess-service src/service/main.cpp)
target_include_directories(trackaccess-service PRIVATE third_party)
target_link_libraries(trackaccess-service PRIVATE ta_store)
find_package(Threads REQUIRED)
target_link_libraries(trackaccess-service PRIVATE Threads::Threads)

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE ta_core)
enable_testing()
add_test(NAME core COMMAND test_core WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME sample COMMAND check_sample WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME store COMMAND test_store WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
