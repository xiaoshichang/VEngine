include_guard(GLOBAL)

function(ve_add_viewport_tests)
    add_executable(VEngineViewportTests
        Tests/Unit/ViewportTests.cpp
    )

    target_link_libraries(VEngineViewportTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineViewportTests)

    add_test(
        NAME VEngineViewportTests
        COMMAND $<TARGET_FILE:VEngineViewportTests>
    )
endfunction()
