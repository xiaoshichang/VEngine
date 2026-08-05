include_guard(GLOBAL)

function(ve_add_frame_graph_debug_tests)
    add_executable(VEngineFrameGraphDebugTests
        Tests/Unit/FrameGraphDebugTests.cpp
        Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp
        Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h
    )

    target_link_libraries(VEngineFrameGraphDebugTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineFrameGraphDebugTests)

    add_test(
        NAME VEngineFrameGraphDebugTests
        COMMAND $<TARGET_FILE:VEngineFrameGraphDebugTests>
    )
endfunction()
