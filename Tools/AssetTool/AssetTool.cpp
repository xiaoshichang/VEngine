#include "Engine/Runtime/Core/Version.h"

#include <iostream>
#include <string_view>

namespace
{
    void PrintHelp()
    {
        std::cout << "VEngineAssetTool\n"
                  << "\n"
                  << "Commands:\n"
                  << "  --help\n"
                  << "  --version\n"
                  << "  <input>\n";
    }
}

int main(int argc, char* argv[])
{
    const ve::BuildInfo buildInfo = ve::GetBuildInfo();

    if (argc <= 1 || std::string_view(argv[1]) == "--help")
    {
        PrintHelp();
        std::cout << "Build: " << buildInfo.projectName << " " << buildInfo.version << '\n';
        return 0;
    }

    if (std::string_view(argv[1]) == "--version")
    {
        std::cout << buildInfo.projectName << " " << buildInfo.version << '\n';
        return 0;
    }

    std::cout << "VEngineAssetTool ready (" << buildInfo.projectName << " " << buildInfo.version << ")" << '\n';
    std::cout << "Input: " << argv[1] << '\n';
    return 0;
}
