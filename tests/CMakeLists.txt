include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(trading_oms_rms_tests
    smoke_test.cpp
    types_test.cpp
)

target_link_libraries(trading_oms_rms_tests
    PRIVATE
        oms_domain
        GTest::gtest_main
)

target_compile_options(trading_oms_rms_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Werror>
)

include(GoogleTest)
gtest_discover_tests(trading_oms_rms_tests DISCOVERY_MODE PRE_TEST)
