include_guard(GLOBAL)

function(ve_add_jolt_integration_tests)
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
