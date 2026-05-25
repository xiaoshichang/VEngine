#include "Engine/Runtime/Scene/SceneRenderExtractor.h"

#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

namespace ve
{
    namespace
    {
        void ExtractGameObject(SceneRenderSnapshot& snapshot,
                               GameObject& gameObject,
                               const ResourceManager& resourceManager,
                               Float32 aspectRatio)
        {
            if (!gameObject.IsActiveInHierarchy())
            {
                return;
            }

            TransformComponent* transform = gameObject.GetComponent<TransformComponent>();

            if (!snapshot.hasMainCamera)
            {
                if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>())
                {
                    if (camera->IsActiveAndEnabled())
                    {
                        snapshot.hasMainCamera = true;
                        snapshot.mainCamera.objectId = gameObject.GetId();
                        snapshot.mainCamera.viewMatrix = camera->GetViewMatrix();
                        snapshot.mainCamera.projectionMatrix = camera->GetProjectionMatrix(aspectRatio);
                        snapshot.mainCamera.viewportRect = camera->GetViewportRect();
                        snapshot.mainCamera.clearColor = camera->GetClearColor();
                    }
                }
            }

            if (LightComponent* light = gameObject.GetComponent<LightComponent>())
            {
                if (light->IsActiveAndEnabled() && light->GetLightType() == LightType::Directional)
                {
                    snapshot.directionalLights.push_back(SceneRenderDirectionalLight{
                        gameObject.GetId(), light->GetDirection(), light->GetColor(), light->GetIntensity()});
                }
            }

            if (MeshRendererComponent* renderer = gameObject.GetComponent<MeshRendererComponent>())
            {
                if (renderer->IsActiveAndEnabled() && renderer->IsVisible() && transform != nullptr)
                {
                    const MeshResource* mesh = resourceManager.FindMesh(renderer->GetMesh());
                    const MaterialResource* material = resourceManager.FindMaterial(renderer->GetMaterial());
                    if (mesh != nullptr && material != nullptr)
                    {
                        SceneRenderDrawItem drawItem;
                        drawItem.objectId = gameObject.GetId();
                        drawItem.mesh = renderer->GetMesh();
                        drawItem.material = renderer->GetMaterial();
                        drawItem.worldMatrix = transform->GetWorldMatrix();
                        drawItem.boundsCenter = renderer->GetLocalBoundsCenter();
                        drawItem.boundsExtents = renderer->GetLocalBoundsExtents();

                        snapshot.drawItems.push_back(std::move(drawItem));
                    }
                }
            }

            for (GameObject* child : gameObject.GetChildren())
            {
                ExtractGameObject(snapshot, *child, resourceManager, aspectRatio);
            }
        }
    } // namespace

    SceneRenderSnapshot ExtractSceneRenderSnapshot(Scene& scene,
                                                   const ResourceManager& resourceManager,
                                                   UInt64 frameId,
                                                   Float32 aspectRatio)
    {
        scene.ValidateMutationAccess();
        scene.UpdateTransforms();

        SceneRenderSnapshot snapshot;
        snapshot.frameId = frameId;

        for (GameObject* root : scene.GetRootGameObjects())
        {
            ExtractGameObject(snapshot, *root, resourceManager, aspectRatio);
        }

        return snapshot;
    }
} // namespace ve
