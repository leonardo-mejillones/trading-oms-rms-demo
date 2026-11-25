add_executable(latency_bench latency_bench.cpp)

target_link_libraries(latency_bench PRIVATE oms_domain)

target_compile_options(latency_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Werror>
)
