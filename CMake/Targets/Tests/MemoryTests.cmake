include_guard(GLOBAL)

function(ve_add_memory_tests)
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
endfunction()
