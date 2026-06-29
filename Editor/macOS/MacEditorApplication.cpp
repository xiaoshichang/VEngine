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

        return result;
    }

    void MacEditorApplication::Run()
    {
        ve::Application::Run();
    }

    void MacEditorApplication::UnInit()
    {
        if (GetRuntime().IsInitialized())
        {
            GetRuntime().GetSceneSystem().Shutdown();
        }

        editor_.UnInit();
        ve::Application::UnInit();
    }
} // namespace ve::editor
