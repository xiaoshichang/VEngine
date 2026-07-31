include_guard(GLOBAL)

function(ve_add_assertion_logging_tests)
    add_executable(VEngineAssertionLoggingTests
        Tests/Unit/AssertionLoggingTests.cpp
    )

    target_link_libraries(VEngineAssertionLoggingTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineAssertionLoggingTests)

    add_test(
        NAME VEngineAssertionLoggingTests
        COMMAND $<TARGET_FILE:VEngineAssertionLoggingTests>
    )
endfunction()
