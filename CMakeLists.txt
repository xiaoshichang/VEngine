cmake_minimum_required(VERSION 3.25)

project(VEngine
    VERSION 0.1.0
    DESCRIPTION "A lightweight cross-platform 3D mini game engine"
    LANGUAGES CXX
)

include(CMake/VEngineOptions.cmake)
include(CMake/VEngineTarget.cmake)

include(CMake/ThirdParty/SetupThirdParty.cmake)
include(CMake/ThirdParty/SetupBoostLibrary.cmake)
include(CMake/ThirdParty/SetupWindowsSdkTools.cmake)
include(CMake/ThirdParty/SetupDirectXShaderCompiler.cmake)
include(CMake/ThirdParty/SetupSlang.cmake)
include(CMake/ThirdParty/SetupImGui.cmake)
include(CMake/ThirdParty/SetupJolt.cmake)

include(CMake/Targets/Engine.cmake)
include(CMake/Targets/EngineScriptHost.cmake)
include(CMake/Targets/Applications.cmake)
include(CMake/Targets/Tools.cmake)
include(CMake/Targets/Tests.cmake)
include(CMake/Targets/Demos.cmake)

ve_add_engine()

ve_add_player()
ve_add_editor()

if(VE_BUILD_TESTS)
    enable_testing()
endif()

ve_add_tests()
ve_add_rhi_demos()
ve_add_tools()
