#include "Engine/Runtime/Core/Application.h"
#include "Engine/Runtime/Core/Version.h"

#include <boost/json.hpp>

#include <iostream>
#include <string_view>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }

    return condition;
}
}

int main()
{
    bool passed = true;

    ve::Application application("VEngineTests");
    application.RunOnce();

    passed &= Expect(application.GetName() == "VEngineTests", "Application name should match constructor input");
    passed &= Expect(application.GetExitCode() == 0, "Application exit code should default to success after RunOnce");

    const ve::BuildInfo buildInfo = ve::GetBuildInfo();
    passed &= Expect(std::string_view(buildInfo.projectName) == "VEngine", "Project name should be VEngine");

    boost::json::object boostJsonSmokeValue;
    boostJsonSmokeValue["project"] = buildInfo.projectName;
    passed &= Expect(boostJsonSmokeValue.at("project").as_string() == "VEngine", "Boost.JSON should be available");

    if (passed)
    {
        std::cout << "VEngineTests passed" << '\n';
        return 0;
    }

    return 1;
}
