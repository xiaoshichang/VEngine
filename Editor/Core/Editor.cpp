#include "Editor/Core/Editor.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

namespace ve::editor
{
    Editor::~Editor()
    {
        UnInit();
    }

    ErrorCode Editor::Init(EngineRuntime& runtime)
    {
        if (initialized_.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        sceneSystem_ = &runtime.GetSceneSystem();
        initialized_.store(true, std::memory_order_release);
        sceneSystem_->SetEditorCallback(SceneSystemEditorCallback{
            .onOSEvent = [this](const OSEvent& event) { OnOSEvent(event); },
            .onRender = [this]() { Render(); },
        });

        VE_LOG_INFO_CATEGORY("Editor", "Editor initialized.");
        return ErrorCode::None;
    }

    void Editor::OnOSEvent(const OSEvent& event)
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        switch (event.type)
        {
        case OSEventType::WindowFocusGained:
        case OSEventType::WindowFocusLost:
        case OSEventType::WindowMinimized:
        case OSEventType::WindowRestored:
        case OSEventType::WindowResized:
        case OSEventType::WindowShown:
        case OSEventType::WindowHidden:
            break;
        case OSEventType::FrameEndFenceSignal:
            VE_ASSERT_ALWAYS_MESSAGE(false, "Editor::OnOSEvent should not receive FrameEndFenceSignal.");
            break;
        }
    }

    void Editor::Render()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }
    }

    void Editor::UnInit() noexcept
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        initialized_.store(false, std::memory_order_release);
        if (sceneSystem_ != nullptr)
        {
            sceneSystem_->SetEditorCallback(SceneSystemEditorCallback{});
            sceneSystem_ = nullptr;
        }

        VE_LOG_INFO_CATEGORY("Editor", "Editor uninitialized.");
    }

    bool Editor::IsInitialized() const noexcept
    {
        return initialized_.load(std::memory_order_acquire);
    }

} // namespace ve::editor
