#pragma once

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Application/Application.h"

namespace ve::editor
{
    class MacEditorApplication : public ve::Application
    {
    public:
        explicit MacEditorApplication(ve::ApplicationInitParam initParam);
        ~MacEditorApplication() override;

        [[nodiscard]] int Init() override;
        void Run() override;
        void UnInit() override;

    private:
        Editor editor_;
    };
} // namespace ve::editor
