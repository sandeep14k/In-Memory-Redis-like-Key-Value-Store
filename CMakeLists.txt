cmake_minimum_required(VERSION 3.14)
project(kvstore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimised build (benchmark numbers are meaningless otherwise).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(KV_BUILD_TESTS "Build unit tests" ON)
option(KV_BUILD_BENCH "Build benchmark" ON)

# Everything except main() lives in a library so tests and the benchmark can reuse it.
add_library(kvcore STATIC
  src/StorageEngine.cpp
  src/ExpiryManager.cpp
  src/Database.cpp
  src/CommandParser.cpp
)
target_include_directories(kvcore PUBLIC include)

add_executable(kvstore src/main.cpp)
target_link_libraries(kvstore PRIVATE kvcore)

if(KV_BUILD_TESTS)
  enable_testing()
  add_executable(kv_tests
    tests/test_main.cpp
    tests/test_storage.cpp
    tests/test_expiry.cpp
    tests/test_database.cpp
    tests/test_parser.cpp
  )
  target_include_directories(kv_tests PRIVATE tests)
  target_link_libraries(kv_tests PRIVATE kvcore)
  add_test(NAME kv_tests COMMAND kv_tests)
endif()

if(KV_BUILD_BENCH)
  add_executable(bench benchmarks/bench.cpp)
  target_link_libraries(bench PRIVATE kvcore)
endif()
