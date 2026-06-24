include_guard(GLOBAL)

function(ve_add_tests)
    if(NOT VE_BUILD_TESTS)
        return()
    endif()

    if(NOT WIN32)
        message(STATUS "VEngine unit tests are only built on Windows in the first stage.")
        return()
    endif()

    add_executable(VEngineMemoryTests
        Tests/Unit/MemoryTests.cpp
    )

    target_link_libraries(VEngineMemoryTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineMemoryTests)

    add_test(
        NAME VEngineMemoryTests
        COMMAND $<TARGET_FILE:VEngineMemoryTests>
    )

    add_executable(VEngineMathTests
        Tests/Unit/MathTests.cpp
    )

    target_link_libraries(VEngineMathTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineMathTests)

    add_test(
        NAME VEngineMathTests
        COMMAND $<TARGET_FILE:VEngineMathTests>
    )

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

    add_executable(VEngineSceneSerializationTests
        Tests/Unit/SceneSerializationTests.cpp
    )

    target_link_libraries(VEngineSceneSerializationTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineSceneSerializationTests)

    add_test(
        NAME VEngineSceneSerializationTests
        COMMAND $<TARGET_FILE:VEngineSceneSerializationTests>
    )

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

    add_executable(VEngineJoltIntegrationTests
        Tests/Unit/JoltIntegrationTests.cpp
    )

    target_link_libraries(VEngineJoltIntegrationTests
        PRIVATE
            VEngine
            VEngine::Jolt
    )

    ve_configure_target(VEngineJoltIntegrationTests)

    add_test(
        NAME VEngineJoltIntegrationTests
        COMMAND $<TARGET_FILE:VEngineJoltIntegrationTests>
    )

endfunction()
