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
    events_test.cpp
    order_book_test.cpp
    matching_test.cpp
    cancel_test.cpp
    market_order_test.cpp
    replace_test.cpp
    invariants_test.cpp
    order_state_test.cpp
    idempotency_test.cpp
    validation_test.cpp
    journal_test.cpp
    risk_test.cpp
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
