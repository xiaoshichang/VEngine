include_guard(GLOBAL)

include(CMake/Targets/Demos/RhiMetalTriangle.cmake)

function(ve_add_rhi_demos)
    ve_add_rhi_metal_triangle_demo()
endfunction()
