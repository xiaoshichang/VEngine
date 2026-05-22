#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Core/Version.h"

#include <iostream>

int main()
{
    ve::Application application("VEngineEditor");
    application.RunOnce();

    const ve::BuildInfo buildInfo = ve::GetBuildInfo();
    std::cout << application.GetName() << " ready (" << buildInfo.projectName << " " << buildInfo.version << ", "
              << buildInfo.platform << ")" << '\n';

    return application.GetExitCode();
}
