#pragma once

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetManifest.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ve
{
    class ResourceObject : public NonMovable
    {
    public:
        virtual ~ResourceObject();

        [[nodiscard]] const Guid& GetGuid() const noexcept;
        [[nodiscard]] ResourceType GetType() const noexcept;
        [[nodiscard]] const Path& GetRuntimePath() const noexcept;

    protected:
        ResourceObject(Guid guid, ResourceType type, Path runtimePath);

    private:
        Guid guid_;
        ResourceType type_ = ResourceType::Unknown;
        Path runtimePath_;
    };

    class MeshResource final : public ResourceObject
    {
    public:
        MeshResource(Guid guid, Path runtimePath, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;

    private:
        std::string text_;
    };

    class MaterialResource final : public ResourceObject
    {
    public:
        MaterialResource(Guid guid, Path runtimePath, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;

    private:
        std::string text_;
    };

    class SceneResource final : public ResourceObject
    {
    public:
        SceneResource(Guid guid, Path runtimePath, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;

    private:
        std::string text_;
    };

    class TextResource final : public ResourceObject
    {
    public:
        TextResource(Guid guid, Path runtimePath, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;

    private:
        std::string text_;
    };

    class BinaryResource final : public ResourceObject
    {
    public:
        BinaryResource(Guid guid, Path runtimePath, std::vector<std::byte> bytes);

        [[nodiscard]] const std::vector<std::byte>& GetBytes() const noexcept;

    private:
        std::vector<std::byte> bytes_;
    };

    template<typename TResource>
    struct ResourceObjectTraits;

    template<>
    struct ResourceObjectTraits<MeshResource>
    {
        static constexpr ResourceType Type = ResourceType::Mesh;
        static constexpr const char* Name = "MeshResource";
    };

    template<>
    struct ResourceObjectTraits<MaterialResource>
    {
        static constexpr ResourceType Type = ResourceType::Material;
        static constexpr const char* Name = "MaterialResource";
    };

    template<>
    struct ResourceObjectTraits<SceneResource>
    {
        static constexpr ResourceType Type = ResourceType::Scene;
        static constexpr const char* Name = "SceneResource";
    };

    template<>
    struct ResourceObjectTraits<TextResource>
    {
        static constexpr ResourceType Type = ResourceType::Text;
        static constexpr const char* Name = "TextResource";
    };

    template<>
    struct ResourceObjectTraits<BinaryResource>
    {
        static constexpr ResourceType Type = ResourceType::Binary;
        static constexpr const char* Name = "BinaryResource";
    };
} // namespace ve
