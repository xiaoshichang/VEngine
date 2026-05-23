include_guard(GLOBAL)

function(ve_add_tests)
    if(NOT VE_BUILD_TESTS)
        return()
    endif()

    if(NOT WIN32)
        message(STATUS "VEngine unit tests are only built on Windows in the first stage.")
        return()
    endif()

    add_executable(VEngineTests
        Tests/Unit/CoreTests.cpp
    )

    target_link_libraries(VEngineTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineTests)

    add_test(
        NAME VEngineTests
        COMMAND $<TARGET_FILE:VEngineTests>
    )

    add_executable(VEngineLoggingTests
        Tests/Unit/LoggingTests.cpp
    )

    target_link_libraries(VEngineLoggingTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineLoggingTests)

    add_test(
        NAME VEngineLoggingTests
        COMMAND $<TARGET_FILE:VEngineLoggingTests>
    )

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

    add_executable(VEngineFileSystemTests
        Tests/Unit/FileSystemTests.cpp
    )

    target_link_libraries(VEngineFileSystemTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineFileSystemTests)

    add_test(
        NAME VEngineFileSystemTests
        COMMAND $<TARGET_FILE:VEngineFileSystemTests>
    )

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

    add_executable(VEngineThreadingTests
        Tests/Unit/ThreadingTests.cpp
    )

    target_link_libraries(VEngineThreadingTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineThreadingTests)

    add_test(
        NAME VEngineThreadingTests
        COMMAND $<TARGET_FILE:VEngineThreadingTests>
    )

    if(VE_BUILD_SHADER_TESTS)
        ve_add_shader_tool()

        set(veShaderTestOutputDirectory "${CMAKE_BINARY_DIR}/Generated/ShaderTests/$<CONFIG>")

        add_test(
            NAME VEngineShaderToolHelp
            COMMAND $<TARGET_FILE:VEngineShaderTool> --help
        )

        add_test(
            NAME VEngineShaderToolCompileBasicTriangle
            COMMAND
                $<TARGET_FILE:VEngineShaderTool>
                compile
                --source ${PROJECT_SOURCE_DIR}/Tests/Shaders/HLSL/BasicTriangle.hlsl
                --output ${veShaderTestOutputDirectory}/BasicTriangle
                --name BasicTriangle
                --dxc ${VE_DXC_EXECUTABLE}
                --fxc ${VE_FXC_EXECUTABLE}
                --spirv-cross ${VE_SPIRV_CROSS_EXECUTABLE}
        )

        add_test(
            NAME VEngineShaderToolValidateBasicTriangle
            COMMAND
                ${CMAKE_COMMAND}
                -DVE_SHADER_TEST_OUTPUT_DIR=${veShaderTestOutputDirectory}/BasicTriangle
                -P ${PROJECT_SOURCE_DIR}/Tests/Shaders/ValidateShaderArtifacts.cmake
        )

        add_test(
            NAME VEngineShaderToolRejectsMissingRegister
            COMMAND
                $<TARGET_FILE:VEngineShaderTool>
                compile
                --source ${PROJECT_SOURCE_DIR}/Tests/Shaders/HLSL/MissingRegister.hlsl
                --output ${veShaderTestOutputDirectory}/MissingRegister
                --name MissingRegister
                --dxc ${VE_DXC_EXECUTABLE}
                --fxc ${VE_FXC_EXECUTABLE}
                --spirv-cross ${VE_SPIRV_CROSS_EXECUTABLE}
        )

        set_tests_properties(VEngineShaderToolCompileBasicTriangle
            PROPERTIES
                FIXTURES_SETUP VEngineShaderArtifacts
        )

        set_tests_properties(VEngineShaderToolValidateBasicTriangle
            PROPERTIES
                FIXTURES_REQUIRED VEngineShaderArtifacts
        )

        set_tests_properties(VEngineShaderToolRejectsMissingRegister
            PROPERTIES
                WILL_FAIL TRUE
        )
    endif()
endfunction()
