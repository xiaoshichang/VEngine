#include "Engine/Runtime/Resource/ResourceObject.h"

#include <utility>

namespace ve
{
    ResourceObject::ResourceObject(Guid guid, ResourceType type, Path runtimePath)
        : guid_(std::move(guid))
        , type_(type)
        , runtimePath_(std::move(runtimePath))
    {
    }

    ResourceObject::~ResourceObject() = default;

    const Guid& ResourceObject::GetGuid() const noexcept
    {
        return guid_;
    }

    ResourceType ResourceObject::GetType() const noexcept
    {
        return type_;
    }

    const Path& ResourceObject::GetRuntimePath() const noexcept
    {
        return runtimePath_;
    }

    MeshResource::MeshResource(Guid guid, Path runtimePath, std::string text)
        : ResourceObject(std::move(guid), ResourceType::Mesh, std::move(runtimePath))
        , text_(std::move(text))
    {
    }

    const std::string& MeshResource::GetText() const noexcept
    {
        return text_;
    }

    MaterialResource::MaterialResource(Guid guid, Path runtimePath, std::string text)
        : ResourceObject(std::move(guid), ResourceType::Material, std::move(runtimePath))
        , text_(std::move(text))
    {
    }

    const std::string& MaterialResource::GetText() const noexcept
    {
        return text_;
    }

    SceneResource::SceneResource(Guid guid, Path runtimePath, std::string text)
        : ResourceObject(std::move(guid), ResourceType::Scene, std::move(runtimePath))
        , text_(std::move(text))
    {
    }

    const std::string& SceneResource::GetText() const noexcept
    {
        return text_;
    }

    TextResource::TextResource(Guid guid, Path runtimePath, std::string text)
        : ResourceObject(std::move(guid), ResourceType::Text, std::move(runtimePath))
        , text_(std::move(text))
    {
    }

    const std::string& TextResource::GetText() const noexcept
    {
        return text_;
    }

    BinaryResource::BinaryResource(Guid guid, Path runtimePath, std::vector<std::byte> bytes)
        : ResourceObject(std::move(guid), ResourceType::Binary, std::move(runtimePath))
        , bytes_(std::move(bytes))
    {
    }

    const std::vector<std::byte>& BinaryResource::GetBytes() const noexcept
    {
        return bytes_;
    }
} // namespace ve
