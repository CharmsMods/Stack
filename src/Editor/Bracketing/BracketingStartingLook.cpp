#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "NodeMath/ToneCurveParameters.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Project/RawLayerStack.h"
#include "Raw/Tone/BurstStartingLook.h"

namespace {
using namespace Stack;
nlohmann::json StoredSourceRecipe(const Project::RawProjectSnapshot& snapshot, const std::string& setId) {
    const auto* set = Project::FindSourceSet(snapshot, setId);
    return set ? set->settings.value("sharedPostHdrRecipe", nlohmann::json::object()) : nlohmann::json{};
}
nlohmann::json AuthoredComposition(const EditorNodeGraph::Graph& graph) {
    auto value = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), graph);
    auto& authored = value["nodeGraph"];
    authored.erase("selectedNodeId");
    for (auto& node : authored["nodes"]) {
        // Worker publication updates status, not the authored graph.
        node.erase("hdrStatus");
        node.erase("hdrResultState");
    }
    return value;
}
bool DefaultCreativeStack(const Project::RawLayerStackState& state) {
    const auto& layer = state.background;
    if (!state.layers.empty() || state.explicitRouting || !layer.enabled || layer.opacity != 1 ||
        layer.layerMask || layer.graph.GetNodes().size() != 7 || layer.graph.GetLinks().size() != 5) return false;
    for (const auto& node : layer.graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawOperation) continue;
        const auto expected = RawRecipe::MakeGraphOperation(node.rawOperation.kind);
        if (RawRecipe::SerializeGraphOperation(node.rawOperation) != RawRecipe::SerializeGraphOperation(expected)) return false;
    }
    return Project::FindRawOperation(layer, RawRecipe::GraphOperationKind::LocalEv) &&
        Project::FindRawOperation(layer, RawRecipe::GraphOperationKind::ToneCurve);
}
bool DefaultView(const RawRecipe::RawDevelopmentRecipe& recipe) {
    // The planner is explicitly matched to this display transform. Do not
    // reinterpret custom/disabled/graph-placed presentation as the default.
    const auto current = RawRecipe::SerializeRecipe(recipe);
    const auto defaults = RawRecipe::SerializeRecipe(RawRecipe::MakeDefaultRecipe({}));
    return current.at("viewTransform") == defaults.at("viewTransform");
}
}

void EditorModule::ArmBracketingStartingLook() {
    if (!m_Bracketing || !m_Project->snapshot) return;
    auto& look = m_Bracketing->startingLook;
    look = {};
    const auto& layers = m_Project->rawLayers.State();
    if (!DefaultCreativeStack(layers) || !m_Project->layers.empty()) return;
    look.documentId = GetProjectDocumentId();
    look.localEvUuid = Stack::Project::FindRawOperation(layers.background,
        Stack::RawRecipe::GraphOperationKind::LocalEv)->instanceUuid;
    look.toneUuid = Stack::Project::FindRawOperation(layers.background,
        Stack::RawRecipe::GraphOperationKind::ToneCurve)->instanceUuid;
    look.layerRevision = m_Project->rawLayers.Revision();
    look.layers = Stack::Project::SerializeRawLayerStack(layers);
    look.composition = AuthoredComposition(m_Project->graph);
    look.sourceRecipe = StoredSourceRecipe(*m_Project->snapshot, m_Bracketing->setId);
    look.armed = true;
}

void EditorModule::PrepareBracketingStartingLookAnalysis() {
    if (!m_Bracketing || !m_Bracketing->startingLook.armed) return;
    auto& look = m_Bracketing->startingLook;
    look.armed = false; // The first publication consumes the opportunity, even if edited.
    if (!m_Project->snapshot || !m_HdrAdoptedRawResult ||
        look.documentId != GetProjectDocumentId() ||
        look.layerRevision != m_Project->rawLayers.Revision() ||
        !m_Project->layers.empty() || look.composition != AuthoredComposition(m_Project->graph) ||
        look.layers != Stack::Project::SerializeRawLayerStack(m_Project->rawLayers.State()) ||
        look.sourceRecipe != StoredSourceRecipe(*m_Project->snapshot, m_Bracketing->setId) ||
        !DefaultView(m_Project->rawRecipe) ||
        (m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->dirty)) return;
    const auto* set = Stack::Project::FindSourceSet(*m_Project->snapshot, m_Bracketing->setId);
    if (!set || set->settings.value("viewTransformPlacement", std::string("internal")) != "internal") return;
    look.sourceRecipe = Stack::RawRecipe::SerializeWorkspaceSourceRecipe(m_Project->rawRecipe);
    look.sourceHash = m_HdrAdoptedRawResult->contentHash;
    look.inputRevision = m_Project->snapshot->hdrInputRevision;
    look.editRevision = m_Project->editRevision;
    look.awaitingAnalysis = look.sourceHash != 0;
    if (look.awaitingAnalysis) {
        m_RawWorkspaceAnalysisRequested = true;
        MarkRenderRefreshDirty();
    }
}

void EditorModule::TryApplyBracketingStartingLook(const RenderTextureStats& stats, std::uint64_t sourceHash) {
    if (!m_Bracketing || !m_Bracketing->startingLook.awaitingAnalysis) return;
    auto& look = m_Bracketing->startingLook;
    // A stale readback is not the awaited result. Never use an old frame's
    // statistics to initialize this source, including another project's frame.
    if (!sourceHash || sourceHash != look.sourceHash) return;
    look.awaitingAnalysis = false;
    if (!m_Project->snapshot || look.documentId != GetProjectDocumentId() ||
        m_Bracketing->projectId != m_Project->snapshot->projectId ||
        m_Bracketing->setId != m_Project->snapshot->activeSourceSetId ||
        look.inputRevision != m_Project->snapshot->hdrInputRevision ||
        look.editRevision != m_Project->editRevision ||
        look.layerRevision != m_Project->rawLayers.Revision() ||
        !m_Project->layers.empty() || look.composition != AuthoredComposition(m_Project->graph) ||
        m_Bracketing->pending || m_Bracketing->job || m_Bracketing->processRequired ||
        GetActiveRawWorkspacePreviewSourceHash() != sourceHash ||
        m_Project->rawLayers.GestureActive() ||
        (m_RawLayerMaskWorkspace && m_RawLayerMaskWorkspace->dirty) ||
        look.sourceRecipe != Stack::RawRecipe::SerializeWorkspaceSourceRecipe(m_Project->rawRecipe) ||
        look.layers != Stack::Project::SerializeRawLayerStack(m_Project->rawLayers.State()) ||
        !stats.valid || !std::isfinite(stats.validPixelPercent) || stats.validPixelPercent < 99.f) return;
    const auto plan = Stack::RawRecipe::PlanBurstStartingLook(stats.p50Luma, stats.p999Luma);
    if (!plan.valid || (std::abs(plan.exposureEv) < .01f && plan.shoulder == 0)) return;
    auto candidate = m_Project->rawLayers.State();
    auto& background = candidate.background;
    auto* local = Stack::Project::FindRawOperation(background, Stack::RawRecipe::GraphOperationKind::LocalEv, look.localEvUuid);
    auto* tone = Stack::Project::FindRawOperation(background, Stack::RawRecipe::GraphOperationKind::ToneCurve, look.toneUuid);
    if (!local || !tone) return;
    auto localRecipe = Stack::RawRecipe::ReadGraphOperation(local->rawOperation);
    auto toneRecipe = Stack::RawRecipe::ReadGraphOperation(tone->rawOperation);
    localRecipe.preToneExposureEv = plan.exposureEv;
    Stack::RawRecipe::SceneTone curve;
    curve.curve.clear();
    // Straight segments preserve monotonicity without overshoot. These are
    // ordinary editable luminance points, not hidden automatic parameters.
    for (const float ev : {-16.f, -8.f, -4.f, 0.f, 1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f})
        curve.curve.push_back({(ev + 16) / 32, (plan.OutputEv(ev) + 16) / 32});
    if (plan.shoulder > 0) {
        toneRecipe.finishTone.layerJson["curveMode"] = "luminance";
        toneRecipe.finishTone.layerJson["luminanceTone"] = Stack::RawRecipe::SerializeSceneTone(curve);
    }
    std::string error;
    if (!Stack::NodeMath::ValidateToneCurveParameters(toneRecipe.finishTone.layerJson, error) ||
        !Stack::Project::WriteRawLayerOperation(background, look.localEvUuid, localRecipe, error) ||
        !Stack::Project::WriteRawLayerOperation(background, look.toneUuid, toneRecipe, error)) return;
    // This is a new undoable action, not completion of an earlier UI gesture.
    if (ApplyRawLayerStackEdit(std::move(candidate), true, false))
        PostNotification(UiNotificationSeverity::Info,
            "Burst starting look applied to Local EV and Tone Curve. Undo restores neutral controls.",
            "bracketing-starting-look");
}
