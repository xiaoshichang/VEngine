#include "Engine/Runtime/Core/Version.h"

#include <iostream>

int main(int argc, char* argv[])
{
    const ve::BuildInfo buildInfo = ve::GetBuildInfo();
    std::cout << "VEngineAssetTool ready (" << buildInfo.projectName << " " << buildInfo.version << ")" << '\n';

    if (argc > 1)
    {
        std::cout << "Input: " << argv[1] << '\n';
    }

    return 0;
}
