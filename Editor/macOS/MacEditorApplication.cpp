#include "Editor/macOS/MacEditorApplication.h"

#include "Engine/Runtime/Logging/Log.h"

#include <utility>

namespace ve::editor
{
    MacEditorApplication::MacEditorApplication(ve::ApplicationInitParam initParam)
        : ve::Application(std::move(initParam))
    {
    }

    MacEditorApplication::~MacEditorApplication()
    {
        UnInit();
    }

    int MacEditorApplication::Init()
    {
        VE_LOG_INFO("Initializing macOS editor application.");
        return ve::Application::Init();
    }

    void MacEditorApplication::Run()
    {
        ve::Application::Run();
    }

    void MacEditorApplication::UnInit()
    {
        ve::Application::UnInit();
    }
} // namespace ve::editor
