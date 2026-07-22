#include "Editor/Windows/WindowsEditorApplication.h"

#include "Editor/Windows/WinEditorWindowPlacement.h"
#include "Engine/Runtime/Logging/Log.h"

#include <utility>

namespace ve::editor
{
    WindowsEditorApplication::WindowsEditorApplication(ve::ApplicationInitParam initParam, EditorStartupOptions startupOptions)
        : ve::Application(std::move(initParam))
        , startupOptions_(std::move(startupOptions))
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

        const ErrorCode editorResult = editor_.Init(GetRuntime(), GetMainThreadCommandQueue(), GetMainWindowNativeHandle());
        if (editorResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Editor::Init failed: {}", ToString(editorResult));
            return 1;
        }

        PlaceWindowsEditorStartupWindows(GetMainWindowNativeHandle());
        QueueEditorStartupOptions(editor_, startupOptions_);

        return result;
    }

    void WindowsEditorApplication::Run()
    {
        ve::Application::Run();
    }

    void WindowsEditorApplication::UnInit()
    {
        if (GetRuntime().IsInitialized())
        {
            GetRuntime().GetSceneSystem().Shutdown();
        }

        editor_.UnInit();
        ve::Application::UnInit();
    }
} // namespace ve::editor
