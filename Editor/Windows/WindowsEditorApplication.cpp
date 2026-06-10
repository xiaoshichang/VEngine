#include "Editor/Windows/WindowsEditorApplication.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <utility>

namespace ve::editor
{
    WindowsEditorApplication::WindowsEditorApplication(ve::ApplicationInitParam desc)
        : ve::Application(std::move(desc))
    {
    }

    WindowsEditorApplication::~WindowsEditorApplication()
    {
        UnInit();
    }

    int WindowsEditorApplication::Init()
    {
        VE_LOG_INFO("Initializing Windows editor application.");
        const int result = ve::Application::Init();
        if (result != 0)
        {
            return result;
        }

        const ErrorCode editorResult = editor_.Init(GetRuntime());
        VE_ASSERT_MESSAGE(editorResult == ErrorCode::None, "Editor::Init failed.");
        return result;
    }

    void WindowsEditorApplication::Run()
    {
        ve::Application::Run();
    }

    void WindowsEditorApplication::UnInit()
    {
        editor_.UnInit();
        ve::Application::UnInit();
    }
} // namespace ve::editor
