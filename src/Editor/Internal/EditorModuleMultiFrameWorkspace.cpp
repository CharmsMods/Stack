#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"

#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Library/LibraryManager.h"
#include "Raw/MultiFrame/GraphExecution.h"
#include "Raw/MultiFrameDenoise/Processor.h"
#include "Raw/MultiFrameHdr/Contracts.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

using Stack::Project::EmbeddedAssetRecord;
using Stack::Project::MultiFrameOperationIntent;
using Stack::Project::MultiFrameGraphDocument;
using Stack::Project::MultiFrameGraphLink;
using Stack::Project::MultiFrameGraphNode;
using Stack::Project::MultiFrameGraphNodeKind;
using Stack::Project::MultiFrameGraphResourceType;
using Stack::Project::MultiFrameSourceSet;
using Stack::Project::RawCaptureCompatibilitySummary;
using Stack::Project::RawProjectSnapshot;
using Stack::Project::SourceSetFrame;

std::string OptionalJsonString(
    const nlohmann::json& object,
    const char* key) {
    const auto value = object.find(key);
    if (value == object.end() || !value->is_string()) {
        return {};
    }
    return value->get<std::string>();
}

constexpr float kInspectorWidth = 360.0f;
constexpr float kWorkspaceGap = 12.0f;
constexpr float kWorkspaceChromeClearance = 48.0f;

ImVec2 MeasurementNodeSize(MultiFrameGraphNodeKind kind, bool expanded) {
    switch (kind) {
    case MultiFrameGraphNodeKind::CaptureSet:
        return ImVec2(250.0f, expanded ? 168.0f : 132.0f);
    case MultiFrameGraphNodeKind::CaptureSubset:
        return ImVec2(218.0f, expanded ? 142.0f : 112.0f);
    case MultiFrameGraphNodeKind::BurstDenoise:
    case MultiFrameGraphNodeKind::HdrMerge:
        return ImVec2(230.0f, expanded ? 166.0f : 126.0f);
    case MultiFrameGraphNodeKind::Output:
        return ImVec2(210.0f, expanded ? 150.0f : 116.0f);
    }
    return ImVec2(220.0f, 120.0f);
}

const char* MeasurementNodeSubtitle(MultiFrameGraphNodeKind kind) {
    switch (kind) {
    case MultiFrameGraphNodeKind::CaptureSet: return "RAW Measurement Set";
    case MultiFrameGraphNodeKind::CaptureSubset: return "RAW File";
    case MultiFrameGraphNodeKind::BurstDenoise: return "RAW Measurements -> RAW Estimate";
    case MultiFrameGraphNodeKind::HdrMerge: return "RAW Measurements -> RAW Estimate";
    case MultiFrameGraphNodeKind::Output: return "Virtual CFA -> RAW";
    }
    return "RAW Measurement";
}

bool IsRawFileNode(const MultiFrameGraphNode& node) {
    if (node.kind != MultiFrameGraphNodeKind::CaptureSubset ||
        node.frameIds.size() != 1u) {
        return false;
    }
    const std::string source = node.settings.value("source", std::string());
    return source == "raw-file" || source == "explorer-drop";
}

Stack::Editor::NodeGraphUIVisuals::NodeFamily MeasurementNodeFamily(
    MultiFrameGraphNodeKind kind) {
    using Stack::Editor::NodeGraphUIVisuals::NodeFamily;
    switch (kind) {
    case MultiFrameGraphNodeKind::CaptureSet:
    case MultiFrameGraphNodeKind::CaptureSubset:
        return NodeFamily::Generator;
    case MultiFrameGraphNodeKind::BurstDenoise:
    case MultiFrameGraphNodeKind::HdrMerge:
        return NodeFamily::Merge;
    case MultiFrameGraphNodeKind::Output:
        return NodeFamily::Gray;
    }
    return NodeFamily::Gray;
}

MultiFrameGraphResourceType MeasurementNodeOutputType(
    MultiFrameGraphNodeKind kind) {
    return kind == MultiFrameGraphNodeKind::CaptureSet ||
            kind == MultiFrameGraphNodeKind::CaptureSubset
        ? MultiFrameGraphResourceType::RawMeasurementSet
        : MultiFrameGraphResourceType::RawMeasurement;
}

bool MeasurementNodeHasInput(MultiFrameGraphNodeKind kind) {
    return kind == MultiFrameGraphNodeKind::BurstDenoise ||
        kind == MultiFrameGraphNodeKind::HdrMerge ||
        kind == MultiFrameGraphNodeKind::Output;
}

bool MeasurementNodeHasOutput(MultiFrameGraphNodeKind kind) {
    return kind != MultiFrameGraphNodeKind::Output;
}

const char* ObjectiveLabel(MultiFrameOperationIntent intent) {
    switch (intent) {
    case MultiFrameOperationIntent::RawCaptureSet:
        return "No processing node";
    case MultiFrameOperationIntent::RawBurstHdr:
        return "Range + noise";
    case MultiFrameOperationIntent::RawBurstDenoise:
        return "Burst noise";
    case MultiFrameOperationIntent::Mfsr:
    default:
        return "Unsupported legacy objective";
    }
}

const char* ExecutionAdapterLabel(MultiFrameOperationIntent intent) {
    switch (intent) {
    case MultiFrameOperationIntent::RawCaptureSet:
        return "Choose a processing node in the Inspector";
    case MultiFrameOperationIntent::RawBurstHdr:
        return "HDR v4 execution adapter";
    case MultiFrameOperationIntent::RawBurstDenoise:
        return "Shared Burst V1 - Static Maximum";
    case MultiFrameOperationIntent::Mfsr:
    default:
        return "No executable adapter";
    }
}

std::string FormatShutter(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return "shutter ?";
    }
    std::ostringstream stream;
    if (seconds < 0.5) {
        stream << "1/" << std::max(1, static_cast<int>(std::llround(1.0 / seconds)))
               << " s";
    } else {
        stream << std::fixed << std::setprecision(seconds < 10.0 ? 2 : 1)
               << seconds << " s";
    }
    return stream.str();
}

std::string CaptureMetadataLabel(const RawCaptureCompatibilitySummary& capture) {
    std::ostringstream stream;
    stream << FormatShutter(capture.exposureTimeSeconds);
    if (capture.apertureFNumber > 0.0) {
        stream << "  f/" << std::fixed << std::setprecision(1)
               << capture.apertureFNumber;
    }
    if (capture.isoSpeed > 0.0) {
        stream << "  ISO " << std::fixed << std::setprecision(0)
               << capture.isoSpeed;
    }
    return stream.str();
}

RawCaptureCompatibilitySummary CaptureSummary(
    const EmbeddedAssetRecord* asset) {
    RawCaptureCompatibilitySummary capture;
    if (asset) {
        Stack::Project::DeserializeRawCaptureCompatibilitySummary(
            asset->captureMetadataSummary,
            capture,
            nullptr);
    }
    return capture;
}

std::string FrameLabel(
    const SourceSetFrame& frame,
    const EmbeddedAssetRecord* asset) {
    if (!frame.userLabel.empty()) {
        return frame.userLabel;
    }
    if (asset && !asset->originalFilename.empty()) {
        return asset->originalFilename;
    }
    return frame.frameId.empty() ? "RAW capture" : frame.frameId;
}

ImVec2 FitInside(float width, float height, const ImVec2& bounds) {
    if (!(width > 0.0f) || !(height > 0.0f) ||
        !(bounds.x > 0.0f) || !(bounds.y > 0.0f)) {
        return ImVec2(0.0f, 0.0f);
    }
    const float scale = std::min(bounds.x / width, bounds.y / height);
    return ImVec2(width * scale, height * scale);
}

void DrawWire(
    ImDrawList* drawList,
    const ImVec2& from,
    const ImVec2& to,
    ImU32 color,
    const Stack::Editor::NodeGraphUIVisuals::GraphStyleTokens& graphStyle,
    bool straight,
    const char* label) {
    const float tangent = std::max(48.0f, std::abs(to.x - from.x) * 0.38f);
    const ImVec2 control1(from.x + tangent, from.y);
    const ImVec2 control2(to.x - tangent, to.y);
    const ImU32 underlay = ImGui::ColorConvertFloat4ToU32(
        graphStyle.enabled
            ? graphStyle.linkUnderlay
            : ImVec4(0.02f, 0.025f, 0.03f, 0.82f));
    if (straight) {
        drawList->AddLine(from, to, underlay, 5.0f);
        drawList->AddLine(from, to, color, 2.2f);
    } else {
        Stack::Editor::NodeGraphUIVisuals::DrawBezierLinkStroke(
            drawList, from, control1, control2, to, underlay, 5.0f, false);
        Stack::Editor::NodeGraphUIVisuals::DrawBezierLinkStroke(
            drawList, from, control1, control2, to, color, 2.2f, false);
    }
    if (label && label[0] != '\0') {
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        const ImVec2 midpoint(
            (from.x + to.x) * 0.5f - labelSize.x * 0.5f,
            std::min(from.y, to.y) - labelSize.y - 8.0f);
        drawList->AddText(midpoint, ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
    }
}

bool BeginWorkspaceNode(
    const char* id,
    const ImVec2& cursor,
    const ImVec2& size,
    const char* title,
    const char* subtitle,
    bool selected,
    bool ready) {
    ImGui::SetCursorPos(cursor);
    ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Border);
    if (selected) {
        border = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    }
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, selected ? 1.5f : 1.0f);
    ImGui::BeginChild(
        id,
        size,
        true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton(
        "##NodeHeader",
        ImVec2(ImGui::GetContentRegionAvail().x, 42.0f));
    const ImVec2 minimum = ImGui::GetItemRectMin();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(
        ImVec2(minimum.x + 2.0f, minimum.y + 2.0f),
        ImGui::GetColorU32(ImGuiCol_Text),
        title);
    drawList->AddText(
        ImVec2(minimum.x + 2.0f, minimum.y + 22.0f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        subtitle);
    drawList->AddCircleFilled(
        ImVec2(ImGui::GetItemRectMax().x - 8.0f, minimum.y + 9.0f),
        4.0f,
        ImGui::GetColorU32(
            ready ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled));
    ImGui::Separator();
    ImGui::PopID();
    return clicked;
}

void EndWorkspaceNode() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace

void EditorModule::RenderMultiFrameWorkspaceUI() {
    if (IsBracketingActive()) { RenderBracketingUI(); return; }
    LoadResourceTextures();
    EnsureRawWorkspaceLoaded();
    PumpNonRenderingWork(2.5);
    PumpRawWorkspaceThumbnailTextureUploads();

    // A detached RAW gallery belongs to the RAW workspace, not this graph.
    if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
        CloseRawWorkspaceLabNativeGallery();
    }

    // Root-tab bodies begin at the main viewport origin while Stack's custom
    // app chrome is drawn over the top of that region. Keep all MultiFrame
    // controls below the draggable title-bar band; otherwise the native drag
    // target wins hit testing and makes this workspace's toolbar unclickable.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float minimumContentScreenY =
        viewport->Pos.y + kWorkspaceChromeClearance;
    const float contentScreenY = ImGui::GetCursorScreenPos().y;
    if (contentScreenY < minimumContentScreenY) {
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() +
            (minimumContentScreenY - contentScreenY));
    }

    std::function<void()> deferredAction;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild(
        "MultiFrameWorkspaceRoot",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (!IsMultiFrameRawProjectActive() || !m_Project->snapshot) {
        const Async::TaskState libraryLoadState =
            GetProjectLoadTaskState();
        const bool projectLoading =
            Async::IsBusy(libraryLoadState) ||
            IsRawWorkspaceProjectLoadBusy() ||
            IsDeferredLoadedProjectApplyActive();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float panelWidth = std::min(560.0f, std::max(320.0f, available.x - 40.0f));
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (available.x - panelWidth) * 0.5f),
            std::max(24.0f, available.y * 0.22f)));
        ImGui::BeginChild(
            "MultiFrameEmptyState",
            ImVec2(panelWidth, 290.0f),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextUnformatted(projectLoading ? "Opening Project" : "MultiFrame");
        ImGui::TextDisabled(
            projectLoading
                ? "Restoring the saved capture graph and rendered RAW result."
                : "Build one truthful Virtual Bayer source from a RAW capture set.");
        ImGui::Spacing();
        if (projectLoading) {
            std::string status = GetRawWorkspaceProjectLoadStatusText();
            if (status.empty()) {
                status = GetProjectLoadStatusText();
            }
            ImGui::ProgressBar(
                -0.1f, ImVec2(-1.0f, 0.0f),
                status.empty() ? "Loading project..." : status.c_str());
            ImGui::TextDisabled(
                "You can continue using Stack while the project is restored.");
        } else {
            ImGui::TextWrapped(
                "Start with compatible RAW captures. The capture set stays neutral; "
                "the processing node you add to the graph decides whether it performs "
                "burst denoise, HDR merge, or a future operation.");
            ImGui::Spacing();
        }
        if (!projectLoading && m_RawWorkspace.workspaceRoot.empty()) {
            if (ImGui::Button("Open RAW folder", ImVec2(180.0f, 32.0f))) {
                OpenRawWorkspaceFolderDialog();
            }
            ImGui::TextDisabled("Choose a folder before building a capture set.");
        } else if (!projectLoading) {
            if (ImGui::Button("New Capture Set", ImVec2(220.0f, 34.0f))) {
                BeginMultiFrameCaptureSetGallerySelection(true);
                RequestOpenRawLabTab();
            }
            ImGui::TextDisabled(
                "Selection opens in the Gallery window and returns here after creation.");
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Capture Set  ->  MultiFrame Fusion  ->  Virtual Bayer");
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        RenderMultiFrameRawLabCreationPopup();
        RenderRawWorkspaceLifecyclePopups();
        return;
    }

    RawProjectSnapshot& snapshot = *m_Project->snapshot;
    if (m_MultiFrameWorkspacePresentedProjectId != snapshot.projectId) {
        m_MultiFrameWorkspacePresentedProjectId = snapshot.projectId;
        m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::CaptureSet;
        m_MultiFrameWorkspaceSelectedFrameId.clear();
        m_MultiFrameWorkspaceSelectedNodeId.clear();
        m_MultiFrameWorkspaceConnectionFromNodeId.clear();
        m_MultiFrameWorkspaceGraphGestureDirty = false;
        m_MultiFrameWorkspaceStatusText.clear();
    }

    MultiFrameSourceSet* sourceSet = Stack::Project::FindSourceSet(
        snapshot,
        snapshot.activeSourceSetId);
    if (!sourceSet && !snapshot.sourceSets.empty()) {
        const std::string firstSetId = snapshot.sourceSets.front().sourceSetId;
        deferredAction = [this, firstSetId]() {
            ActivateMultiFrameSourceSet(firstSetId);
        };
    }

    if (!sourceSet) {
        ImGui::TextUnformatted("This project has no active RAW capture set.");
        ImGui::TextDisabled("Create or activate a source set to continue.");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        RenderMultiFrameRawLabCreationPopup();
        RenderRawWorkspaceLifecyclePopups();
        if (deferredAction) deferredAction();
        return;
    }

    const bool captureSet =
        sourceSet->operationIntent == MultiFrameOperationIntent::RawCaptureSet;
    const bool hdr = sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr;
    const bool denoise =
        sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise;
    const std::size_t enabledFrameCount = static_cast<std::size_t>(std::count_if(
        sourceSet->frames.begin(),
        sourceSet->frames.end(),
        [](const SourceSetFrame& frame) { return frame.enabled; }));
    const bool busy = IsMultiFrameGraphProcessingBusy() ||
        IsHdrProcessingBusy() || IsMfdExperimentalProcessingBusy();
    const std::uint64_t inputRevision = hdr
        ? snapshot.hdrInputRevision
        : (denoise ? snapshot.mfdInputRevision : 0u);
    const bool currentResult = captureSet
        ? false
        : (hdr
        ? (m_HdrAdoptedRawResult &&
           m_HdrAdoptedRawResult->projectId == snapshot.projectId &&
           m_HdrAdoptedRawResult->sourceSetId == sourceSet->sourceSetId &&
           m_HdrAdoptedRawResult->inputRevision == inputRevision)
        : (m_MfdAdoptedRawResult &&
           m_MfdAdoptedRawResult->projectId == snapshot.projectId &&
           m_MfdAdoptedRawResult->sourceSetId == sourceSet->sourceSetId &&
           m_MfdAdoptedRawResult->inputRevision == inputRevision));
    const MultiFrameGraphLink* outputInputLink = nullptr;
    for (const MultiFrameGraphLink& link : snapshot.multiFrameGraph.links) {
        if (link.toNodeId == snapshot.multiFrameGraph.outputNodeId) {
            outputInputLink = &link;
            break;
        }
    }
    const MultiFrameGraphNode* terminalMeasurementNode = outputInputLink
        ? Stack::Project::FindMultiFrameGraphNode(
            snapshot.multiFrameGraph, outputInputLink->fromNodeId)
        : nullptr;
    const bool graphBurst = terminalMeasurementNode &&
        terminalMeasurementNode->kind == MultiFrameGraphNodeKind::BurstDenoise;
    const bool graphHdr = terminalMeasurementNode &&
        terminalMeasurementNode->kind == MultiFrameGraphNodeKind::HdrMerge;
    bool graphRequiresDerivedChaining = false;
    if (terminalMeasurementNode) {
        for (const MultiFrameGraphLink& link : snapshot.multiFrameGraph.links) {
            if (link.toNodeId != terminalMeasurementNode->nodeId) continue;
            const MultiFrameGraphNode* producer =
                Stack::Project::FindMultiFrameGraphNode(
                    snapshot.multiFrameGraph, link.fromNodeId);
            if (producer &&
                (producer->kind == MultiFrameGraphNodeKind::BurstDenoise ||
                 producer->kind == MultiFrameGraphNodeKind::HdrMerge)) {
                graphRequiresDerivedChaining = true;
            }
        }
    }
    const bool graphHasProcessor = graphBurst || graphHdr;
    const Raw::MultiFrame::GraphExecutionPlan executionPlan =
        Raw::MultiFrame::BuildMultiFrameGraphExecutionPlan(snapshot);
    const bool invalidBurst = denoise &&
        (sourceSet->operationSchemaVersion !=
             Stack::Project::kMfdOperationSchemaVersion ||
         sourceSet->settings.value("algorithmId", std::string()) !=
             Raw::Mfd::kSharedBurstAlgorithmId);

    ImGui::TextUnformatted(snapshot.projectName.empty()
        ? "MultiFrame project"
        : snapshot.projectName.c_str());
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##MultiFrameSourceSet", sourceSet->name.c_str())) {
        for (const MultiFrameSourceSet& candidate : snapshot.sourceSets) {
            const bool selected = candidate.sourceSetId == sourceSet->sourceSetId;
            if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                const std::string candidateId = candidate.sourceSetId;
                deferredAction = [this, candidateId]() {
                    ActivateMultiFrameSourceSet(candidateId);
                };
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled(
        "Goal: %s",
        graphHdr ? "Range + noise" : (graphBurst ? "Burst noise" : "Draft"));
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled("%llu/%llu enabled",
        static_cast<unsigned long long>(enabledFrameCount),
        static_cast<unsigned long long>(sourceSet->frames.size()));

    const float rightButtonsWidth = 500.0f;
    if (ImGui::GetContentRegionAvail().x > rightButtonsWidth) {
        ImGui::SameLine(ImGui::GetCursorPosX() +
            ImGui::GetContentRegionAvail().x - rightButtonsWidth);
    } else {
        ImGui::Spacing();
    }
    if (ImGui::Checkbox("Expanded stages", &m_MultiFrameWorkspaceExpanded)) {
        if (!m_MultiFrameWorkspaceExpanded &&
            m_MultiFrameWorkspaceSelection ==
                MultiFrameWorkspaceSelection::MatchAndAlign) {
            m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::Fusion;
        }
    }
    ImGui::SameLine();
    const char* processButtonLabel = currentResult
        ? "Reprocess Output"
        : "Process Output";
    ImGui::BeginDisabled(
        busy || enabledFrameCount < 2u ||
        !graphHasProcessor || !executionPlan.valid ||
        !executionPlan.executableWithCurrentAdapters);
    if (ImGui::Button(processButtonLabel)) {
        const std::string setId = sourceSet->sourceSetId;
        deferredAction = [this, setId, graphHdr, graphBurst, hdr, denoise]() {
            std::string error;
            if (((graphHdr && !hdr) || (graphBurst && !denoise)) &&
                !SetMultiFrameOperationIntent(
                    setId,
                    graphHdr
                        ? MultiFrameOperationIntent::RawBurstHdr
                        : MultiFrameOperationIntent::RawBurstDenoise,
                    &error)) {
                m_MultiFrameWorkspaceStatusText = error;
                return;
            }
            const bool started = StartMultiFrameGraphProcessing(setId, &error);
            m_MultiFrameWorkspaceStatusText = started
                ? "Processing queued."
                : (error.empty() ? "Processing could not start." : error);
        };
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (enabledFrameCount < 2u) {
            ImGui::SetTooltip("Enable at least two captures first.");
        } else if (!graphHasProcessor) {
            ImGui::SetTooltip("Connect a Burst Denoise or HDR Fusion node to Output.");
        } else if (!executionPlan.valid) {
            ImGui::SetTooltip(
                "%s",
                executionPlan.errors.empty()
                    ? "The connected graph is invalid."
                    : executionPlan.errors.front().c_str());
        } else if (!executionPlan.executableWithCurrentAdapters) {
            ImGui::SetTooltip(
                "%s",
                executionPlan.warnings.empty()
                    ? "This topology needs a processor capability that is not available."
                    : executionPlan.warnings.back().c_str());
        }
    }
    if (busy) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            deferredAction = [this]() {
                if (IsMultiFrameGraphProcessingBusy())
                    CancelMultiFrameGraphProcessing();
                else if (IsHdrProcessingBusy())
                    CancelHdrProcessing();
                else
                    CancelMfdExperimentalProcessing();
            };
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("RAW")) {
        if (currentResult) {
            m_RawWorkspaceLabUi.activeTool = RawLabTool::Light;
            RequestOpenRawLabTab();
        } else {
            m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::Fusion;
            m_MultiFrameWorkspaceSelectedFrameId.clear();
            m_MultiFrameWorkspaceStatusText = captureSet
                ? "Choose a processor and process this capture set first. "
                  "Publish connects the resulting Virtual Bayer mosaic to RAW automatically."
                : "Process the current MultiFrame node first. Publish connects its "
                  "Virtual Bayer result to RAW automatically.";
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            currentResult
                ? "Continue editing the published Virtual Bayer result in RAW."
                : "A current published result is required. No extra output connection is needed.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Graph")) {
        if (currentResult) {
            RequestOpenEditorTab();
        } else {
            m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::Fusion;
            m_MultiFrameWorkspaceSelectedFrameId.clear();
            m_MultiFrameWorkspaceStatusText = captureSet
                ? "Choose and process a MultiFrame processor first. The published result "
                  "then continues through RAW and into Graph automatically."
                : "Process the current MultiFrame node first. The published result then "
                  "continues through RAW and into Graph automatically.";
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            currentResult
                ? "Continue with the published result in Graph."
                : "A current published result is required. No extra output connection is needed.");
    }

    ImGui::Separator();
    if (captureSet) {
        ImGui::TextColored(
            ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
            graphHasProcessor
                ? "The manually connected graph is ready to review or process."
                : "RAW file nodes are ready. Add Denoise or HDR Fusion and connect them manually.");
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Nothing is adopted until Process Output succeeds.");
    }

    ImGui::Checkbox("Show Fusion preview", &m_MultiFramePreviewVisible);
    const ImVec2 bodyAvailable = ImGui::GetContentRegionAvail();
    const float inspectorWidth = std::clamp(
        kInspectorWidth,
        300.0f,
        std::max(300.0f, bodyAvailable.x * 0.38f));
    const float canvasWidth = std::max(
        420.0f,
        bodyAvailable.x - inspectorWidth - kWorkspaceGap);

    ImGui::BeginGroup();
    const float previewHeight = m_MultiFramePreviewVisible ? std::max(220.0f, bodyAvailable.y * 0.52f) : 0.0f;
    if (m_MultiFramePreviewVisible) {
        ImGui::BeginChild("MultiFrameFusionPreview", ImVec2(canvasWidth, previewHeight), true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderMultiFrameFusionPreview(ImVec2(canvasWidth, previewHeight));
        ImGui::EndChild();
    }
    ImGui::BeginChild(
        "MultiFrameGraphPane",
        ImVec2(canvasWidth, std::max(180.0f, bodyAvailable.y - previewHeight - (m_MultiFramePreviewVisible ? 8.0f : 0.0f))),
        true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    MultiFrameGraphDocument& measurementGraph = snapshot.multiFrameGraph;
    const auto queueNodeAddition = [
        this,
        sourceSet,
        &measurementGraph,
        &deferredAction
    ](MultiFrameGraphNodeKind kind, double positionX, double positionY) {
        MultiFrameGraphDocument edited = measurementGraph;
        MultiFrameGraphNode node;
        node.nodeId = Stack::Project::GenerateStableUuid();
        node.kind = kind;
        node.positionX = positionX;
        node.positionY = positionY;
        switch (kind) {
        case MultiFrameGraphNodeKind::BurstDenoise:
            node.title = "Burst Denoise";
            node.settings = {
                { "profile", "shared-burst-v1-static-maximum" },
                { "alignmentMode", "identity" },
                { "outputMode", "terminal-estimate" },
                { "evidencePolicy", "disjoint-original-evidence-v1" }
            };
            break;
        case MultiFrameGraphNodeKind::HdrMerge:
            node.title = "HDR Fusion";
            node.settings = {
                { "profile", "raw-hdr-v4-compatibility" },
                { "evidencePolicy", "disjoint-original-evidence-v1" },
                { "reopenOriginalMeasurements", true }
            };
            break;
        case MultiFrameGraphNodeKind::CaptureSet:
        case MultiFrameGraphNodeKind::CaptureSubset:
        case MultiFrameGraphNodeKind::Output:
            return;
        }
        const std::string nodeId = node.nodeId;
        edited.nodes.push_back(std::move(node));
        edited.userEdited = true;
        m_MultiFrameWorkspaceSelectedNodeId = nodeId;
        deferredAction = [this, edited = std::move(edited)]() mutable {
            std::string error;
            if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                m_MultiFrameWorkspaceStatusText = error;
            }
        };
    };
    if (m_MultiFrameWorkspaceSelectedNodeId.empty()) {
        const auto captureNode = std::find_if(
            measurementGraph.nodes.begin(),
            measurementGraph.nodes.end(),
            [&](const MultiFrameGraphNode& node) {
                return node.kind == MultiFrameGraphNodeKind::CaptureSubset &&
                    node.sourceSetId == sourceSet->sourceSetId;
            });
        if (captureNode != measurementGraph.nodes.end()) {
            m_MultiFrameWorkspaceSelectedNodeId = captureNode->nodeId;
        }
    }
    ImGui::TextDisabled("Measurement graph");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add Node")) {
        ImGui::OpenPopup("Add MultiFrame node from toolbar");
    }
    ImGui::SameLine();
    Stack::UiActivity::BeginDisabledForWork(busy);
    if (ImGui::SmallButton("Group by exposure")) ImGui::OpenPopup("Build exposure groups");
    if (ImGui::BeginPopup("Build exposure groups")) {
        ImGui::TextWrapped("Build same-exposure Burst Denoise branches and connect them to HDR Fusion. This replaces the current bracket graph.");
        if (ImGui::Button("Build grouped graph")) {
            MultiFrameGraphDocument grouped;
            std::string error;
            if (Stack::Project::BuildExposureGroupedMultiFrameGraph(snapshot, *sourceSet, grouped, &error)) {
                deferredAction = [this, grouped = std::move(grouped)]() mutable {
                    std::string problem;
                    if (!SetMultiFrameGraphDocument(std::move(grouped), &problem)) m_MultiFrameWorkspaceStatusText = problem;
                    else { m_MultiFrameWorkspaceSelectedNodeId.clear(); m_MultiFrameWorkspaceStatusText = "Exposure groups connected. Process Output to prepare Fusion."; }
                };
            } else m_MultiFrameWorkspaceStatusText = error;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("Add MultiFrame node from toolbar")) {
        ImGui::TextDisabled("RAW processing nodes");
        if (ImGui::MenuItem("Burst Denoise")) {
            queueNodeAddition(
                MultiFrameGraphNodeKind::BurstDenoise,
                500.0,
                420.0);
        }
        if (ImGui::MenuItem("HDR Fusion")) {
            queueNodeAddition(
                MultiFrameGraphNodeKind::HdrMerge,
                720.0,
                420.0);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset to RAW Files")) {
        namespace N = Stack::Notifications;
        const auto document = GetProjectDocumentId();
        const auto revision = GetProjectEditRevision();
        const auto fileGraph = Stack::Project::BuildManualMultiFrameGraph(snapshot, *sourceSet);
        N::NoticeSpec notice;
        notice.title = "Reset graph to RAW files?";
        notice.message = "Replace the nodes and connections with unconnected RAW file nodes and Output?";
        notice.details = "Embedded originals and the previous published result are preserved.";
        notice.route = N::Route::Center;
        notice.foreground = m_NotificationForeground;
        notice.operationId = GetNotifier().NewOperation();
        N::ActionSpec reset;
        reset.label = "Reset";
        reset.destructive = true;
        reset.canInvoke = [this, document, revision] {
            return GetProjectDocumentId() == document && GetProjectEditRevision() == revision &&
                IsMultiFrameRawProjectActive();
        };
        reset.invoke = [this, fileGraph] {
            std::string error;
            if (!SetMultiFrameGraphDocument(fileGraph, &error))
                return N::ActionResult::Failure(error.empty() ? "The graph could not be reset." : error);
            m_MultiFrameWorkspaceSelectedNodeId.clear();
            m_MultiFrameWorkspaceStatusText = "Reset to unconnected RAW file nodes.";
            return N::ActionResult::Success();
        };
        N::ActionSpec cancel;
        cancel.label = "Cancel";
        cancel.safeCancel = true;
        cancel.invoke = [] { return N::ActionResult::Success(); };
        notice.actions = {std::move(reset), std::move(cancel)};
        RequestNotificationDecision(std::move(notice));
    }

    const ImVec2 canvasMinimum = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize(
        std::max(200.0f, ImGui::GetContentRegionAvail().x),
        std::max(320.0f, ImGui::GetContentRegionAvail().y));
    // Match the normal Graph canvas ownership model: reserve the drawing area
    // without installing one full-canvas button. A full-size InvisibleButton
    // wins ActiveId on mouse-down before the later node/socket controls can,
    // which makes visible nodes feel immovable and their ports unconnectable.
    ImGui::Dummy(canvasSize);
    const ImVec2 canvasMaximum(
        canvasMinimum.x + canvasSize.x,
        canvasMinimum.y + canvasSize.y);
    const bool canvasHovered =
        !m_LibraryWindowHovered &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseHoveringRect(canvasMinimum, canvasMaximum, false);
    m_MultiFrameDropMinX = canvasMinimum.x;
    m_MultiFrameDropMinY = canvasMinimum.y;
    m_MultiFrameDropMaxX = canvasMaximum.x;
    m_MultiFrameDropMaxY = canvasMaximum.y;
    ImDrawList* graphDrawList = ImGui::GetWindowDrawList();
    const Stack::Editor::NodeGraphUIVisuals::GraphStyleTokens graphStyle =
        Stack::Editor::NodeGraphUIVisuals::BuildGraphStyleTokens(this);
    const ImVec4 canvasColor = graphStyle.enabled
        ? graphStyle.canvas
        : GetWorkspaceBaseColor();
    const auto resourceColor = [graphStyle](
        MultiFrameGraphResourceType type,
        float alpha = 1.0f) {
        ImVec4 color = graphStyle.enabled
            ? (type == MultiFrameGraphResourceType::RawMeasurementSet
                ? graphStyle.socketRaw
                : graphStyle.linkAnalysis)
            : (type == MultiFrameGraphResourceType::RawMeasurementSet
                ? ImVec4(105.0f / 255.0f, 166.0f / 255.0f, 196.0f / 255.0f, 1.0f)
                : ImVec4(142.0f / 255.0f, 207.0f / 255.0f, 163.0f / 255.0f, 1.0f));
        color.w *= std::clamp(alpha, 0.0f, 1.0f);
        return ImGui::ColorConvertFloat4ToU32(color);
    };
    const bool straightLinks =
        Stack::Editor::NodeGraphUIVisuals::GraphStraightLinksEnabled(this);
    graphDrawList->PushClipRect(canvasMinimum, canvasMaximum, true);
    graphDrawList->AddRectFilled(
        canvasMinimum,
        canvasMaximum,
        ImGui::ColorConvertFloat4ToU32(canvasColor));

    double zoom = std::clamp(measurementGraph.viewZoom, 0.25, 4.0);
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const double previousZoom = zoom;
        zoom = std::clamp(
            zoom * (ImGui::GetIO().MouseWheel > 0.0f ? 1.1 : 1.0 / 1.1),
            0.25,
            4.0);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const double graphMouseX =
            (mouse.x - canvasMinimum.x) / previousZoom - measurementGraph.viewPanX;
        const double graphMouseY =
            (mouse.y - canvasMinimum.y) / previousZoom - measurementGraph.viewPanY;
        measurementGraph.viewPanX =
            (mouse.x - canvasMinimum.x) / zoom - graphMouseX;
        measurementGraph.viewPanY =
            (mouse.y - canvasMinimum.y) / zoom - graphMouseY;
        measurementGraph.viewZoom = zoom;
        m_MultiFrameWorkspaceGraphGestureDirty = true;
    }
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        measurementGraph.viewPanX += ImGui::GetIO().MouseDelta.x / zoom;
        measurementGraph.viewPanY += ImGui::GetIO().MouseDelta.y / zoom;
        m_MultiFrameWorkspaceGraphGestureDirty = true;
    }
    m_MultiFrameDropPanX = measurementGraph.viewPanX;
    m_MultiFrameDropPanY = measurementGraph.viewPanY;
    m_MultiFrameDropZoom = zoom;

    const auto graphToScreen = [&](double x, double y) {
        return ImVec2(
            canvasMinimum.x + static_cast<float>((x + measurementGraph.viewPanX) * zoom),
            canvasMinimum.y + static_cast<float>((y + measurementGraph.viewPanY) * zoom));
    };
    const float gridStep = static_cast<float>(48.0 * zoom);
    if (gridStep >= 12.0f) {
        const float offsetX = std::fmod(
            static_cast<float>(measurementGraph.viewPanX * zoom), gridStep);
        const float offsetY = std::fmod(
            static_cast<float>(measurementGraph.viewPanY * zoom), gridStep);
        for (float x = canvasMinimum.x + offsetX; x < canvasMaximum.x; x += gridStep) {
            graphDrawList->AddLine(
                ImVec2(x, canvasMinimum.y), ImVec2(x, canvasMaximum.y),
                graphStyle.light
                    ? IM_COL32(0, 0, 0, 18)
                    : IM_COL32(255, 255, 255, 20));
        }
        for (float y = canvasMinimum.y + offsetY; y < canvasMaximum.y; y += gridStep) {
            graphDrawList->AddLine(
                ImVec2(canvasMinimum.x, y), ImVec2(canvasMaximum.x, y),
                graphStyle.light
                    ? IM_COL32(0, 0, 0, 18)
                    : IM_COL32(255, 255, 255, 20));
        }
    }

    for (const MultiFrameGraphLink& link : measurementGraph.links) {
        const MultiFrameGraphNode* from = Stack::Project::FindMultiFrameGraphNode(
            measurementGraph, link.fromNodeId);
        const MultiFrameGraphNode* to = Stack::Project::FindMultiFrameGraphNode(
            measurementGraph, link.toNodeId);
        if (!from || !to) continue;
        const ImVec2 fromSize = MeasurementNodeSize(from->kind, m_MultiFrameWorkspaceExpanded);
        const ImVec2 toSize = MeasurementNodeSize(to->kind, m_MultiFrameWorkspaceExpanded);
        const ImVec2 fromPosition = graphToScreen(
            from->positionX + fromSize.x,
            from->positionY + fromSize.y * 0.5);
        const ImVec2 toPosition = graphToScreen(
            to->positionX,
            to->positionY + toSize.y * 0.5);
        DrawWire(
            graphDrawList,
            fromPosition,
            toPosition,
            resourceColor(link.resourceType, 0.92f),
            graphStyle,
            straightLinks,
            nullptr);
    }
    if (!m_MultiFrameWorkspaceConnectionFromNodeId.empty()) {
        const MultiFrameGraphNode* from = Stack::Project::FindMultiFrameGraphNode(
            measurementGraph, m_MultiFrameWorkspaceConnectionFromNodeId);
        if (from) {
            const ImVec2 size = MeasurementNodeSize(
                from->kind, m_MultiFrameWorkspaceExpanded);
            DrawWire(
                graphDrawList,
                graphToScreen(
                    from->positionX + size.x,
                    from->positionY + size.y * 0.5),
                ImGui::GetIO().MousePos,
                resourceColor(MeasurementNodeOutputType(from->kind), 0.88f),
                graphStyle,
                straightLinks,
                nullptr);
        }
    }

    bool anyNodeHovered = false;
    bool connectionCompletedThisFrame = false;
    bool requestNodeActionsPopup = false;
    for (MultiFrameGraphNode& node : measurementGraph.nodes) {
        const ImVec2 logicalSize = MeasurementNodeSize(
            node.kind, m_MultiFrameWorkspaceExpanded);
        const ImVec2 nodeMinimum = graphToScreen(node.positionX, node.positionY);
        const ImVec2 nodeSize(
            logicalSize.x * static_cast<float>(zoom),
            logicalSize.y * static_cast<float>(zoom));
        if (nodeMinimum.x + nodeSize.x < canvasMinimum.x ||
            nodeMinimum.y + nodeSize.y < canvasMinimum.y ||
            nodeMinimum.x > canvasMaximum.x || nodeMinimum.y > canvasMaximum.y) {
            continue;
        }
        ImGui::PushID(node.nodeId.c_str());
        // Keep the body hit target clear of the socket hit regions at each
        // edge. Overlapping one full-node button with later port buttons has
        // the same ActiveId problem as the old full-canvas button.
        ImGui::SetCursorScreenPos(ImVec2(nodeMinimum.x + 14.0f, nodeMinimum.y));
        ImGui::InvisibleButton(
            "##MeasurementNode",
            ImVec2(std::max(1.0f, nodeSize.x - 28.0f), nodeSize.y),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool nodeHovered = ImGui::IsItemHovered();
        anyNodeHovered = anyNodeHovered || nodeHovered;
        const bool selected = m_MultiFrameWorkspaceSelectedNodeId == node.nodeId;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            m_MultiFrameWorkspaceSelectedNodeId = node.nodeId;
            m_MultiFrameWorkspaceSelectedFrameId.clear();
            switch (node.kind) {
            case MultiFrameGraphNodeKind::CaptureSet:
                m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::CaptureSet;
                break;
            case MultiFrameGraphNodeKind::CaptureSubset:
                m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::CaptureSubset;
                break;
            case MultiFrameGraphNodeKind::BurstDenoise:
            case MultiFrameGraphNodeKind::HdrMerge:
                m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::Fusion;
                break;
            case MultiFrameGraphNodeKind::Output:
                m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::Publish;
                break;
            }
        }
        if (nodeHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            (node.kind == MultiFrameGraphNodeKind::HdrMerge || node.kind == MultiFrameGraphNodeKind::BurstDenoise))
            m_MultiFramePreviewVisible = true;
        if (ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
            node.positionX += ImGui::GetIO().MouseDelta.x / zoom;
            node.positionY += ImGui::GetIO().MouseDelta.y / zoom;
            measurementGraph.userEdited = true;
            m_MultiFrameWorkspaceGraphGestureDirty = true;
        }
        if (nodeHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            m_MultiFrameWorkspaceSelectedNodeId = node.nodeId;
            requestNodeActionsPopup = true;
        }
        const ImVec2 nodeMaximum(nodeMinimum.x + nodeSize.x, nodeMinimum.y + nodeSize.y);
        const auto familyStyle =
            Stack::Editor::NodeGraphUIVisuals::StyleForFamily(
                MeasurementNodeFamily(node.kind), graphStyle);
        const float nodeUiScale = static_cast<float>(zoom);
        ImVec4 nodeSurface = familyStyle.fill;
        if (nodeHovered) {
            nodeSurface = Stack::Editor::NodeGraphUIVisuals::BlendColor(
                nodeSurface, familyStyle.accent, 0.06f);
        }
        Stack::Editor::NodeGraphUIVisuals::DrawGraphNodeSpotlightSurface(
            graphDrawList,
            nodeMinimum,
            nodeMaximum,
            nodeSurface,
            familyStyle.border,
            familyStyle.accent,
            graphStyle,
            selected,
            true,
            nodeUiScale,
            8.0f * nodeUiScale,
            (selected ? 1.8f : 1.15f) * nodeUiScale);
        const float fontSize = ImGui::GetFontSize() * nodeUiScale;
        graphDrawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(nodeMinimum.x + 12.0f * nodeUiScale,
                   nodeMinimum.y + 13.0f * nodeUiScale),
            ImGui::ColorConvertFloat4ToU32(familyStyle.text),
            node.title.empty()
                ? Stack::Project::MultiFrameGraphNodeKindName(node.kind)
                : node.title.c_str());
        graphDrawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(nodeMinimum.x + 12.0f * nodeUiScale,
                   nodeMinimum.y + 35.0f * nodeUiScale),
            ImGui::ColorConvertFloat4ToU32(familyStyle.mutedText),
            MeasurementNodeSubtitle(node.kind));
        std::string detail;
        if (IsRawFileNode(node)) {
            const SourceSetFrame* fileFrame = nullptr;
            for (const SourceSetFrame& frame : sourceSet->frames) {
                if (frame.frameId == node.frameIds.front()) {
                    fileFrame = &frame;
                    break;
                }
            }
            const EmbeddedAssetRecord* asset = fileFrame
                ? Stack::Project::FindEmbeddedAsset(snapshot, fileFrame->assetId)
                : nullptr;
            detail = CaptureMetadataLabel(CaptureSummary(asset));
        } else if (node.kind == MultiFrameGraphNodeKind::CaptureSet ||
                   node.kind == MultiFrameGraphNodeKind::CaptureSubset) {
            detail = std::to_string(node.frameIds.size()) + " capture" +
                (node.frameIds.size() == 1u ? "" : "s");
        } else if (node.kind == MultiFrameGraphNodeKind::BurstDenoise) {
            detail = "temporal base + residual evidence";
        } else if (node.kind == MultiFrameGraphNodeKind::HdrMerge) {
            detail = "range + overlap precision";
        } else {
            detail = currentResult ? "published and current" : "awaiting processing";
        }
        graphDrawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(nodeMinimum.x + 12.0f * nodeUiScale,
                   nodeMinimum.y + 62.0f * nodeUiScale),
            ImGui::GetColorU32(
                node.kind == MultiFrameGraphNodeKind::Output && currentResult
                    ? ImGuiCol_CheckMark
                    : ImGuiCol_TextDisabled),
            detail.c_str());
        if (m_MultiFrameWorkspaceExpanded &&
            (node.kind == MultiFrameGraphNodeKind::BurstDenoise ||
             node.kind == MultiFrameGraphNodeKind::HdrMerge)) {
            graphDrawList->AddText(
                ImGui::GetFont(),
                fontSize,
                ImVec2(nodeMinimum.x + 12.0f * nodeUiScale,
                       nodeMinimum.y + 88.0f * nodeUiScale),
                ImGui::ColorConvertFloat4ToU32(familyStyle.mutedText),
                "calibrated CFA measurements");
            graphDrawList->AddText(
                ImGui::GetFont(),
                fontSize,
                ImVec2(nodeMinimum.x + 12.0f * nodeUiScale,
                       nodeMinimum.y + 108.0f * nodeUiScale),
                ImGui::ColorConvertFloat4ToU32(familyStyle.mutedText),
                "uncertainty + evidence lineage");
        }

        if (MeasurementNodeHasInput(node.kind)) {
            const ImVec2 port(
                nodeMinimum.x,
                nodeMinimum.y + nodeSize.y * 0.5f);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float portDx = mouse.x - port.x;
            const float portDy = mouse.y - port.y;
            const bool portHovered =
                portDx * portDx + portDy * portDy <= 144.0f;
            anyNodeHovered = anyNodeHovered || portHovered;
            ImGui::SetCursorScreenPos(ImVec2(port.x - 8.0f, port.y - 8.0f));
            ImGui::InvisibleButton("##MeasurementInput", ImVec2(16.0f, 16.0f));
            EditorNodeGraph::SocketDefinition measurementSocket;
            measurementSocket.type = EditorNodeGraph::SocketType::Raw;
            measurementSocket.direction = EditorNodeGraph::SocketDirection::Input;
            const bool portConnected = std::any_of(measurementGraph.links.begin(), measurementGraph.links.end(),
                [&](const MultiFrameGraphLink& link) { return link.toNodeId == node.nodeId; });
            Stack::Editor::NodeGraphUIVisuals::DrawSocketPin(
                graphDrawList,
                port,
                6.0f * std::clamp(nodeUiScale, 0.65f, 1.5f),
                resourceColor(MultiFrameGraphResourceType::RawMeasurement, 1.0f),
                graphStyle,
                portHovered,
                measurementSocket,
                portConnected,
                !m_MultiFrameWorkspaceConnectionFromNodeId.empty() ? 1.0f : 0.0f);
            const bool finishConnection =
                portHovered &&
                !m_MultiFrameWorkspaceConnectionFromNodeId.empty() &&
                (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                 ImGui::IsMouseReleased(ImGuiMouseButton_Left));
            if (finishConnection &&
                !connectionCompletedThisFrame) {
                const std::string fromId = m_MultiFrameWorkspaceConnectionFromNodeId;
                const MultiFrameGraphNode* from = Stack::Project::FindMultiFrameGraphNode(
                    measurementGraph, fromId);
                if (!from || fromId == node.nodeId ||
                    Stack::Project::WouldCreateMultiFrameGraphCycle(
                        measurementGraph, fromId, node.nodeId)) {
                    m_MultiFrameWorkspaceStatusText =
                        "That connection would create a cycle or has no valid source.";
                } else {
                    MultiFrameGraphDocument edited = measurementGraph;
                    MultiFrameGraphLink link;
                    link.linkId = Stack::Project::GenerateStableUuid();
                    link.fromNodeId = fromId;
                    link.fromPortId = MeasurementNodeOutputType(from->kind) ==
                            MultiFrameGraphResourceType::RawMeasurementSet
                        ? "measurements"
                        : "estimate";
                    link.toNodeId = node.nodeId;
                    link.toPortId = "measurements";
                    link.resourceType = MeasurementNodeOutputType(from->kind);
                    for (const MultiFrameGraphLink& existing : edited.links) {
                        if (existing.toNodeId == link.toNodeId &&
                            existing.toPortId == link.toPortId) {
                            link.variadicOrder = std::max(
                                link.variadicOrder,
                                existing.variadicOrder + 1u);
                        }
                    }
                    edited.links.push_back(std::move(link));
                    edited.userEdited = true;
                    const Stack::Project::MultiFrameGraphValidationResult validation =
                        Stack::Project::ValidateMultiFrameGraph(
                            edited, snapshot, false);
                    if (!validation.valid) {
                        m_MultiFrameWorkspaceStatusText = validation.errors.empty()
                            ? "That RAW measurement connection is invalid."
                            : validation.errors.front();
                    } else {
                        deferredAction = [this, edited = std::move(edited)]() mutable {
                            std::string error;
                            if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                                m_MultiFrameWorkspaceStatusText = error;
                            }
                        };
                    }
                }
                m_MultiFrameWorkspaceConnectionFromNodeId.clear();
                connectionCompletedThisFrame = true;
            }
        }
        if (MeasurementNodeHasOutput(node.kind)) {
            const ImVec2 port(
                nodeMaximum.x,
                nodeMinimum.y + nodeSize.y * 0.5f);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float portDx = mouse.x - port.x;
            const float portDy = mouse.y - port.y;
            const bool portHovered =
                portDx * portDx + portDy * portDy <= 144.0f;
            anyNodeHovered = anyNodeHovered || portHovered;
            ImGui::SetCursorScreenPos(ImVec2(port.x - 8.0f, port.y - 8.0f));
            ImGui::InvisibleButton("##MeasurementOutput", ImVec2(16.0f, 16.0f));
            EditorNodeGraph::SocketDefinition measurementSocket;
            measurementSocket.type = EditorNodeGraph::SocketType::Raw;
            measurementSocket.direction = EditorNodeGraph::SocketDirection::Output;
            const bool portConnected = std::any_of(measurementGraph.links.begin(), measurementGraph.links.end(),
                [&](const MultiFrameGraphLink& link) { return link.fromNodeId == node.nodeId; });
            Stack::Editor::NodeGraphUIVisuals::DrawSocketPin(
                graphDrawList,
                port,
                6.0f * std::clamp(nodeUiScale, 0.65f, 1.5f),
                resourceColor(MeasurementNodeOutputType(node.kind), 1.0f),
                graphStyle,
                portHovered,
                measurementSocket,
                portConnected,
                m_MultiFrameWorkspaceConnectionFromNodeId == node.nodeId
                    ? 0.82f
                    : 0.0f);
            if (portHovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_MultiFrameWorkspaceConnectionFromNodeId = node.nodeId;
                m_MultiFrameWorkspaceStatusText =
                    "Drag to a compatible input socket and release. A click-then-click connection also works.";
            }
        }
        ImGui::PopID();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_MultiFrameWorkspaceConnectionFromNodeId.clear();
    }
    graphDrawList->PopClipRect();

    auto& contextSpawnX = m_ProjectInteractionUi.multiFrame.contextSpawnX;
    auto& contextSpawnY = m_ProjectInteractionUi.multiFrame.contextSpawnY;
    const auto updateContextSpawn = [&]() {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        contextSpawnX =
            (mouse.x - canvasMinimum.x) / zoom - measurementGraph.viewPanX;
        contextSpawnY =
            (mouse.y - canvasMinimum.y) / zoom - measurementGraph.viewPanY;
    };
    if (requestNodeActionsPopup) {
        ImGui::OpenPopup("MultiFrame node actions");
    } else if (canvasHovered && !anyNodeHovered &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        updateContextSpawn();
        ImGui::OpenPopup("Add MultiFrame node on canvas");
    }
    if (canvasHovered && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        updateContextSpawn();
        ImGui::OpenPopup("Add MultiFrame node on canvas");
    }

    if (ImGui::BeginPopup("Add MultiFrame node on canvas")) {
        ImGui::TextDisabled("Add at pointer");
        if (ImGui::MenuItem("Burst Denoise")) {
            queueNodeAddition(
                MultiFrameGraphNodeKind::BurstDenoise,
                contextSpawnX,
                contextSpawnY);
        }
        if (ImGui::MenuItem("HDR Fusion")) {
            queueNodeAddition(
                MultiFrameGraphNodeKind::HdrMerge,
                contextSpawnX,
                contextSpawnY);
        }
        ImGui::Separator();
        ImGui::TextDisabled("Drop RAW files here to add source nodes");
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("MultiFrame node actions")) {
        const MultiFrameGraphNode* selectedNode =
            Stack::Project::FindMultiFrameGraphNode(
                measurementGraph,
                m_MultiFrameWorkspaceSelectedNodeId);
        if (selectedNode) {
            ImGui::TextUnformatted(
                selectedNode->title.empty()
                    ? Stack::Project::MultiFrameGraphNodeKindName(
                        selectedNode->kind)
                    : selectedNode->title.c_str());
            ImGui::Separator();
            const bool hasInputs = std::any_of(
                measurementGraph.links.begin(),
                measurementGraph.links.end(),
                [&](const MultiFrameGraphLink& link) {
                    return link.toNodeId == selectedNode->nodeId;
                });
            const bool hasOutputs = std::any_of(
                measurementGraph.links.begin(),
                measurementGraph.links.end(),
                [&](const MultiFrameGraphLink& link) {
                    return link.fromNodeId == selectedNode->nodeId;
                });
            if (ImGui::MenuItem(
                    "Disconnect inputs",
                    nullptr,
                    false,
                    hasInputs)) {
                MultiFrameGraphDocument edited = measurementGraph;
                const std::string nodeId = selectedNode->nodeId;
                edited.links.erase(std::remove_if(
                    edited.links.begin(),
                    edited.links.end(),
                    [&](const MultiFrameGraphLink& link) {
                        return link.toNodeId == nodeId;
                    }), edited.links.end());
                edited.userEdited = true;
                deferredAction = [this, edited = std::move(edited)]() mutable {
                    std::string error;
                    if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
            if (ImGui::MenuItem(
                    "Disconnect outputs",
                    nullptr,
                    false,
                    hasOutputs)) {
                MultiFrameGraphDocument edited = measurementGraph;
                const std::string nodeId = selectedNode->nodeId;
                edited.links.erase(std::remove_if(
                    edited.links.begin(),
                    edited.links.end(),
                    [&](const MultiFrameGraphLink& link) {
                        return link.fromNodeId == nodeId;
                    }), edited.links.end());
                edited.userEdited = true;
                deferredAction = [this, edited = std::move(edited)]() mutable {
                    std::string error;
                    if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
            const bool canDelete =
                selectedNode->kind != MultiFrameGraphNodeKind::CaptureSet &&
                selectedNode->kind != MultiFrameGraphNodeKind::Output;
            ImGui::Separator();
            if (ImGui::MenuItem("Delete node", "Del", false, canDelete)) {
                MultiFrameGraphDocument edited = measurementGraph;
                const std::string nodeId = selectedNode->nodeId;
                edited.nodes.erase(std::remove_if(
                    edited.nodes.begin(),
                    edited.nodes.end(),
                    [&](const MultiFrameGraphNode& node) {
                        return node.nodeId == nodeId;
                    }), edited.nodes.end());
                edited.links.erase(std::remove_if(
                    edited.links.begin(),
                    edited.links.end(),
                    [&](const MultiFrameGraphLink& link) {
                        return link.fromNodeId == nodeId ||
                            link.toNodeId == nodeId;
                    }), edited.links.end());
                edited.userEdited = true;
                m_MultiFrameWorkspaceSelectedNodeId.clear();
                deferredAction = [this, edited = std::move(edited)]() mutable {
                    std::string error;
                    if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
        }
        ImGui::EndPopup();
    }

    if (m_MultiFrameWorkspaceGraphGestureDirty &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        MultiFrameGraphDocument edited = measurementGraph;
        m_MultiFrameWorkspaceGraphGestureDirty = false;
        deferredAction = [this, edited = std::move(edited)]() mutable {
            std::string error;
            if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                m_MultiFrameWorkspaceStatusText = error;
            }
        };
    }
    ImGui::EndChild();

    ImGui::EndGroup();
    ImGui::SameLine(0.0f, kWorkspaceGap);
    ImGui::BeginChild("MultiFrameInspector", ImVec2(0.0f, 0.0f), true);
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();

    const MultiFrameGraphNode* selectedMeasurementNode =
        Stack::Project::FindMultiFrameGraphNode(
            snapshot.multiFrameGraph,
            m_MultiFrameWorkspaceSelectedNodeId);

    if (selectedMeasurementNode) {
        ImGui::TextDisabled(
            "%s",
            Stack::Project::MultiFrameGraphNodeKindName(
                selectedMeasurementNode->kind));
        if (selectedMeasurementNode->kind != MultiFrameGraphNodeKind::CaptureSet &&
            selectedMeasurementNode->kind != MultiFrameGraphNodeKind::Output) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete node")) {
                MultiFrameGraphDocument edited = snapshot.multiFrameGraph;
                const std::string nodeId = selectedMeasurementNode->nodeId;
                edited.nodes.erase(std::remove_if(
                    edited.nodes.begin(), edited.nodes.end(),
                    [&](const MultiFrameGraphNode& node) {
                        return node.nodeId == nodeId;
                    }), edited.nodes.end());
                edited.links.erase(std::remove_if(
                    edited.links.begin(), edited.links.end(),
                    [&](const MultiFrameGraphLink& link) {
                        return link.fromNodeId == nodeId || link.toNodeId == nodeId;
                    }), edited.links.end());
                edited.userEdited = true;
                m_MultiFrameWorkspaceSelectedNodeId.clear();
                m_MultiFrameWorkspaceSelection =
                    MultiFrameWorkspaceSelection::CaptureSet;
                deferredAction = [this, edited = std::move(edited)]() mutable {
                    std::string error;
                    if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
        }
        std::vector<const MultiFrameGraphLink*> selectedInputs;
        for (const MultiFrameGraphLink& link : snapshot.multiFrameGraph.links) {
            if (link.toNodeId == selectedMeasurementNode->nodeId) {
                selectedInputs.push_back(&link);
            }
        }
        std::sort(selectedInputs.begin(), selectedInputs.end(),
            [](const MultiFrameGraphLink* a, const MultiFrameGraphLink* b) {
                return a->variadicOrder < b->variadicOrder;
            });
        if (!selectedInputs.empty()) {
            ImGui::SeparatorText("Inputs");
            for (const MultiFrameGraphLink* link : selectedInputs) {
                const MultiFrameGraphNode* from =
                    Stack::Project::FindMultiFrameGraphNode(
                        snapshot.multiFrameGraph, link->fromNodeId);
                ImGui::PushID(link->linkId.c_str());
                ImGui::BulletText(
                    "%u  %s",
                    link->variadicOrder + 1u,
                    from && !from->title.empty() ? from->title.c_str() : "RAW measurement");
                ImGui::SameLine();
                if (ImGui::SmallButton("Disconnect")) {
                    MultiFrameGraphDocument edited = snapshot.multiFrameGraph;
                    const std::string linkId = link->linkId;
                    edited.links.erase(std::remove_if(
                        edited.links.begin(), edited.links.end(),
                        [&](const MultiFrameGraphLink& candidate) {
                            return candidate.linkId == linkId;
                        }), edited.links.end());
                    edited.userEdited = true;
                    deferredAction = [this, edited = std::move(edited)]() mutable {
                        std::string error;
                        if (!SetMultiFrameGraphDocument(std::move(edited), &error)) {
                            m_MultiFrameWorkspaceStatusText = error;
                        }
                    };
                }
                ImGui::PopID();
            }
        }
        ImGui::Separator();
    }

    if (m_MultiFrameWorkspaceSelection == MultiFrameWorkspaceSelection::Frame) {
        const SourceSetFrame* selectedFrame = nullptr;
        for (const SourceSetFrame& frame : sourceSet->frames) {
            if (frame.frameId == m_MultiFrameWorkspaceSelectedFrameId) {
                selectedFrame = &frame;
                break;
            }
        }
        if (selectedFrame) {
            const EmbeddedAssetRecord* asset = Stack::Project::FindEmbeddedAsset(
                snapshot,
                selectedFrame->assetId);
            const RawCaptureCompatibilitySummary capture = CaptureSummary(asset);
            ImGui::TextWrapped("%s", FrameLabel(*selectedFrame, asset).c_str());
            ImGui::TextDisabled("RAW capture dataset member");
            ImGui::Spacing();
            bool enabled = selectedFrame->enabled;
            Stack::UiActivity::BeginDisabledForWork(busy);
            if (ImGui::Checkbox("Include in processing", &enabled)) {
                const std::string setId = sourceSet->sourceSetId;
                const std::string frameId = selectedFrame->frameId;
                deferredAction = [this, setId, frameId, enabled]() {
                    std::string error;
                    if (!SetMultiFrameFrameEnabled(setId, frameId, enabled, &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
            const bool reference =
                selectedFrame->frameId == sourceSet->referenceFrameId;
            ImGui::BeginDisabled(reference);
            if (ImGui::Button(reference ? "Geometric reference" : "Make reference")) {
                const std::string setId = sourceSet->sourceSetId;
                const std::string frameId = selectedFrame->frameId;
                deferredAction = [this, setId, frameId]() {
                    std::string error;
                    if (!SetMultiFrameReferenceFrame(setId, frameId, &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (denoise) {
                const nlohmann::json trust = sourceSet->settings.value(
                    "frameTrust", nlohmann::json::object());
                float frameTrust = static_cast<float>(
                    trust.value(selectedFrame->frameId, 1.0));
                ImGui::SetNextItemWidth(-1.0f);
                Stack::UiActivity::BeginDisabledForWork(busy, reference || invalidBurst);
                if (ImGui::SliderFloat(
                        "Trust attenuation",
                        &frameTrust,
                        0.0f,
                        1.0f,
                        "%.2f")) {
                    const std::string setId = sourceSet->sourceSetId;
                    const std::string frameId = selectedFrame->frameId;
                    deferredAction = [this, setId, frameId, frameTrust]() {
                        std::string error;
                        if (!SetMfdSharedBurstFrameTrust(
                                setId, frameId, frameTrust, &error)) {
                            m_MultiFrameWorkspaceStatusText = error;
                        }
                    };
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(invalidBurst
                        ? "Reprocess with Shared Burst V1 before editing frame trust."
                        : reference
                            ? "The temporal owner/reference has fixed reliability 1. Choose another reference to attenuate this frame."
                            : "May only reduce this frame's influence. 1.00 uses all evidence that passes safety gates.");
                }
            }
            ImGui::Spacing();
            ImGui::Text("Shutter: %s", FormatShutter(capture.exposureTimeSeconds).c_str());
            const std::string apertureLabel = capture.apertureFNumber > 0.0
                ? "f/" + std::to_string(capture.apertureFNumber)
                : "unknown";
            ImGui::Text("Aperture: %s", apertureLabel.c_str());
            ImGui::Text("ISO: %.0f", capture.isoSpeed);
            ImGui::Text("CFA: %s", capture.cfaPattern.empty()
                ? "unknown" : capture.cfaPattern.c_str());
            ImGui::Text("Size: %d x %d", capture.visibleWidth, capture.visibleHeight);
            ImGui::Spacing();
            ImGui::TextWrapped("%s", asset && !asset->originalSourcePath.empty()
                ? asset->originalSourcePath.c_str()
                : "Embedded original; prior source path unavailable.");
        } else {
            ImGui::TextDisabled("Select a capture in the Capture Set node.");
        }
    } else if (
        m_MultiFrameWorkspaceSelection ==
            MultiFrameWorkspaceSelection::CaptureSubset) {
        if (selectedMeasurementNode &&
            selectedMeasurementNode->kind == MultiFrameGraphNodeKind::CaptureSubset) {
            if (IsRawFileNode(*selectedMeasurementNode)) {
                const SourceSetFrame* fileFrame = nullptr;
                for (const SourceSetFrame& frame : sourceSet->frames) {
                    if (frame.frameId == selectedMeasurementNode->frameIds.front()) {
                        fileFrame = &frame;
                        break;
                    }
                }
                const EmbeddedAssetRecord* asset = fileFrame
                    ? Stack::Project::FindEmbeddedAsset(snapshot, fileFrame->assetId)
                    : nullptr;
                const RawCaptureCompatibilitySummary capture = CaptureSummary(asset);
                ImGui::TextWrapped(
                    "%s",
                    fileFrame
                        ? FrameLabel(*fileFrame, asset).c_str()
                        : selectedMeasurementNode->title.c_str());
                ImGui::TextDisabled("Embedded RAW dataset file");
                ImGui::Spacing();
                ImGui::Text("Shutter: %s", FormatShutter(capture.exposureTimeSeconds).c_str());
                ImGui::Text("Aperture: %s",
                    capture.apertureFNumber > 0.0
                        ? ("f/" + std::to_string(capture.apertureFNumber)).c_str()
                        : "unknown");
                ImGui::Text("ISO: %.0f", capture.isoSpeed);
                ImGui::Text("CFA: %s", capture.cfaPattern.empty()
                    ? "unknown" : capture.cfaPattern.c_str());
                ImGui::Text("Size: %d x %d", capture.visibleWidth, capture.visibleHeight);
                if (fileFrame) {
                    bool included = fileFrame->enabled;
                    Stack::UiActivity::BeginDisabledForWork(busy);
                    if (ImGui::Checkbox("Include capture", &included)) {
                        const auto frameId = fileFrame->frameId;
                        const auto setId = sourceSet->sourceSetId;
                        deferredAction = [this, frameId, setId, included]() {
                            std::string error;
                            if (!SetMultiFrameFrameEnabled(setId, frameId, included, &error))
                                m_MultiFrameWorkspaceStatusText = error;
                        };
                    }
                    ImGui::EndDisabled();
                    ImGui::Text("Role: %s%s",
                        fileFrame->frameId == sourceSet->referenceFrameId
                            ? "geometric reference"
                            : "capture",
                        fileFrame->enabled ? "" : " (excluded in capture set)");
                }
                ImGui::Spacing();
                ImGui::TextWrapped("%s",
                    asset && !asset->originalSourcePath.empty()
                        ? asset->originalSourcePath.c_str()
                        : "Embedded original; prior source path unavailable.");
                ImGui::Spacing();
                ImGui::TextDisabled(
                    "Connect this file directly to Burst Denoise or HDR Fusion. The file node is never an exposure group.");
            } else {
                ImGui::TextWrapped("%s", selectedMeasurementNode->title.c_str());
                ImGui::TextWrapped(
                    "Legacy capture subset. Reset the graph to replace legacy grouped nodes with one node per RAW file.");
            }
        }
    } else if (selectedMeasurementNode && selectedMeasurementNode->kind == MultiFrameGraphNodeKind::HdrMerge) {
        RenderMultiFrameFusionInspector(*selectedMeasurementNode, busy, deferredAction);
    } else if (selectedMeasurementNode && selectedMeasurementNode->kind == MultiFrameGraphNodeKind::BurstDenoise) {
        RenderMultiFrameBurstInspector(*selectedMeasurementNode, busy, deferredAction);
    } else if (
        m_MultiFrameWorkspaceSelection ==
            MultiFrameWorkspaceSelection::MatchAndAlign) {
        ImGui::TextUnformatted("Match & Align Captures");
        ImGui::TextWrapped(
            "Exposure matching and geometry are one guided stage, but their "
            "evidence and cache identities remain separate.");
        ImGui::Spacing();
        if (hdr) {
            Raw::Hdr::Parameters parameters;
            std::string parameterError;
            Raw::Hdr::DeserializeParameters(
                sourceSet->settings.value("parameters", nlohmann::json::object()),
                parameters,
                &parameterError);
            int alignmentIndex = parameters.alignmentMode ==
                    Raw::Hdr::AlignmentMode::Identity
                ? 1 : 0;
            const char* modes[] = {
                "Auto: identity or translation",
                "Identity: fixed tripod"
            };
            Stack::UiActivity::BeginDisabledForWork(busy, invalidBurst);
            if (ImGui::Combo("Alignment", &alignmentIndex, modes, 2)) {
                const std::string setId = sourceSet->sourceSetId;
                const std::string referenceId = sourceSet->referenceFrameId;
                const bool autoReference = sourceSet->settings.value(
                    "automaticGeometricReference", true);
                const bool autoAnchor = sourceSet->settings.value(
                    "automaticRadiometricAnchor", true);
                const std::string radiometricAnchor = OptionalJsonString(
                    sourceSet->settings,
                    "radiometricAnchorFrameId");
                deferredAction = [
                    this,
                    setId,
                    alignmentIndex,
                    autoReference,
                    referenceId,
                    autoAnchor,
                    radiometricAnchor
                ]() {
                    std::string error;
                    if (!SetHdrProcessingConfiguration(
                            setId,
                            alignmentIndex == 1
                                ? Raw::Hdr::AlignmentMode::Identity
                                : Raw::Hdr::AlignmentMode::AutoTranslation,
                            autoReference,
                            referenceId,
                            autoAnchor,
                            radiometricAnchor,
                            &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
            ImGui::EndDisabled();
        } else if (denoise) {
            Raw::Mfd::MfdAlignmentMode alignment = Raw::Mfd::MfdAlignmentMode::Full;
            Raw::Mfd::ParseMfdAlignmentMode(
                sourceSet->settings.value(
                    "experimentalAlignmentMode", std::string("full")),
                alignment);
            int alignmentIndex = alignment == Raw::Mfd::MfdAlignmentMode::Identity
                ? 2
                : (alignment == Raw::Mfd::MfdAlignmentMode::TranslationOnly
                    ? 1 : 0);
            const char* modes[] = {
                "Full registration",
                "Global translation",
                "Identity coordinates"
            };
            Stack::UiActivity::BeginDisabledForWork(busy);
            if (ImGui::Combo("Alignment", &alignmentIndex, modes, 3)) {
                const std::string setId = sourceSet->sourceSetId;
                deferredAction = [this, setId, alignmentIndex]() {
                    const Raw::Mfd::MfdAlignmentMode requested = alignmentIndex == 2
                        ? Raw::Mfd::MfdAlignmentMode::Identity
                        : (alignmentIndex == 1
                            ? Raw::Mfd::MfdAlignmentMode::TranslationOnly
                            : Raw::Mfd::MfdAlignmentMode::Full);
                    std::string error;
                    if (!SetMfdExperimentalAlignmentMode(setId, requested, &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
            ImGui::EndDisabled();
            Raw::Mfd::SharedBurstSettings burstSettings;
            std::string burstError;
            const auto storedSettings =
                sourceSet->settings.find("sharedBurstSettings");
            if (storedSettings != sourceSet->settings.end()) {
                Raw::Mfd::DeserializeSharedBurstSettings(
                    *storedSettings, burstSettings, &burstError);
            }
            float tolerance = static_cast<float>(
                burstSettings.exposureGroupToleranceEv);
            ImGui::SetNextItemWidth(-1.0f);
            Stack::UiActivity::BeginDisabledForWork(busy);
            if (ImGui::SliderFloat(
                    "Exposure group tolerance",
                    &tolerance,
                    0.1f,
                    2.0f,
                    "%.2f EV")) {
                const std::string setId = sourceSet->sourceSetId;
                deferredAction = [this, setId, tolerance]() {
                    std::string error;
                    if (!SetMfdSharedBurstExposureTolerance(
                            setId, tolerance, &error)) {
                        m_MultiFrameWorkspaceStatusText = error;
                    }
                };
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped(
                "Frames outside this fitted span stay in the project but are excluded from Burst. Use Bracket Merge/HDR for them.");
            if (invalidBurst) {
                ImGui::TextDisabled(
                    "Shared Burst defaults become editable after the required first reprocess succeeds.");
            }
        }
    } else if (
        m_MultiFrameWorkspaceSelection ==
            MultiFrameWorkspaceSelection::Fusion) {
        if (captureSet) {
            ImGui::TextUnformatted(
                selectedMeasurementNode && !selectedMeasurementNode->title.empty()
                    ? selectedMeasurementNode->title.c_str()
                    : "Processing Node");
            ImGui::TextWrapped(
                "This authored node consumes calibrated RAW measurements. Processing remains neutral until the authoritative Output succeeds; embedded originals are unchanged.");
            ImGui::Spacing();
            ImGui::Text("Enabled captures: %llu",
                static_cast<unsigned long long>(enabledFrameCount));
            if (selectedMeasurementNode) {
                ImGui::Text("Policy: %s",
                    selectedMeasurementNode->settings.value(
                        "profile", std::string("custom")).c_str());
                ImGui::Text("Evidence: %s",
                    selectedMeasurementNode->settings.value(
                        "evidencePolicy",
                        std::string("disjoint-original-evidence-v1")).c_str());
            }
            if (graphRequiresDerivedChaining) {
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "This branch consumes a derived RAW estimate. The graph executor preserves its variance, validity, clipping, and original-frame lineage instead of flattening it into a pretend independent capture.");
            } else if (enabledFrameCount < 2u) {
                ImGui::TextDisabled(
                    "These two current processors each require at least two enabled captures.");
            } else {
                ImGui::TextDisabled(
                    "Process Output schedules only nodes that reach Output and adopts the result atomically.");
            }
        } else {
            ImGui::TextUnformatted(
                selectedMeasurementNode && !selectedMeasurementNode->title.empty()
                    ? selectedMeasurementNode->title.c_str()
                    : "MultiFrame Fusion");
            ImGui::TextDisabled("Goal: %s", ObjectiveLabel(sourceSet->operationIntent));
            ImGui::TextWrapped("%s", ExecutionAdapterLabel(sourceSet->operationIntent));
            if (denoise) {
                ImGui::TextWrapped(
                    "Static Maximum strongly averages already-safe aligned measurements without spatial blur or HDR highlight replacement.");
            }
            ImGui::Spacing();
            ImGui::Text("Enabled captures: %llu",
                static_cast<unsigned long long>(enabledFrameCount));
            ImGui::Text("Input revision: %llu",
                static_cast<unsigned long long>(inputRevision));
            ImGui::Text("Output: %s", currentResult ? "current" : "not current");
            ImGui::Spacing();
            ImGui::BeginDisabled(
                busy || enabledFrameCount < 2u ||
                !executionPlan.valid ||
                !executionPlan.executableWithCurrentAdapters ||
                (!hdr && !denoise));
            if (ImGui::Button(currentResult ? "Reprocess fusion" : "Process fusion")) {
                const std::string setId = sourceSet->sourceSetId;
                deferredAction = [this, setId]() {
                    std::string error;
                    const bool started =
                        StartMultiFrameGraphProcessing(setId, &error);
                    m_MultiFrameWorkspaceStatusText = started
                        ? "Processing queued."
                        : (error.empty() ? "Processing could not start." : error);
                };
            }
            ImGui::EndDisabled();
            if (invalidBurst) {
                ImGui::TextColored(
                    ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg),
                    "Reprocess required with Shared Burst V1.");
                ImGui::TextWrapped(
                    "Legacy fused pixels and covers are not reused. Processing reads the original embedded captures and preserves the saved post-Burst RAW recipe.");
            }
            ImGui::Spacing();
            ImGui::SeparatorText("Latest evidence");
            if (hdr && m_HdrProcessingReport &&
                m_HdrProcessingReport->sourceSetId == sourceSet->sourceSetId) {
                ImGui::Text("Exposure span: %.2f EV", m_HdrProcessingReport->exposureSpanEv);
                ImGui::Text("Effective samples: %.2f", m_HdrProcessingReport->meanEffectiveSamples);
                ImGui::Text("Backend: %s",
                    m_HdrProcessingReport->executionBackend.empty()
                        ? "CPU reference"
                        : m_HdrProcessingReport->executionBackend.c_str());
                ImGui::Text("CFA repairs: %llu",
                    static_cast<unsigned long long>(
                        m_HdrProcessingReport->colorCoherentRepairPixelCount));
                for (const std::string& warning : m_HdrProcessingReport->warnings) {
                    ImGui::BulletText("%s", warning.c_str());
                }
            } else if (denoise && m_MfdExperimentalProcessingReport &&
                       m_MfdExperimentalProcessingReport->sourceSetId ==
                           sourceSet->sourceSetId) {
                if (!m_MfdExperimentalProcessingReport->message.empty()) {
                    ImGui::TextWrapped(
                        "%s",
                        m_MfdExperimentalProcessingReport->message.c_str());
                    ImGui::Spacing();
                }
                ImGui::Text("Alternates accepted: %llu/%llu",
                    static_cast<unsigned long long>(
                        m_MfdExperimentalProcessingReport->acceptedAlternateCount),
                    static_cast<unsigned long long>(
                        m_MfdExperimentalProcessingReport->compatibleAlternateCount));
                ImGui::Text("Pixels using alternates: %.1f%%",
                    m_MfdExperimentalProcessingReport->contributingPixelFraction * 100.0);
                ImGui::Text("Effective samples: %.2f",
                    m_MfdExperimentalProcessingReport->meanEffectiveSampleCount);
                ImGui::Text("Backend: %s",
                    m_MfdExperimentalProcessingReport->executionBackend.empty()
                        ? "CPU reference"
                        : m_MfdExperimentalProcessingReport
                              ->executionBackend.c_str());
                ImGui::Text("Registration: %s",
                    m_MfdExperimentalProcessingReport
                            ->registrationBackend.empty()
                        ? "CPU reference"
                        : m_MfdExperimentalProcessingReport
                              ->registrationBackend.c_str());
                if (m_MfdExperimentalProcessingReport
                        ->registrationGpuScoredCandidateCount > 0u) {
                    ImGui::TextDisabled(
                        "GPU-scored candidates: %llu in %u dispatches",
                        static_cast<unsigned long long>(
                            m_MfdExperimentalProcessingReport
                                ->registrationGpuScoredCandidateCount),
                        m_MfdExperimentalProcessingReport
                            ->registrationGpuDispatchCount);
                }
                if (!m_MfdExperimentalProcessingReport
                         ->registrationGpuFallbackReason.empty()) {
                    ImGui::TextWrapped("Registration GPU fallback: %s",
                        m_MfdExperimentalProcessingReport
                            ->registrationGpuFallbackReason.c_str());
                }
                if (!m_MfdExperimentalProcessingReport
                         ->gpuFallbackReason.empty()) {
                    ImGui::TextWrapped("GPU fallback: %s",
                        m_MfdExperimentalProcessingReport
                            ->gpuFallbackReason.c_str());
                }
                ImGui::Text("Exposure group: %llu selected, %llu excluded",
                    static_cast<unsigned long long>(
                        m_MfdExperimentalProcessingReport->exposureGroupedCaptureCount),
                    static_cast<unsigned long long>(
                        m_MfdExperimentalProcessingReport->exposureExcludedCaptureCount));
                ImGui::Text("Usable captures: %llu of %llu selected",
                    static_cast<unsigned long long>(
                        m_MfdExperimentalProcessingReport->acceptedAlternateCount + 1u),
                    static_cast<unsigned long long>(
                        m_MfdExperimentalProcessingReport->selectedCaptureCount));
                ImGui::Text("Robust evidence retained: %.1f%%",
                    m_MfdExperimentalProcessingReport->meanRobustAttenuation * 100.0);
                ImGui::Text("Model-only independent reduction: %.2fx",
                    m_MfdExperimentalProcessingReport->predictedIndependentNoiseReduction);
                if (!m_MfdExperimentalProcessingReport
                         ->independentNoiseReductionClaimQualified) {
                    ImGui::TextWrapped(
                        "Correlation is not yet calibrated, so selected frame count is not claimed as independent information.");
                }
                if (!m_MfdExperimentalProcessingReport->frames.empty()) {
                    ImGuiTreeNodeFlags evidenceFlags =
                        ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (m_MfdExperimentalProcessingReport
                            ->acceptedAlternateCount == 0u) {
                        evidenceFlags |= ImGuiTreeNodeFlags_DefaultOpen;
                    }
                    if (ImGui::TreeNodeEx(
                            "Frame decisions##shared-burst-evidence",
                            evidenceFlags)) {
                        for (const MfdExperimentalFrameReport& frame :
                             m_MfdExperimentalProcessingReport->frames) {
                            const char* state = frame.reference
                                ? "reference"
                                : (frame.acceptedForFusion
                                    ? "included"
                                    : "rejected");
                            ImGui::Bullet();
                            ImGui::SameLine();
                            ImGui::TextWrapped(
                                "%s (%s): %s",
                                frame.label.c_str(),
                                state,
                                frame.message.empty()
                                    ? "No processor detail was reported."
                                    : frame.message.c_str());
                        }
                        ImGui::TreePop();
                    }
                }
            } else {
                ImGui::TextDisabled("Process once to populate contribution evidence.");
            }
        }
    } else if (
        m_MultiFrameWorkspaceSelection ==
            MultiFrameWorkspaceSelection::Publish) {
        ImGui::TextUnformatted("Virtual Bayer");
        ImGui::TextWrapped(
            "Signed, positive-overrange, anchor-relative linear sensor data. "
            "No hidden exposure, tone, local contrast, or display fit is added here.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Output is the single authoritative MultiFrame publication binding. Its connected RAW estimate is adopted atomically and then routes to RAW automatically.");
        ImGui::Spacing();
        ImGui::Text("State: %s", currentResult ? "published and current" : "not current");
        ImGui::BeginDisabled(!currentResult);
        if (ImGui::Button("Continue in RAW", ImVec2(-1.0f, 32.0f))) {
            m_RawWorkspaceLabUi.activeTool = RawLabTool::Light;
            RequestOpenRawLabTab();
        }
        if (ImGui::Button("Continue in Graph", ImVec2(-1.0f, 32.0f))) {
            RequestOpenEditorTab();
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextUnformatted("Capture Set");
        ImGui::TextWrapped(
            "This node owns immutable membership and capture metadata. "
            "Excluding a frame changes processing input, not the embedded original.");
        ImGui::Spacing();
        ImGui::Text("Total captures: %llu",
            static_cast<unsigned long long>(sourceSet->frames.size()));
        ImGui::Text("Enabled captures: %llu",
            static_cast<unsigned long long>(enabledFrameCount));
        ImGui::Text("Reference: %s", sourceSet->referenceFrameId.c_str());
        ImGui::Spacing();
        ImGui::SeparatorText("Captures");
        for (const SourceSetFrame& frame : sourceSet->frames) {
            const EmbeddedAssetRecord* asset = Stack::Project::FindEmbeddedAsset(
                snapshot, frame.assetId);
            const RawCaptureCompatibilitySummary capture = CaptureSummary(asset);
            ImGui::PushID(frame.frameId.c_str());
            const bool selected =
                m_MultiFrameWorkspaceSelection == MultiFrameWorkspaceSelection::Frame &&
                m_MultiFrameWorkspaceSelectedFrameId == frame.frameId;
            if (ImGui::Selectable(FrameLabel(frame, asset).c_str(), selected)) {
                m_MultiFrameWorkspaceSelection = MultiFrameWorkspaceSelection::Frame;
                m_MultiFrameWorkspaceSelectedFrameId = frame.frameId;
            }
            ImGui::TextDisabled(
                "%s%s",
                CaptureMetadataLabel(capture).c_str(),
                frame.enabled ? "" : "  excluded");
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::TextDisabled(
            captureSet
                ? "Each embedded RAW appears as its own graph node; processing topology is authored manually."
                : "The graph and processor settings remain part of this capture set's project lineage.");
        ImGui::Spacing();
        if (ImGui::Button("New Capture Set", ImVec2(-1.0f, 30.0f))) {
            deferredAction = [this]() {
                BeginMultiFrameCaptureSetGallerySelection(true);
                RequestOpenRawLabTab();
            };
        }
    }

    if (busy) {
        std::shared_ptr<MfdExperimentalProcessingProgressState> progress =
            IsMultiFrameGraphProcessingBusy()
                ? m_MultiFrameGraphProcessingProgress
                : (hdr ? m_HdrProcessingProgress
                       : m_MfdExperimentalProcessingProgress);
        if (progress) {
            std::lock_guard<std::mutex> lock(progress->mutex);
            ImGui::Spacing();
            ImGui::SeparatorText("Processing");
            ImGui::ProgressBar(
                static_cast<float>(progress->overallFraction),
                ImVec2(-1.0f, 0.0f));
            ImGui::TextWrapped("%s", progress->stageLabel.c_str());
            ImGui::TextDisabled("%s", progress->message.c_str());
            const auto now = std::chrono::steady_clock::now();
            const double elapsedSeconds =
                std::chrono::duration<double>(now - progress->startedAt).count();
            const double inactiveSeconds =
                std::chrono::duration<double>(now - progress->lastAdvancedAt).count();
            ImGui::TextDisabled(
                "Stage %.1f%%  |  elapsed %.1f min  |  last update %.1f s ago",
                100.0 * std::clamp(progress->stageFraction, 0.0, 1.0),
                elapsedSeconds / 60.0,
                std::max(0.0, inactiveSeconds));
        }
    }
    const std::string& processorStatus =
        !m_MultiFrameGraphProcessingStatusText.empty()
            ? m_MultiFrameGraphProcessingStatusText
            : (hdr ? m_HdrProcessingStatusText
                   : m_MfdExperimentalProcessingStatusText);
    if (!processorStatus.empty() || !m_MultiFrameWorkspaceStatusText.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Status");
        if (!m_MultiFrameWorkspaceStatusText.empty()) {
            ImGui::TextWrapped("%s", m_MultiFrameWorkspaceStatusText.c_str());
        }
        if (!processorStatus.empty()) {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", processorStatus.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    if (deferredAction) {
        deferredAction();
    }
    RenderMultiFrameRawLabCreationPopup();
    RenderRawWorkspaceLifecyclePopups();
}
