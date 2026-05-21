cmake_minimum_required(VERSION 3.25)

project(VEngine
    VERSION 0.1.0
    DESCRIPTION "A lightweight cross-platform 3D mini game engine"
    LANGUAGES CXX
)

include(CMake/VEngineOptions.cmake)
include(CMake/VEngineTarget.cmake)

include(CMake/SetupBoostLibrary.cmake)
include(CMake/SetupDirectXShaderCompiler.cmake)
include(CMake/SetupSpirvCross.cmake)

include(CMake/Targets/Engine.cmake)
include(CMake/Targets/Applications.cmake)
include(CMake/Targets/Tools.cmake)
include(CMake/Targets/Tests.cmake)
include(CMake/Targets/Demos.cmake)

ve_add_engine()

ve_add_windows_player()
ve_add_windows_editor()
ve_add_ios_player()

ve_add_tests()
ve_add_rhi_demos()
ve_add_tools()
