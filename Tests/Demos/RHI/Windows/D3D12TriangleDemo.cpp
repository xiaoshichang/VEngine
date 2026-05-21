#include "Engine/RHI/D3D12/D3D12Rhi.h"
#include "Tests/Demos/RHI/Windows/WindowsTriangleDemo.h"

#include <cstdlib>
#include <cstring>

namespace
{
uint32_t ParseMaxFrames(int argc, char* argv[])
{
    if (argc == 3 && std::strcmp(argv[1], "--frames") == 0)
    {
        return static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    }

    return 0;
}
}

int main(int argc, char* argv[])
{
    return ve::tests::RunWindowsTriangleDemo(
        "VEngine RHI D3D12 Triangle Demo",
        ve::rhi::CreateD3D12Device(false),
        ParseMaxFrames(argc, argv));
}
