#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Resource/MaterialProperty.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <cstddef>
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

        ve::MeshResource mesh(MakeRecord(ve::ResourceType::Mesh, "11111111-1111-1111-1111-111111111111", "Meshes/Triangle.vemesh"), MeshText);

        std::shared_ptr<ve::RTMeshResource> rtMesh = mesh.GetRTMeshResource();
        bool passed = true;
        passed &= Expect(rtMesh != nullptr, "MeshResource should create an RT proxy");
        passed &= Expect(rtMesh->GetDesc().vertices.size() == 3, "MeshResource should parse vertices for RT upload");
        passed &= Expect(rtMesh->GetDesc().indices.size() == 3, "MeshResource should parse indices for RT upload");
        passed &= Expect(rtMesh->GetVertexStride() == sizeof(ve::RTMeshVertex), "MeshResource should expose vertex stride");
        passed &= Expect(rtMesh->GetDesc().vertices[1].position[0] == 1.0f, "MeshResource should preserve vertex data");
        passed &= Expect(rtMesh->GetDesc().vertices[2].normal[2] == 1.0f, "MeshResource should preserve normal data");
        passed &= Expect(rtMesh->GetDesc().indices[2] == 2, "MeshResource should preserve index data");
        return passed;
    }

    bool TestMaterialConstantDataBuildsRenderDesc()
    {
        ve::ShaderMaterialLayout layout;
        layout.constantBufferSize = 256;

        ve::ShaderMaterialPropertyDesc property;
        property.name = "baseColor";
        property.type = ve::MaterialPropertyType::Color;
        property.binding.kind = ve::MaterialPropertyBindingKind::ConstantBuffer;
        property.binding.offset = 0;
        property.binding.size = 16;
        layout.properties.push_back(std::move(property));

        ve::MaterialPropertyValue value;
        value.type = ve::MaterialPropertyType::Color;
        value.vectorValue = ve::Vector4(0.25f, 0.5f, 0.75f, 1.0f);

        ve::RTMaterialResourceDesc desc;
        desc.name = "Materials/Tinted.vematerial";
        desc.constantData = ve::BuildMaterialConstantData(layout, {value});
        desc.revision = 7;

        std::shared_ptr<ve::RTMaterialResource> rtMaterial = std::make_shared<ve::RTMaterialResource>(desc);
        bool passed = true;
        passed &= Expect(rtMaterial != nullptr, "MaterialResource should create an RT proxy");
        passed &= Expect(rtMaterial->GetDesc().constantData.size() == 256, "Material constant data should use the shader layout size");
        const auto* color = reinterpret_cast<const float*>(rtMaterial->GetDesc().constantData.data());
        passed &= Expect(color[0] == 0.25f && color[1] == 0.5f && color[2] == 0.75f && color[3] == 1.0f,
                         "Material constant data should pack baseColor at the reflected offset");
        passed &= Expect(rtMaterial->GetRevision() == 7, "Material render desc should preserve the submitted revision");
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestMeshResourceBuildsRenderDesc();
    passed &= TestMaterialConstantDataBuildsRenderDesc();

    if (passed)
    {
        std::cout << "VEngineResourceRenderTests passed" << '\n';
        return 0;
    }

    return 1;
}
