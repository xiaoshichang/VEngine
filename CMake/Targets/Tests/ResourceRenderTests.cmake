include_guard(GLOBAL)

function(ve_add_resource_render_tests)
    add_executable(VEngineResourceRenderTests
        Tests/Unit/ResourceRenderTests.cpp
    )

    target_link_libraries(VEngineResourceRenderTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineResourceRenderTests)

    add_test(
        NAME VEngineResourceRenderTests
        COMMAND $<TARGET_FILE:VEngineResourceRenderTests>
    )
endfunction()
