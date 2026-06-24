#pragma once

#include "Engine/Runtime/FileSystem/Path.h"

namespace ve
{
    class GameObject;
}

namespace ve::editor
{
    enum class EditorSelectionType
    {
        None,
        GameObject,
        Asset,
    };

    struct EditorSelectionChangedEvent
    {
        EditorSelectionType selectionType = EditorSelectionType::None;
        ve::GameObject* gameObject = nullptr;
        Path assetPath;
    };
} // namespace ve::editor
