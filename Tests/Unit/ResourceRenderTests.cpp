#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <iostream>
#include <memory>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    [[nodiscard]] ve::AssetRecord MakeRecord(ve::ResourceType type, const char* guid, const char* runtimePath)
    {
        ve::AssetRecord record;
        record.id = ve::AssetID(ve::Guid::Parse(guid).GetValue());
        record.type = type;
        record.runtimePath = ve::Path(runtimePath);
        return record;
    }

    bool TestMeshResourceBuildsRenderDesc()
    {
        constexpr const char* MeshText = R"({
            "version": 1,
            "type": "Mesh",
            "vertices": [
                [0.0, 0.0, 0.0],
                [1.0, 0.0, 0.0],
                [0.0, 1.0, 0.0]
            ],
            "normals": [
                [0.0, 0.0, 1.0],
                [0.0, 0.0, 1.0],
                [0.0, 0.0, 1.0]
            ],
            "indices": [0, 1, 2]
        })";

        ve::MeshResource mesh(
            MakeRecord(ve::ResourceType::Mesh, "11111111-1111-1111-1111-111111111111", "Meshes/Triangle.vemesh"),
            MeshText);

        std::shared_ptr<ve::RTMeshResource> rtMesh = mesh.GetRTMeshResource();
        bool passed = true;
        passed &= Expect(rtMesh != nullptr, "MeshResource should create an RT proxy");
        passed &= Expect(rtMesh->GetDesc().vertices.size() == 3, "MeshResource should parse vertices for RT upload");
        passed &= Expect(rtMesh->GetDesc().indices.size() == 3, "MeshResource should parse indices for RT upload");
        passed &=
            Expect(rtMesh->GetVertexStride() == sizeof(ve::RTMeshVertex), "MeshResource should expose vertex stride");
        passed &= Expect(rtMesh->GetDesc().vertices[1].position[0] == 1.0f, "MeshResource should preserve vertex data");
        passed &= Expect(rtMesh->GetDesc().vertices[2].normal[2] == 1.0f, "MeshResource should preserve normal data");
        passed &= Expect(rtMesh->GetDesc().indices[2] == 2, "MeshResource should preserve index data");
        return passed;
    }

    bool TestMaterialResourceBuildsRenderDesc()
    {
        constexpr const char* MaterialText = R"({
            "schemaVersion": 1,
            "name": "Tinted",
            "shader": "Engine/Shaders/BasicMesh",
            "parameters": {
                "baseColor": [0.25, 0.5, 0.75, 1.0]
            }
        })";

        ve::MaterialResource material(
            MakeRecord(ve::ResourceType::Material,
                       "22222222-2222-2222-2222-222222222222",
                       "Materials/Tinted.vematerial"),
            MaterialText);

        std::shared_ptr<ve::RTMaterialResource> rtMaterial = material.GetRTMaterialResource();
        bool passed = true;
        passed &= Expect(rtMaterial != nullptr, "MaterialResource should create an RT proxy");
        passed &= Expect(rtMaterial->GetDesc().baseColor.IsNearlyEqual(ve::Vector4(0.25f, 0.5f, 0.75f, 1.0f)),
                         "MaterialResource should parse baseColor for RT upload");
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestMeshResourceBuildsRenderDesc();
    passed &= TestMaterialResourceBuildsRenderDesc();

    if (passed)
    {
        std::cout << "VEngineResourceRenderTests passed" << '\n';
        return 0;
    }

    return 1;
}
