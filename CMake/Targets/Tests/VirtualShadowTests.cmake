include_guard(GLOBAL)

function(ve_add_virtual_shadow_tests)
    add_executable(VEngineVirtualShadowTests
        Tests/Unit/VirtualShadowTests.cpp
    )

    target_link_libraries(VEngineVirtualShadowTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineVirtualShadowTests)

    add_test(
        NAME VEngineVirtualShadowTests
        COMMAND $<TARGET_FILE:VEngineVirtualShadowTests>
    )
endfunction()
