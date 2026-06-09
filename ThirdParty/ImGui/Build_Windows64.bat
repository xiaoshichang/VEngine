@echo off
setlocal EnableExtensions

set "IMGUI_ROOT=%~dp0imgui-1.92.8"

if not exist "%IMGUI_ROOT%\imgui.cpp" (
    echo Missing Dear ImGui source: "%IMGUI_ROOT%\imgui.cpp"
    exit /b 1
)

if not exist "%IMGUI_ROOT%\backends\imgui_impl_win32.cpp" (
    echo Missing Dear ImGui Win32 backend: "%IMGUI_ROOT%\backends\imgui_impl_win32.cpp"
    exit /b 1
)

if not exist "%IMGUI_ROOT%\backends\imgui_impl_dx11.cpp" (
    echo Missing Dear ImGui D3D11 backend: "%IMGUI_ROOT%\backends\imgui_impl_dx11.cpp"
    exit /b 1
)

if not exist "%IMGUI_ROOT%\backends\imgui_impl_dx12.cpp" (
    echo Missing Dear ImGui D3D12 backend: "%IMGUI_ROOT%\backends\imgui_impl_dx12.cpp"
    exit /b 1
)

echo Dear ImGui vendored source ready: "%IMGUI_ROOT%"
exit /b 0
