cmake_minimum_required(VERSION 3.20)

project(trading_oms_rms_demo
    VERSION 0.1.0
    DESCRIPTION "Trading OMS/RMS demo"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(trading_oms_rms_demo src/main.cpp)
target_include_directories(trading_oms_rms_demo PRIVATE include)

target_compile_options(trading_oms_rms_demo PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Werror>
)

enable_testing()
add_test(NAME smoke COMMAND trading_oms_rms_demo)

# Skeleton directories for future stories
# tests/  - unit/integration tests
# bench/  - benchmarks
# docs/adr/ - architecture decision records
