include_guard(GLOBAL)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

get_filename_component(_VE_BOOST_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_BOOST_ROOT "" CACHE PATH "Optional Boost installation root override.")
set(VE_BOOST_COMPILER "" CACHE STRING "Optional Boost compiler tag override, such as vc142 or clang-darwin.")
set(VE_BOOST_COMPONENTS "json;log;log_setup;system" CACHE STRING "Boost components required by VEngine.")

function(ve_get_default_boost_root outVariable)
    set(boostRoot "")

    if(WIN32)
        set(boostRoot "${_VE_BOOST_REPOSITORY_ROOT}/ThirdParty/Boost/Bin/Windows64")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(VE_IOS_PLATFORM STREQUAL "DEVICE")
            set(boostRoot "${_VE_BOOST_REPOSITORY_ROOT}/ThirdParty/Boost/Bin/IOS/device")
        else()
            set(boostRoot "${_VE_BOOST_REPOSITORY_ROOT}/ThirdParty/Boost/Bin/IOS/simulator")
        endif()
    endif()

    set(${outVariable} "${boostRoot}" PARENT_SCOPE)
endfunction()

function(ve_detect_boost_compiler boostRoot outVariable)
    set(detectedCompilers "")

    foreach(boostComponent IN LISTS VE_BOOST_COMPONENTS)
        file(GLOB variantFiles
            "${boostRoot}/lib/cmake/boost_${boostComponent}-*/libboost_${boostComponent}-variant-*.cmake"
        )

        foreach(variantFile IN LISTS variantFiles)
            get_filename_component(variantName "${variantFile}" NAME)

            if(variantName MATCHES "^libboost_[A-Za-z0-9_]+-variant-(.+)-mt.*\\.cmake$")
                list(APPEND detectedCompilers "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES detectedCompilers)
    list(LENGTH detectedCompilers detectedCompilerCount)

    if(detectedCompilerCount EQUAL 1)
        list(GET detectedCompilers 0 detectedCompiler)
        set(${outVariable} "${detectedCompiler}" PARENT_SCOPE)
    else()
        set(${outVariable} "" PARENT_SCOPE)
    endif()
endfunction()

function(ve_setup_boost_library targetName)
    if(NOT TARGET ${targetName})
        message(FATAL_ERROR "ve_setup_boost_library target does not exist: ${targetName}")
    endif()

    if(VE_BOOST_ROOT)
        set(boostRoot "${VE_BOOST_ROOT}")
    else()
        ve_get_default_boost_root(boostRoot)
    endif()

    if(boostRoot)
        if(NOT EXISTS "${boostRoot}")
            message(FATAL_ERROR
                "Boost root does not exist: ${boostRoot}\n"
                "Build Boost with the script under ThirdParty/Boost or set VE_BOOST_ROOT to a valid installation."
            )
        endif()

        list(PREPEND CMAKE_PREFIX_PATH "${boostRoot}")
        set(BOOST_ROOT "${boostRoot}")
        set(Boost_ROOT "${boostRoot}")
    endif()

    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_STATIC_RUNTIME OFF)

    if(VE_BOOST_COMPILER)
        set(Boost_COMPILER "${VE_BOOST_COMPILER}")
    elseif(NOT DEFINED Boost_COMPILER OR Boost_COMPILER STREQUAL "")
        ve_detect_boost_compiler("${boostRoot}" detectedBoostCompiler)

        if(detectedBoostCompiler)
            set(Boost_COMPILER "${detectedBoostCompiler}")
        endif()
    endif()

    message(STATUS "Boost root: ${boostRoot}")
    message(STATUS "Boost components: ${VE_BOOST_COMPONENTS}")
    if(Boost_COMPILER)
        message(STATUS "Boost compiler tag: ${Boost_COMPILER}")

        # Boost 1.85 does not recognize newer MSVC 19.4x toolsets, so use the installed variant tag.
        if(NOT Boost_COMPILER MATCHES ";" AND NOT BOOST_DETECTED_TOOLSET)
            set(BOOST_DETECTED_TOOLSET "${Boost_COMPILER}")
        endif()
    endif()

    find_package(Boost CONFIG REQUIRED COMPONENTS ${VE_BOOST_COMPONENTS})

    if(NOT TARGET VEngineBoost)
        add_library(VEngineBoost INTERFACE)
        add_library(VEngine::Boost ALIAS VEngineBoost)

        target_compile_definitions(VEngineBoost
            INTERFACE
                BOOST_ALL_NO_LIB
        )

        if(WIN32)
            target_compile_definitions(VEngineBoost
                INTERFACE
                    _WIN32_WINNT=0x0602
            )
        endif()

        set(boostTargets "")
        foreach(boostComponent IN LISTS VE_BOOST_COMPONENTS)
            if(NOT TARGET Boost::${boostComponent})
                message(FATAL_ERROR "Boost component target was not created: Boost::${boostComponent}")
            endif()

            list(APPEND boostTargets Boost::${boostComponent})
        endforeach()

        target_link_libraries(VEngineBoost
            INTERFACE
                ${boostTargets}
        )
    endif()

    target_link_libraries(${targetName}
        PUBLIC
            VEngine::Boost
    )
endfunction()
