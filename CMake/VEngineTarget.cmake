include_guard(GLOBAL)

function(ve_configure_target targetName)
    if(NOT TARGET ${targetName})
        message(FATAL_ERROR "ve_configure_target target does not exist: ${targetName}")
    endif()

    set_target_properties(${targetName}
        PROPERTIES
            CXX_EXTENSIONS OFF
    )

    target_compile_features(${targetName}
        PRIVATE
            cxx_std_20
    )

    if(MSVC)
        target_compile_options(${targetName}
            PRIVATE
                /W4
                /permissive-
        )
    else()
        target_compile_options(${targetName}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    endif()
endfunction()
