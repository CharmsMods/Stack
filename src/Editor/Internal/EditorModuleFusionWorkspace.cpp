#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"
#include "Editor/UI/FusionWorkspaceUi.h"
#include "Raw/MultiFrame/GraphExecution.h"
#include <cmath>

namespace {
using Stack::Project::MultiFrameGraphNode;
using Stack::Project::MultiFrameGraphNodeKind;

void LoadDraft(Stack::Editor::FusionWorkspaceUiState& ui,
    const Stack::Project::RawProjectSnapshot& snapshot, const MultiFrameGraphNode& node) {
    const auto stored = node.settings.value("fusion", nlohmann::json::object()).dump();
    if (ui.nodeId != node.nodeId || ui.projectId != snapshot.projectId ||
        (!ui.dirty && ui.storedControls != stored)) {
        ui.controls = {};
        Raw::Hdr::DeserializeFusionControls(node.settings.value("fusion", nlohmann::json::object()), ui.controls, nullptr);
        ui.storedControls = stored; ui.nodeId = node.nodeId; ui.projectId = snapshot.projectId;
        ui.dirty = false; ui.selectedMask = -1; ui.placeMask = false; ui.imageIdentity.clear();
    }
    ui.sourceLabels.clear();
    for (const auto& n : snapshot.multiFrameGraph.nodes) {
        ui.sourceLabels[n.nodeId] = n.title;
        if (n.frameIds.size() == 1) ui.sourceLabels[n.frameIds.front()] = n.title;
    }
}

std::vector<std::pair<std::string, std::string>> NodeInputs(
    const Stack::Project::RawProjectSnapshot& snapshot, const MultiFrameGraphNode& node) {
    std::vector<std::pair<std::string, std::string>> inputs;
    for (const auto& link : snapshot.multiFrameGraph.links) {
        if (link.toNodeId != node.nodeId) continue;
        const auto* producer = Stack::Project::FindMultiFrameGraphNode(snapshot.multiFrameGraph, link.fromNodeId);
        if (!producer) continue;
        if (producer->kind == MultiFrameGraphNodeKind::CaptureSet || producer->kind == MultiFrameGraphNodeKind::CaptureSubset) {
            for (const auto& frame : producer->frameIds) inputs.emplace_back(frame, producer->title);
        } else inputs.emplace_back(producer->nodeId, producer->title);
    }
    return inputs;
}

bool AnchorControl(const char* label, const std::vector<std::pair<std::string, std::string>>& inputs,
    nlohmann::json& settings, const char* key) {
    std::string selected = settings.value(key, std::string());
    std::string display = "Automatic";
    for (const auto& input : inputs) if (input.first == selected) display = input.second;
    bool changed = false;
    if (ImGui::BeginCombo(label, display.c_str())) {
        if (ImGui::Selectable("Automatic", selected.empty())) { settings.erase(key); changed = true; }
        for (const auto& input : inputs) {
            ImGui::PushID(input.first.c_str());
            if (ImGui::Selectable(input.second.c_str(), selected == input.first)) { settings[key] = input.first; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}
}

void EditorModule::RenderMultiFrameFusionPreview(const ImVec2& size) {
    if (!m_Project->snapshot) return;
    const auto& snapshot = *m_Project->snapshot;
    const auto* node = Stack::Project::FindMultiFrameGraphNode(snapshot.multiFrameGraph, m_MultiFrameWorkspaceSelectedNodeId);
    if (!node || node->kind != MultiFrameGraphNodeKind::HdrMerge) {
        for (const auto& link : snapshot.multiFrameGraph.links) if (link.toNodeId == snapshot.multiFrameGraph.outputNodeId) {
            node = Stack::Project::FindMultiFrameGraphNode(snapshot.multiFrameGraph, link.fromNodeId); break;
        }
    }
    if (!m_FusionWorkspaceUi) m_FusionWorkspaceUi = std::make_shared<Stack::Editor::FusionWorkspaceUiState>();
    const Raw::Hdr::FusionPreview* preview = nullptr;
    if (node && node->kind == MultiFrameGraphNodeKind::HdrMerge) {
        LoadDraft(*m_FusionWorkspaceUi, snapshot, *node);
        if (m_MultiFrameCacheProjectId == snapshot.projectId) {
            const auto hit = m_MultiFrameFusionResults.find(node->nodeId);
            if (hit != m_MultiFrameFusionResults.end()) preview = hit->second->fusionPreview.get();
            if (preview && preview->graphInputIdentity != Raw::MultiFrame::MultiFrameFusionInputIdentity(snapshot, node->nodeId))
                preview = nullptr;
        }
    } else if (node && node->kind == MultiFrameGraphNodeKind::BurstDenoise && m_MfdAdoptedRawResult &&
        m_MfdAdoptedRawResult->projectId == snapshot.projectId &&
        m_MfdAdoptedRawResult->inputRevision == snapshot.mfdInputRevision &&
        m_MfdAdoptedRawResult->rawData && m_MfdAdoptedRawResult->rawData->normalizedMosaicBuffer) {
        auto& ui = *m_FusionWorkspaceUi;
        LoadDraft(ui, snapshot, *node);
        const auto& adopted = *m_MfdAdoptedRawResult;
        const auto& raw = *adopted.rawData;
        if (!ui.burstPreview || ui.burstContentHash != adopted.contentHash) {
            auto p = std::make_shared<Raw::Hdr::FusionPreview>();
            p->contentIdentity = "burst-" + std::to_string(adopted.contentHash);
            p->rawWidth = raw.metadata.visibleWidth; p->rawHeight = raw.metadata.visibleHeight;
            p->stride = static_cast<std::uint32_t>(std::max<std::uint64_t>(1, (std::max(p->rawWidth, p->rawHeight) + 639) / 640));
            p->width = static_cast<std::uint32_t>((p->rawWidth / 2 + p->stride - 1) / p->stride);
            p->height = static_cast<std::uint32_t>((p->rawHeight / 2 + p->stride - 1) / p->stride);
            switch (raw.metadata.cfaPattern) {
            case Raw::CfaPattern::BGGR: p->channelByParity = {2,1,1,0}; break;
            case Raw::CfaPattern::GRBG: p->channelByParity = {1,0,2,1}; break;
            case Raw::CfaPattern::GBRG: p->channelByParity = {1,2,0,1}; break;
            default: break;
            }
            const auto& wb = raw.metadata.cameraWhiteBalance;
            p->whiteBalance = {wb[0] / std::max(1.0e-6f, wb[1]), 1, wb[2] / std::max(1.0e-6f, wb[1])};
            p->sources.push_back({node->nodeId, 0, 1});
            p->observations.resize(static_cast<std::size_t>(p->width) * p->height * 4);
            p->analysisEv.resize(static_cast<std::size_t>(p->width) * p->height);
            for (std::size_t i = 0; i < p->analysisEv.size(); ++i) {
                float mean = 0;
                for (std::size_t parity = 0; parity < 4; ++parity) {
                    const auto x = (i % p->width) * p->stride * 2 + parity % 2;
                    const auto y = (i / p->width) * p->stride * 2 + parity / 2;
                    const auto index = y * p->rawWidth + x;
                    auto& o = p->observations[i * 4 + parity];
                    o.scene = (*raw.normalizedMosaicBuffer)[index]; mean += o.scene / 4;
                    o.automaticWeight = o.headroom = o.motionConfidence = 1;
                    if (raw.multiFrameMeasurementSidecars) {
                        const auto& sidecars = *raw.multiFrameMeasurementSidecars;
                        if (sidecars.variance) o.variance = (*sidecars.variance)[index];
                        if (sidecars.effectiveSupport) o.effectiveSamples = (*sidecars.effectiveSupport)[index];
                        if (sidecars.validity && !(*sidecars.validity)[index]) o.automaticWeight = 0;
                        if (sidecars.clipping && (*sidecars.clipping)[index]) o.automaticWeight = o.headroom = 0;
                    }
                }
                p->analysisEv[i] = std::log2(std::max(1.0e-6f, mean) / 0.18f);
            }
            ui.burstPreview = p; ui.burstContentHash = adopted.contentHash;
        }
        preview = ui.burstPreview.get();
    }
    Stack::UiActivity::BeginDisabledForWork(IsMultiFrameGraphProcessingBusy() || IsHdrProcessingBusy() || IsMfdExperimentalProcessingBusy());
    Stack::Editor::DrawFusionPreview(*m_FusionWorkspaceUi, preview, size);
    ImGui::EndDisabled();
}

void EditorModule::RenderMultiFrameFusionInspector(const MultiFrameGraphNode& node, bool busy,
    std::function<void()>& deferredAction) {
    if (!m_Project->snapshot) return;
    if (!m_FusionWorkspaceUi) m_FusionWorkspaceUi = std::make_shared<Stack::Editor::FusionWorkspaceUiState>();
    auto& ui = *m_FusionWorkspaceUi;
    const auto& snapshot = *m_Project->snapshot;
    LoadDraft(ui, snapshot, node);
    const Raw::Hdr::Result* result = nullptr;
    if (m_MultiFrameCacheProjectId == snapshot.projectId) {
        const auto hit = m_MultiFrameFusionResults.find(node.nodeId);
        if (hit != m_MultiFrameFusionResults.end()) result = hit->second.get();
    }
    const auto* preview = result ? result->fusionPreview.get() : nullptr;
    if (preview && preview->graphInputIdentity != Raw::MultiFrame::MultiFrameFusionInputIdentity(snapshot, node.nodeId))
        preview = nullptr;
    auto settings = node.settings;
    bool changed = false;
    Stack::UiActivity::BeginDisabledForWork(busy);
    if (ImGui::CollapsingHeader("Registration and anchors", ImGuiTreeNodeFlags_DefaultOpen)) {
        Raw::Hdr::Parameters parameters;
        const auto* set = Stack::Project::FindSourceSet(snapshot, snapshot.activeSourceSetId);
        Raw::Hdr::DeserializeParameters(settings.value("parameters", set ? set->settings.value("parameters", nlohmann::json::object()) : nlohmann::json::object()), parameters, nullptr);
        int mode = static_cast<int>(parameters.alignmentMode);
        const char* modes[] = {"Global CFA translation", "Bypass, exact identity", "Verify only"};
        if (ImGui::Combo("Alignment", &mode, modes, 3)) {
            parameters.alignmentMode = static_cast<Raw::Hdr::AlignmentMode>(mode);
            settings["parameters"] = Raw::Hdr::SerializeParameters(parameters); changed = true;
        }
        const auto inputs = NodeInputs(snapshot, node);
        changed |= AnchorControl("Geometry anchor", inputs, settings, "geometryAnchor");
        changed |= AnchorControl("Exposure origin", inputs, settings, "radiometricAnchor");
        ImGui::TextWrapped("Geometry chooses framing and fallback. Exposure origin defines 0 EV. Neither is a contribution slider.");
        ImGui::TextDisabled("Global alignment uses whole CFA cells. Fine RGB reconstruction is not available here yet.");
    }
    Stack::Editor::DrawFusionControls(ui, preview);
    ImGui::EndDisabled();
    if (!preview) ImGui::TextWrapped("Process Output once to inspect capture contributions and edit against the neutral analysis image.");
    if (preview && ImGui::CollapsingHeader("Cursor inspector", ImGuiTreeNodeFlags_DefaultOpen))
        Stack::Editor::DrawFusionProbe(ui, *preview);
    if (result && ImGui::CollapsingHeader("Measurement status")) {
        ImGui::Text("Exposure span %.2f EV", result->diagnostics.exposureSpanEv);
        ImGui::Text("Prepared data %s", result->diagnostics.reusedPreparation ? "reused" : "computed");
        for (const auto& frame : result->diagnostics.frames) {
            ImGui::TextWrapped("%s: %s", ui.sourceLabels[frame.stableFrameId].c_str(), frame.noiseModelQuality.c_str());
            ImGui::TextDisabled("Shift %.3f, %.3f px", frame.translationRawX, frame.translationRawY);
        }
        for (const auto& warning : result->diagnostics.warnings) ImGui::TextWrapped("%s", warning.c_str());
        Stack::UiActivity::BeginDisabledForWork(busy);
        if (ImGui::Button("Rebuild neutral analysis")) {
            const auto setId = snapshot.activeSourceSetId;
            deferredAction = [this, setId]() {
                m_MultiFrameProcessingCache.reset();
                std::string error;
                if (!StartMultiFrameGraphProcessing(setId, &error)) m_MultiFrameWorkspaceStatusText = error;
            };
        }
        ImGui::EndDisabled();
    }
    if (!busy && (changed || (ui.dirty && !ImGui::IsAnyItemActive()))) {
        settings["fusion"] = Raw::Hdr::SerializeFusionControls(ui.controls);
        auto graph = snapshot.multiFrameGraph;
        if (auto* edited = Stack::Project::FindMultiFrameGraphNode(graph, node.nodeId)) edited->settings = settings;
        graph.userEdited = true;
        ui.dirty = false; ui.storedControls = settings["fusion"].dump();
        deferredAction = [this, graph = std::move(graph)]() mutable {
            std::string error;
            if (!SetMultiFrameGraphDocument(std::move(graph), &error)) m_MultiFrameWorkspaceStatusText = error;
        };
    }
}

void EditorModule::RenderMultiFrameBurstInspector(const MultiFrameGraphNode& node, bool busy,
    std::function<void()>& deferredAction) {
    ImGui::TextUnformatted("Burst Denoise");
    ImGui::TextWrapped("Merge repeated captures at one exposure. The result carries variance, validity, and original-frame evidence into HDR Fusion.");
    auto settings = node.settings;
    Raw::Mfd::SharedBurstSettings burst;
    Raw::Mfd::DeserializeSharedBurstSettings(settings.value("sharedBurstSettings", nlohmann::json::object()), burst, nullptr);
    float tolerance = static_cast<float>(burst.exposureGroupToleranceEv);
    float robustness = static_cast<float>(burst.huberThreshold);
    Raw::Mfd::MfdAlignmentMode alignment = Raw::Mfd::MfdAlignmentMode::Identity;
    Raw::Mfd::ParseMfdAlignmentMode(settings.value("alignmentMode", std::string("identity")), alignment);
    int mode = alignment == Raw::Mfd::MfdAlignmentMode::Identity ? 0 :
        (alignment == Raw::Mfd::MfdAlignmentMode::TranslationOnly ? 1 : 2);
    const char* modes[] = {"Bypass, exact identity", "Global translation", "Local alignment"};
    Stack::UiActivity::BeginDisabledForWork(busy);
    bool changed = ImGui::Combo("Alignment", &mode, modes, 3);
    changed |= ImGui::SliderFloat("Exposure tolerance", &tolerance, 0.1f, 0.5f, "%.2f EV");
    changed |= ImGui::SliderFloat("Robustness threshold", &robustness, 0.5f, 4, "%.3f sigma");
    ImGui::TextWrapped("Lower thresholds reject disagreement more strongly. Exclude a capture from its RAW file node when it should never be used.");
    if (m_Project->snapshot)
        changed |= AnchorControl("Geometry anchor", NodeInputs(*m_Project->snapshot, node), settings, "geometryAnchor");
    ImGui::EndDisabled();
    if (changed && m_Project->snapshot) {
        burst.exposureGroupToleranceEv = tolerance; burst.huberThreshold = robustness;
        settings["sharedBurstSettings"] = Raw::Mfd::SerializeSharedBurstSettings(burst);
        settings["alignmentMode"] = mode == 0 ? "identity" : (mode == 1 ? "translation-only" : "full");
        auto graph = m_Project->snapshot->multiFrameGraph;
        if (auto* edited = Stack::Project::FindMultiFrameGraphNode(graph, node.nodeId)) edited->settings = settings;
        graph.userEdited = true;
        deferredAction = [this, graph = std::move(graph)]() mutable {
            std::string error;
            if (!SetMultiFrameGraphDocument(std::move(graph), &error)) m_MultiFrameWorkspaceStatusText = error;
        };
    }
}
