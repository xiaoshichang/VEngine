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
endfunction()
