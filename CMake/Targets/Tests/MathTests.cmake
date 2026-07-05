include_guard(GLOBAL)

function(ve_add_math_tests)
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
endfunction()
