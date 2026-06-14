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

    add_executable(VEngineResourceManifestTests
        Tests/Unit/ResourceManifestTests.cpp
    )

    target_link_libraries(VEngineResourceManifestTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineResourceManifestTests)

    add_test(
        NAME VEngineResourceManifestTests
        COMMAND $<TARGET_FILE:VEngineResourceManifestTests>
    )

endfunction()
