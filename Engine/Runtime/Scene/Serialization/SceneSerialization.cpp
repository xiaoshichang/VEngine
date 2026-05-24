#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"

#include "Engine/Runtime/Logging/Log.h"

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

        void SerializeGameObject(const GameObject& gameObject,
                                 const ReflectionRegistry& reflectionRegistry,
                                 array& gameObjects)
        {
            object gameObjectJson;
            gameObjectJson["id"] = gameObject.GetId();
            gameObjectJson["name"] = gameObject.GetName();
            gameObjectJson["active"] = gameObject.IsActiveSelf();
            gameObjectJson["parent"] =
                gameObject.GetParent() != nullptr ? gameObject.GetParent()->GetId() : InvalidSceneObjectId;

            array componentsJson;
            for (const std::unique_ptr<Component>& component : gameObject.GetComponents())
            {
                const ReflectedTypeInfo* typeInfo = nullptr;
                for (const ReflectedTypeInfo& reflectedType : reflectionRegistry.GetTypes())
                {
                    std::unique_ptr<Component> probe =
                        reflectedType.componentFactory ? reflectedType.componentFactory() : nullptr;
                    if (probe != nullptr && typeid(*probe) == typeid(*component))
                    {
                        typeInfo = &reflectedType;
                        break;
                    }
                }

                if (typeInfo == nullptr)
                {
                    VE_LOG_WARN_CATEGORY("Scene", "Skipping unregistered component during scene serialization.");
                    continue;
                }

                object componentJson;
                componentJson["type"] = typeInfo->name;

                object propertiesJson;
                for (const ReflectedPropertyInfo& property : typeInfo->properties)
                {
                    if (property.serializable && property.serialize)
                    {
                        propertiesJson[property.name] = property.serialize(*component);
                    }
                }

                componentJson["properties"] = std::move(propertiesJson);
                componentsJson.push_back(std::move(componentJson));
            }

            gameObjectJson["components"] = std::move(componentsJson);
            gameObjects.push_back(std::move(gameObjectJson));

            for (GameObject* child : gameObject.GetChildren())
            {
                SerializeGameObject(*child, reflectionRegistry, gameObjects);
            }
        }

        [[nodiscard]] const value* FindObjectMember(const object& objectValue, const char* name)
        {
            const auto iter = objectValue.find(name);
            return iter == objectValue.end() ? nullptr : &iter->value();
        }

        [[nodiscard]] SceneObjectId ReadSceneObjectId(const value* jsonValue,
                                                      SceneObjectId fallback = InvalidSceneObjectId) noexcept
        {
            if (jsonValue == nullptr)
            {
                return fallback;
            }

            if (jsonValue->is_uint64())
            {
                return jsonValue->as_uint64();
            }

            if (jsonValue->is_int64())
            {
                const auto intValue = jsonValue->as_int64();
                return intValue >= 0 ? static_cast<SceneObjectId>(intValue) : fallback;
            }

            if (jsonValue->is_double())
            {
                const double doubleValue = jsonValue->as_double();
                return doubleValue >= 0.0 ? static_cast<SceneObjectId>(doubleValue) : fallback;
            }

            return fallback;
        }
    } // namespace

    std::string SerializeSceneToJson(const Scene& scene, const ReflectionRegistry& reflectionRegistry)
    {
        object root;
        root["version"] = 1;

        object sceneJson;
        sceneJson["name"] = "Scene";
        root["scene"] = std::move(sceneJson);

        array gameObjects;
        for (GameObject* rootGameObject : scene.GetRootGameObjects())
        {
            SerializeGameObject(*rootGameObject, reflectionRegistry, gameObjects);
        }

        root["gameObjects"] = std::move(gameObjects);
        return boost::json::serialize(root);
    }

    ErrorCode
    DeserializeSceneFromJson(Scene& scene, const ReflectionRegistry& reflectionRegistry, std::string_view jsonText)
    {
        boost::system::error_code parseError;
        value rootValue = boost::json::parse(jsonText, parseError);
        if (parseError || !rootValue.is_object())
        {
            return ErrorCode::InvalidArgument;
        }

        const object& root = rootValue.as_object();
        const value* gameObjectsValue = FindObjectMember(root, "gameObjects");
        if (gameObjectsValue == nullptr || !gameObjectsValue->is_array())
        {
            return ErrorCode::InvalidArgument;
        }

        scene.Clear();

        std::unordered_map<SceneObjectId, SceneObjectId> parentByObject;
        for (const value& gameObjectValue : gameObjectsValue->as_array())
        {
            if (!gameObjectValue.is_object())
            {
                continue;
            }

            const object& gameObjectJson = gameObjectValue.as_object();
            const SceneObjectId id = ReadSceneObjectId(FindObjectMember(gameObjectJson, "id"));
            if (id == InvalidSceneObjectId)
            {
                continue;
            }

            std::string name;
            if (const value* nameValue = FindObjectMember(gameObjectJson, "name");
                nameValue != nullptr && nameValue->is_string())
            {
                name = nameValue->as_string().c_str();
            }

            GameObject& gameObject = scene.CreateGameObjectWithId(id, std::move(name));
            if (const value* activeValue = FindObjectMember(gameObjectJson, "active");
                activeValue != nullptr && activeValue->is_bool())
            {
                gameObject.SetActive(activeValue->as_bool());
            }

            if (const value* parentValue = FindObjectMember(gameObjectJson, "parent"); parentValue != nullptr)
            {
                parentByObject.emplace(id, ReadSceneObjectId(parentValue));
            }

            const value* componentsValue = FindObjectMember(gameObjectJson, "components");
            if (componentsValue == nullptr || !componentsValue->is_array())
            {
                continue;
            }

            for (const value& componentValue : componentsValue->as_array())
            {
                if (!componentValue.is_object())
                {
                    continue;
                }

                const object& componentJson = componentValue.as_object();
                const value* typeValue = FindObjectMember(componentJson, "type");
                if (typeValue == nullptr || !typeValue->is_string())
                {
                    continue;
                }

                const std::string typeName = typeValue->as_string().c_str();
                const ReflectedTypeInfo* typeInfo = reflectionRegistry.FindType(typeName);
                std::unique_ptr<Component> component = reflectionRegistry.CreateComponent(typeName);
                if (typeInfo == nullptr || component == nullptr)
                {
                    VE_LOG_WARN_CATEGORY("Scene", "Skipping unknown component type '{}'.", typeName);
                    continue;
                }

                Component& componentRef = gameObject.AddComponent(std::move(component));
                const value* propertiesValue = FindObjectMember(componentJson, "properties");
                if (propertiesValue == nullptr || !propertiesValue->is_object())
                {
                    continue;
                }

                const object& properties = propertiesValue->as_object();
                for (const auto& propertyMember : properties)
                {
                    const std::string propertyName = propertyMember.key_c_str();
                    const auto propertyIter = std::find_if(typeInfo->properties.begin(),
                                                           typeInfo->properties.end(),
                                                           [&propertyName](const ReflectedPropertyInfo& property)
                                                           { return property.name == propertyName; });
                    if (propertyIter == typeInfo->properties.end() || !propertyIter->deserialize)
                    {
                        VE_LOG_WARN_CATEGORY("Scene", "Skipping unknown property '{}.{}'.", typeName, propertyName);
                        continue;
                    }

                    propertyIter->deserialize(componentRef, propertyMember.value());
                }
            }
        }

        for (const auto& [objectId, parentId] : parentByObject)
        {
            if (parentId == InvalidSceneObjectId)
            {
                continue;
            }

            GameObject* gameObject = scene.FindGameObject(objectId);
            GameObject* parent = scene.FindGameObject(parentId);
            if (gameObject != nullptr && parent != nullptr)
            {
                gameObject->SetParent(parent);
            }
        }

        scene.UpdateTransforms();
        return ErrorCode::None;
    }
} // namespace ve
