include_guard(GLOBAL)

function(ve_add_time_tests)
    add_executable(VEngineTimeTests
        Tests/Unit/TimeTests.cpp
    )

    target_link_libraries(VEngineTimeTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineTimeTests)

    add_test(
        NAME VEngineTimeTests
        COMMAND $<TARGET_FILE:VEngineTimeTests>
    )
endfunction()
