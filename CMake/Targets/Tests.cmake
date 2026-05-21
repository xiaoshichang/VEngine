include_guard(GLOBAL)

function(ve_add_tests)
    if(NOT VE_BUILD_TESTS)
        return()
    endif()

    if(NOT WIN32)
        message(STATUS "VEngineTests is only built on Windows in the first stage.")
        return()
    endif()

    enable_testing()

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
