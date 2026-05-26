#include "Engine/Runtime/Asset/SceneAssetLoader.h"

#include "Engine/Runtime/Asset/AssetReference.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Resource/BuiltInResources.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <unordered_map>

namespace ve
{
    namespace
    {
        using boost::json::array;
        using boost::json::object;
        using boost::json::value;

        struct SceneLoadResourceCache
        {
            std::unordered_map<std::string, ResourceHandle<MeshResource>> meshes;
            std::unordered_map<std::string, ResourceHandle<MaterialResource>> materials;
        };

        [[nodiscard]] const value* FindMember(const object& jsonObject, const char* name)
        {
            const auto iter = jsonObject.find(name);
            return iter == jsonObject.end() ? nullptr : &iter->value();
        }

        [[nodiscard]] std::string ReadString(const object& jsonObject, const char* name, std::string fallback = {})
        {
            const value* member = FindMember(jsonObject, name);
            return member != nullptr && member->is_string() ? std::string(member->as_string()) : std::move(fallback);
        }

        [[nodiscard]] Result<object> ReadJsonObject(const Path& path)
        {
            Result<std::string> textResult = FileSystem::ReadTextFile(path);
            if (!textResult)
            {
                return Result<object>::Failure(textResult.GetError());
            }

            boost::system::error_code parseError;
            value root = boost::json::parse(textResult.GetValue(), parseError);
            if (parseError || !root.is_object())
            {
                return Result<object>::Failure(
                    Error(ErrorCode::InvalidArgument, "Scene asset root must be a JSON object."));
            }

            return Result<object>::Success(std::move(root.as_object()));
        }

        [[nodiscard]] Result<AssetGuid> ParseGuidValue(const value* jsonValue)
        {
            if (jsonValue == nullptr || !jsonValue->is_string())
            {
                return Result<AssetGuid>::Failure(Error(ErrorCode::InvalidArgument, "Missing asset reference GUID."));
            }

            const boost::json::string& text = jsonValue->as_string();
            return AssetGuid::Parse(std::string_view(text.data(), text.size()));
        }

        [[nodiscard]] AssetReference ReadAssetReference(const value& jsonValue)
        {
            AssetReference reference;

            if (jsonValue.is_object())
            {
                const object& referenceJson = jsonValue.as_object();
                if (Result<AssetGuid> guid = ParseGuidValue(FindMember(referenceJson, "guid")))
                {
                    reference.guid = guid.GetValue();
                }

                reference.path = Path(ReadString(referenceJson, "path"));
            }
            else if (jsonValue.is_string())
            {
                reference.path = Path(std::string(jsonValue.as_string()));
            }

            return reference;
        }

        [[nodiscard]] std::string MakeReferenceKey(const AssetReference& reference)
        {
            return reference.guid.IsValid() ? reference.guid.ToString() : reference.path.GetString();
        }

        [[nodiscard]] Result<Path> ResolveMeshPath(const AssetDatabase& assetDatabase, const AssetReference& reference)
        {
            if (reference.guid.IsValid())
            {
                Result<Path> artifact = assetDatabase.ResolveArtifact(reference.guid, "Mesh");
                if (artifact)
                {
                    return artifact;
                }

                const AssetRecord* record = assetDatabase.FindAsset(reference.guid);
                if (record != nullptr && record->assetType == AssetType::Mesh)
                {
                    return Result<Path>::Success(record->path);
                }
            }

            if (!reference.path.IsEmpty())
            {
                if (reference.path.GetExtension() == ".vemesh")
                {
                    return Result<Path>::Success(reference.path);
                }

                const AssetRecord* record = assetDatabase.FindAssetByPath(reference.path);
                if (record != nullptr)
                {
                    for (const AssetArtifact& artifact : record->artifacts)
                    {
                        if (artifact.type == "Mesh")
                        {
                            return Result<Path>::Success(artifact.path);
                        }
                    }
                }
            }

            return Result<Path>::Failure(Error(ErrorCode::NotFound, "Mesh asset reference could not be resolved."));
        }

        [[nodiscard]] Result<Path> ResolveMaterialPath(const AssetDatabase& assetDatabase,
                                                       const AssetReference& reference)
        {
            if (reference.guid.IsValid())
            {
                const AssetRecord* record = assetDatabase.FindAsset(reference.guid);
                if (record != nullptr && record->assetType == AssetType::Material)
                {
                    return Result<Path>::Success(record->path);
                }
            }

            if (!reference.path.IsEmpty())
            {
                if (reference.path.GetExtension() == ".vematerial")
                {
                    return Result<Path>::Success(reference.path);
                }

                const AssetRecord* record = assetDatabase.FindAssetByPath(reference.path);
                if (record != nullptr && record->assetType == AssetType::Material)
                {
                    return Result<Path>::Success(record->path);
                }
            }

            return Result<Path>::Failure(Error(ErrorCode::NotFound, "Material asset reference could not be resolved."));
        }

        [[nodiscard]] Result<ResourceHandle<MeshResource>> ResolveMesh(ResourceManager& resourceManager,
                                                                       const AssetDatabase& assetDatabase,
                                                                       SceneLoadResourceCache& cache,
                                                                       const AssetReference& reference)
        {
            const std::string key = MakeReferenceKey(reference);
            if (const auto iter = cache.meshes.find(key); iter != cache.meshes.end())
            {
                return Result<ResourceHandle<MeshResource>>::Success(iter->second);
            }

            if (BuiltInResources::IsBuiltInUri(reference.path.GetString()))
            {
                const ResourceHandle<MeshResource> handle =
                    resourceManager.FindBuiltInMesh(reference.path.GetString());
                if (!handle.IsValid())
                {
                    return Result<ResourceHandle<MeshResource>>::Failure(
                        Error(ErrorCode::NotFound, "Built-in mesh reference could not be resolved."));
                }

                cache.meshes.emplace(key, handle);
                return Result<ResourceHandle<MeshResource>>::Success(handle);
            }

            Result<Path> meshPath = ResolveMeshPath(assetDatabase, reference);
            if (!meshPath)
            {
                VE_LOG_ERROR_CATEGORY("Asset", "Required scene mesh reference could not be resolved.");
                return Result<ResourceHandle<MeshResource>>::Failure(meshPath.GetError());
            }

            Result<ResourceHandle<MeshResource>> loaded =
                resourceManager.LoadMeshFromFile(assetDatabase.ResolveProjectPath(meshPath.GetValue()));
            if (!loaded)
            {
                VE_LOG_ERROR_CATEGORY("Asset",
                                      "Required scene mesh '{}' could not be loaded: {}",
                                      meshPath.GetValue().GetString(),
                                      loaded.GetError().GetMessage());
                return Result<ResourceHandle<MeshResource>>::Failure(loaded.GetError());
            }

            cache.meshes.emplace(key, loaded.GetValue());
            return loaded;
        }

        [[nodiscard]] ResourceHandle<MaterialResource> ResolveMaterial(ResourceManager& resourceManager,
                                                                       const AssetDatabase& assetDatabase,
                                                                       SceneLoadResourceCache& cache,
                                                                       const AssetReference& reference)
        {
            const std::string key = MakeReferenceKey(reference);
            if (const auto iter = cache.materials.find(key); iter != cache.materials.end())
            {
                return iter->second;
            }

            if (BuiltInResources::IsBuiltInUri(reference.path.GetString()))
            {
                const ResourceHandle<MaterialResource> builtInHandle =
                    resourceManager.FindBuiltInMaterial(reference.path.GetString());
                if (builtInHandle.IsValid())
                {
                    cache.materials.emplace(key, builtInHandle);
                    return builtInHandle;
                }

                VE_LOG_WARN_CATEGORY("Asset", "Using fallback material for unresolved built-in material reference.");
            }

            ResourceHandle<MaterialResource> handle = resourceManager.GetDefaultMaterial();
            Result<Path> materialPath = ResolveMaterialPath(assetDatabase, reference);
            if (materialPath)
            {
                Result<ResourceHandle<MaterialResource>> loaded =
                    resourceManager.LoadMaterialFromFile(assetDatabase.ResolveProjectPath(materialPath.GetValue()));
                if (loaded)
                {
                    handle = loaded.GetValue();
                }
                else
                {
                    VE_LOG_WARN_CATEGORY("Asset",
                                         "Failed to load material '{}': {}",
                                         materialPath.GetValue().GetString(),
                                         loaded.GetError().GetMessage());
                }
            }
            else
            {
                VE_LOG_WARN_CATEGORY("Asset", "Using fallback material for unresolved scene material reference.");
            }

            cache.materials.emplace(key, handle);
            return handle;
        }

        [[nodiscard]] ErrorCode ResolveMeshRendererProperties(object& properties,
                                                              ResourceManager& resourceManager,
                                                              const AssetDatabase& assetDatabase,
                                                              SceneLoadResourceCache& cache)
        {
            if (auto meshIter = properties.find("mesh"); meshIter != properties.end() && meshIter->value().is_object())
            {
                const AssetReference reference = ReadAssetReference(meshIter->value());
                Result<ResourceHandle<MeshResource>> handle =
                    ResolveMesh(resourceManager, assetDatabase, cache, reference);
                if (!handle)
                {
                    return handle.GetError().GetCode();
                }

                meshIter->value() = handle.GetValue().GetId();
            }

            if (auto materialIter = properties.find("material");
                materialIter != properties.end() && materialIter->value().is_object())
            {
                const AssetReference reference = ReadAssetReference(materialIter->value());
                const ResourceHandle<MaterialResource> handle =
                    ResolveMaterial(resourceManager, assetDatabase, cache, reference);
                materialIter->value() = handle.GetId();
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ResolveSceneResourceReferences(object& sceneRoot,
                                                               ResourceManager& resourceManager,
                                                               const AssetDatabase& assetDatabase)
        {
            value* gameObjectsValue = sceneRoot.if_contains("gameObjects");
            if (gameObjectsValue == nullptr || !gameObjectsValue->is_array())
            {
                return ErrorCode::None;
            }

            SceneLoadResourceCache cache;
            for (value& gameObjectValue : gameObjectsValue->as_array())
            {
                if (!gameObjectValue.is_object())
                {
                    continue;
                }

                object& gameObjectJson = gameObjectValue.as_object();
                value* componentsValue = gameObjectJson.if_contains("components");
                if (componentsValue == nullptr || !componentsValue->is_array())
                {
                    continue;
                }

                for (value& componentValue : componentsValue->as_array())
                {
                    if (!componentValue.is_object())
                    {
                        continue;
                    }

                    object& componentJson = componentValue.as_object();
                    if (ReadString(componentJson, "type") != "MeshRendererComponent")
                    {
                        continue;
                    }

                    value* propertiesValue = componentJson.if_contains("properties");
                    if (propertiesValue != nullptr && propertiesValue->is_object())
                    {
                        const ErrorCode result = ResolveMeshRendererProperties(
                            propertiesValue->as_object(), resourceManager, assetDatabase, cache);
                        if (result != ErrorCode::None)
                        {
                            return result;
                        }
                    }
                }
            }

            return ErrorCode::None;
        }
    } // namespace

    ErrorCode LoadSceneAsset(Scene& scene,
                             const ReflectionRegistry& reflectionRegistry,
                             ResourceManager& resourceManager,
                             const AssetDatabase& assetDatabase,
                             const Path& scenePath)
    {
        Result<object> rootResult = ReadJsonObject(scenePath);
        if (!rootResult)
        {
            return rootResult.GetError().GetCode();
        }

        const object& root = rootResult.GetValue();
        if (ReadString(root, "format") != "VEngine.Scene")
        {
            return ErrorCode::InvalidArgument;
        }

        const value* versionValue = FindMember(root, "version");
        if (versionValue == nullptr || !versionValue->is_int64() || versionValue->as_int64() != 1)
        {
            return ErrorCode::Unsupported;
        }

        const value* sceneValue = FindMember(root, "scene");
        if (sceneValue == nullptr || !sceneValue->is_object())
        {
            return ErrorCode::InvalidArgument;
        }

        object sceneRoot = sceneValue->as_object();
        const ErrorCode resourceReferenceResult =
            ResolveSceneResourceReferences(sceneRoot, resourceManager, assetDatabase);
        if (resourceReferenceResult != ErrorCode::None)
        {
            return resourceReferenceResult;
        }

        object runtimeRoot;
        runtimeRoot["version"] = 1;
        if (value* gameObjectsValue = sceneRoot.if_contains("gameObjects"); gameObjectsValue != nullptr)
        {
            runtimeRoot["gameObjects"] = *gameObjectsValue;
        }

        return DeserializeSceneFromJson(scene, reflectionRegistry, boost::json::serialize(runtimeRoot));
    }
} // namespace ve
