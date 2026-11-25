cmake_minimum_required(VERSION 3.20)

project(trading_oms_rms_demo
    VERSION 0.1.0
    DESCRIPTION "Trading OMS/RMS demo"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(oms_domain
    src/order.cpp
    src/order_book.cpp
    src/order_state.cpp
    src/oms.cpp
    src/validation.cpp
    src/journal.cpp
    src/matching_engine.cpp
)
target_include_directories(oms_domain PUBLIC include)
target_compile_options(oms_domain PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Werror>
)

add_executable(trading_oms_rms_demo src/main.cpp)
target_link_libraries(trading_oms_rms_demo PRIVATE oms_domain)
target_compile_options(trading_oms_rms_demo PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Werror>
)

enable_testing()
add_subdirectory(tests)

# Skeleton directories for future stories
# bench/  - benchmarks
# docs/adr/ - architecture decision records
