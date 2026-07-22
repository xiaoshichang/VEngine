include_guard(GLOBAL)

function(ve_add_json_utils_tests)
    add_executable(VEngineJsonUtilsTests
        Tests/Unit/JsonUtilsTests.cpp
    )

    target_link_libraries(VEngineJsonUtilsTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineJsonUtilsTests)

    add_test(
        NAME VEngineJsonUtilsTests
        COMMAND $<TARGET_FILE:VEngineJsonUtilsTests>
    )
endfunction()
