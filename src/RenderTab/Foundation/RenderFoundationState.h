#pragma once

#include "RenderTab/Contracts/AccumulationManager.h"
#include "RenderFoundationTypes.h"

#include <string>
#include <vector>

namespace RenderFoundation {

class State {
public:
    State();

    void ResetToDefaultScene();
    Snapshot BuildSnapshot() const;
    void ApplyLoadedState(
        Snapshot snapshot,
        Settings settings,
        const std::string& projectName,
        const std::string& projectFileName);

    const SceneMetadata& GetSceneMetadata() const { return m_Metadata; }
    SceneMetadata& GetSceneMetadata() { return m_Metadata; }

    const Settings& GetSettings() const { return m_Settings; }
    Settings& GetSettings() { return m_Settings; }

    const Camera& GetCamera() const { return m_Camera; }
    Camera& GetCamera() { return m_Camera; }

    const Selection& GetSelection() const { return m_Selection; }
    void SelectNone();
    void SelectPrimitive(Id id);
    void SelectLight(Id id);
    void SelectCamera();

    const std::vector<Material>& GetMaterials() const { return m_Materials; }
    std::vector<Material>& GetMaterials() { return m_Materials; }

    const std::vector<Primitive>& GetPrimitives() const { return m_Primitives; }
    std::vector<Primitive>& GetPrimitives() { return m_Primitives; }

    const std::vector<Light>& GetLights() const { return m_Lights; }
    std::vector<Light>& GetLights() { return m_Lights; }

    Material* FindMaterial(Id id);
    const Material* FindMaterial(Id id) const;
    Primitive* FindPrimitive(Id id);
    const Primitive* FindPrimitive(Id id) const;
    Light* FindLight(Id id);
    const Light* FindLight(Id id) const;

    Id AddPrimitive(PrimitiveType type);
    Id AddLight(LightType type);
    bool DeleteSelection();

    void MarkTransportDirty(
        const std::string& reason,
        RenderContracts::ResetClass resetClass = RenderContracts::ResetClass::FullAccumulation,
        RenderContracts::DirtyFlags dirtyFlags = RenderContracts::DirtyFlags::SceneContent | RenderContracts::DirtyFlags::Viewport);
    void MarkDisplayDirty(const std::string& reason);
    void ApplyExternalChange(const RenderContracts::SceneChangeSet& changeSet);
    void TickFrame();
    RenderContracts::AccumulationManager& AccessAccumulationManager() { return m_AccumulationManager; }
    const RenderContracts::AccumulationManager& AccessAccumulationManager() const { return m_AccumulationManager; }

    int GetAccumulatedSamples() const { return m_AccumulationManager.GetAccumulatedSamples(); }
    std::uint64_t GetTransportEpoch() const { return m_AccumulationManager.GetTransportEpoch(); }
    std::uint64_t GetDisplayEpoch() const { return m_AccumulationManager.GetDisplayEpoch(); }
    const std::string& GetLastChangeReason() const { return m_LastChangeReason; }

    bool HasUnsavedChanges() const { return m_HasUnsavedChanges; }
    void MarkSaved(const std::string& projectName, const std::string& projectFileName);
    const std::string& GetProjectName() const { return m_ProjectName; }
    const std::string& GetProjectFileName() const { return m_ProjectFileName; }

private:
    Id AllocateId();
    Id AddMaterial(Material material);
    void SeedDefaultScene();
    void RefreshNextId();
    Id ResolveMaterialId(Id requestedId) const;
    void ApplyChangeSet(const RenderContracts::SceneChangeSet& changeSet);

private:
    Id m_NextId = 1;
    SceneMetadata m_Metadata {};
    Settings m_Settings {};
    Camera m_Camera {};
    Selection m_Selection {};
    std::vector<Material> m_Materials;
    std::vector<Primitive> m_Primitives;
    std::vector<Light> m_Lights;
    RenderContracts::AccumulationManager m_AccumulationManager {};
    bool m_HasUnsavedChanges = false;
    std::string m_LastChangeReason = "Scene initialized.";
    std::string m_ProjectName;
    std::string m_ProjectFileName;
};

} // namespace RenderFoundation
