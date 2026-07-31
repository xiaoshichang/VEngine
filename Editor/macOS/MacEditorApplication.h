#pragma once

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorStartup.h"
#include "Engine/Runtime/Application/Application.h"

namespace ve::editor
{
    class MacEditorApplication : public ve::Application
    {
    public:
        MacEditorApplication(ve::ApplicationInitParam initParam, EditorStartupOptions startupOptions);
        ~MacEditorApplication() override;

        [[nodiscard]] int Init() override;
        void Run() override;
        void UnInit() override;

    private:
        Editor editor_;
        EditorStartupOptions startupOptions_;
    };
} // namespace ve::editor
