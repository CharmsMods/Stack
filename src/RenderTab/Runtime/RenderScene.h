#pragma once

#include "RenderTab/Runtime/Bvh/RenderBvh.h"
#include "RenderTab/Runtime/Assets/RenderImportedAsset.h"
#include "RenderTab/Runtime/Geometry/RenderMesh.h"
#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"
#include "RenderTab/Runtime/Materials/RenderMaterial.h"
#include "RenderTab/Runtime/RenderLight.h"
#include "RenderTab/Runtime/RenderSceneTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class RenderScene {
public:
    RenderScene();

    const std::string& GetName() const { return m_Name; }
    RenderBackgroundMode GetBackgroundMode() const { return m_BackgroundMode; }
    RenderValidationSceneId GetValidationSceneId() const { return m_ValidationSceneId; }
    const std::string& GetValidationSceneLabel() const { return m_ValidationSceneLabel; }
    const std::string& GetValidationSceneDescription() const { return m_ValidationSceneDescription; }
    bool IsEnvironmentEnabled() const { return m_EnvironmentEnabled; }
    float GetEnvironmentIntensity() const { return m_EnvironmentIntensity; }
    bool IsFogEnabled() const { return m_FogEnabled; }
    const RenderFloat3& GetFogColor() const { return m_FogColor; }
    float GetFogDensity() const { return m_FogDensity; }
    float GetFogAnisotropy() const { return m_FogAnisotropy; }
    std::uint64_t GetRevision() const { return m_Revision; }
    const std::string& GetLastChangeReason() const { return m_LastChangeReason; }

    bool SetSceneLabel(const std::string& label);
    bool SetBackgroundMode(RenderBackgroundMode mode);
    bool SetValidationScene(RenderValidationSceneId id);
    bool SetEnvironmentEnabled(bool enabled);
    bool SetEnvironmentIntensity(float intensity);
    bool SetFogEnabled(bool enabled);
    bool SetFogColor(const RenderFloat3& color);
    bool SetFogDensity(float density);
    bool SetFogAnisotropy(float anisotropy);
    bool UpdateMaterial(int index, const RenderMaterial& material);
    bool UpdateSphere(int index, const RenderSphere& sphere);
    bool UpdateTriangle(int index, const RenderTriangle& triangle);
    bool UpdateMeshInstance(int index, const RenderMeshInstance& meshInstance);
    bool UpdateLight(int index, const RenderLight& light);
    int AddMaterial(const RenderMaterial& material);
    bool DuplicateMaterial(int materialIndex, int& newMaterialIndex);
    bool DeleteMaterial(int materialIndex);
    bool AddSphere(const RenderSphere& sphere, int& newObjectId);
    bool AddMeshInstance(const RenderMeshInstance& meshInstance, int& newObjectId);
    bool AddLight(const RenderLight& light, int& newObjectId);
    bool AddBuiltInCube(int materialIndex, int& newObjectId);
    bool AddBuiltInPlane(int materialIndex, int& newObjectId);
    bool AssignMaterialToMeshInstance(int objectId, int materialIndex);
    void CreateEmptyScene(const std::string& label = "Untitled Render Scene");
    void ApplySceneSnapshot(
        std::string label,
        std::string description,
        RenderBackgroundMode backgroundMode,
        bool environmentEnabled,
        float environmentIntensity,
        bool fogEnabled,
        RenderFloat3 fogColor,
        float fogDensity,
        float fogAnisotropy,
        std::vector<RenderImportedAsset> importedAssets,
        std::vector<RenderImportedTexture> importedTextures,
        std::vector<RenderMaterial> materials,
        std::vector<RenderMeshDefinition> meshes,
        std::vector<RenderMeshInstance> meshInstances,
        std::vector<RenderSphere> spheres,
        std::vector<RenderTriangle> triangles,
        std::vector<RenderLight> lights,
        const std::string& reason);
    bool MergeImportedScene(
        std::string label,
        std::string description,
        std::vector<RenderImportedAsset> importedAssets,
        std::vector<RenderImportedTexture> importedTextures,
        std::vector<RenderMaterial> materials,
        std::vector<RenderMeshDefinition> meshes,
        std::vector<RenderMeshInstance> meshInstances,
        std::vector<RenderSphere> spheres,
        std::vector<RenderTriangle> triangles,
        std::vector<RenderLight> lights,
        const std::string& reason);
    void MarkDirty(const std::string& reason);
    bool UpdateImportedTexturePixels(int index, std::vector<unsigned char> pixels, int width, int height, int channels, const std::string& reason);

    int GetImportedAssetCount() const { return static_cast<int>(m_ImportedAssets.size()); }
    int GetImportedTextureCount() const { return static_cast<int>(m_ImportedTextures.size()); }
    int GetMaterialCount() const { return static_cast<int>(m_Materials.size()); }
    int GetMeshDefinitionCount() const { return static_cast<int>(m_MeshDefinitions.size()); }
    int GetMeshInstanceCount() const { return static_cast<int>(m_MeshInstances.size()); }
    int GetSphereCount() const { return static_cast<int>(m_Spheres.size()); }
    int GetTriangleCount() const { return static_cast<int>(m_Triangles.size()); }
    int GetLightCount() const { return static_cast<int>(m_Lights.size()); }
    int GetResolvedTriangleCount() const { return static_cast<int>(m_ResolvedTriangles.size()); }
    int GetEmissiveMaterialCount() const;
    bool IsImportedAssetIndexValid(int index) const;
    bool IsImportedTextureIndexValid(int index) const;
    bool IsMaterialIndexValid(int index) const;
    bool IsMeshDefinitionIndexValid(int index) const;
    bool IsMeshInstanceIndexValid(int index) const;
    bool IsSphereIndexValid(int index) const;
    bool IsTriangleIndexValid(int index) const;
    bool IsLightIndexValid(int index) const;
    int FindMeshInstanceIndexById(int objectId) const;
    int FindSphereIndexById(int objectId) const;
    int FindTriangleIndexById(int objectId) const;
    int FindLightIndexById(int objectId) const;
    const RenderImportedAsset& GetImportedAsset(int index) const;
    const RenderImportedTexture& GetImportedTexture(int index) const;
    const RenderMaterial& GetMaterial(int index) const;
    const RenderMeshDefinition& GetMeshDefinition(int index) const;
    const RenderMeshInstance& GetMeshInstance(int index) const;
    const RenderSphere& GetSphere(int index) const;
    const RenderTriangle& GetTriangle(int index) const;
    const RenderLight& GetLight(int index) const;
    const RenderResolvedTriangle& GetResolvedTriangle(int index) const;
    bool DuplicateMeshInstance(int objectId, int& newObjectId);
    bool DuplicateSphere(int objectId, int& newObjectId);
    bool DuplicateTriangle(int objectId, int& newObjectId);
    bool DuplicateLight(int objectId, int& newObjectId);
    bool DeleteMeshInstance(int objectId);
    bool DeleteSphere(int objectId);
    bool DeleteTriangle(int objectId);
    bool DeleteLight(int objectId);

    std::size_t GetPrimitiveCount() const { return m_PrimitiveRefs.size(); }
    const std::vector<RenderPrimitiveRef>& GetPrimitiveRefs() const { return m_PrimitiveRefs; }
    const std::vector<RenderBvhNode>& GetBvhNodes() const { return m_BvhNodes; }

private:
    int AllocateObjectId();
    void EnsureObjectIds();
    void LoadValidationScene(RenderValidationSceneId id);
    void RebuildSpatialData();
    void Touch(const std::string& reason);

    std::string m_Name;
    RenderBackgroundMode m_BackgroundMode = RenderBackgroundMode::Gradient;
    RenderValidationSceneId m_ValidationSceneId = RenderValidationSceneId::MixedDebug;
    std::string m_ValidationSceneLabel;
    std::string m_ValidationSceneDescription;
    bool m_EnvironmentEnabled = true;
    float m_EnvironmentIntensity = 1.0f;
    bool m_FogEnabled = false;
    RenderFloat3 m_FogColor { 0.82f, 0.88f, 0.96f };
    float m_FogDensity = 0.0f;
    float m_FogAnisotropy = 0.0f;
    std::vector<RenderImportedAsset> m_ImportedAssets;
    std::vector<RenderImportedTexture> m_ImportedTextures;
    std::vector<RenderMaterial> m_Materials;
    std::vector<RenderMeshDefinition> m_MeshDefinitions;
    std::vector<RenderMeshInstance> m_MeshInstances;
    std::vector<RenderSphere> m_Spheres;
    std::vector<RenderTriangle> m_Triangles;
    std::vector<RenderLight> m_Lights;
    std::vector<RenderResolvedTriangle> m_ResolvedTriangles;
    std::vector<RenderPrimitiveRef> m_PrimitiveRefs;
    std::vector<RenderBvhNode> m_BvhNodes;
    int m_NextObjectId = 1;
    std::uint64_t m_Revision = 0;
    std::string m_LastChangeReason;
};
