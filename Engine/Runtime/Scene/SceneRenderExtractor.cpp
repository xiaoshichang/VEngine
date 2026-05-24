#include "Engine/Runtime/Scene/SceneRenderExtractor.h"

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

namespace ve
{
    namespace
    {
        [[nodiscard]] Vector3 MultiplyColor(const Vector3& color, Float32 scalar)
        {
            return Vector3(color.GetX() * scalar, color.GetY() * scalar, color.GetZ() * scalar);
        }

        [[nodiscard]] Vector3 ModulateColor(const Vector3& left, const Vector3& right)
        {
            return Vector3(left.GetX() * right.GetX(), left.GetY() * right.GetY(), left.GetZ() * right.GetZ());
        }

        [[nodiscard]] Vector3 TransformToClip(const Matrix44& viewProjection, const Vector3& worldPosition)
        {
            return viewProjection.TransformPoint(worldPosition);
        }

        [[nodiscard]] Vector3 ShadeVertex(const MeshVertex& vertex,
                                          const Matrix44& worldMatrix,
                                          const MaterialResource& material,
                                          const std::vector<SceneRenderDirectionalLight>& lights)
        {
            Vector3 normal = worldMatrix.TransformDirection(vertex.normal).Normalized();
            if (normal == Vector3::Zero())
            {
                normal = Vector3::UnitY();
            }

            const Vector3 surfaceColor = ModulateColor(material.baseColor, vertex.color);
            Vector3 litColor = MultiplyColor(surfaceColor, 0.2f);
            for (const SceneRenderDirectionalLight& light : lights)
            {
                const Float32 ndotl = std::max(0.0f, Vector3::Dot(normal, -light.direction.Normalized()));
                litColor += ModulateColor(surfaceColor, light.color) * (ndotl * light.intensity);
            }

            return Vector3(Clamp(litColor.GetX(), 0.0f, 1.0f),
                           Clamp(litColor.GetY(), 0.0f, 1.0f),
                           Clamp(litColor.GetZ(), 0.0f, 1.0f));
        }

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
                        drawItem.vertices.reserve(mesh->vertices.size());

                        const Matrix44 viewProjection = snapshot.hasMainCamera ? snapshot.mainCamera.projectionMatrix *
                                                                                     snapshot.mainCamera.viewMatrix
                                                                               : Matrix44::Identity();

                        for (const MeshVertex& vertex : mesh->vertices)
                        {
                            const Vector3 worldPosition = drawItem.worldMatrix.TransformPoint(vertex.position);
                            drawItem.vertices.push_back(SceneRenderVertex{
                                TransformToClip(viewProjection, worldPosition),
                                ShadeVertex(vertex, drawItem.worldMatrix, *material, snapshot.directionalLights)});
                        }

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
