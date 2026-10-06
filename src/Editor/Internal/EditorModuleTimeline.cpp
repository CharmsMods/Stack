#include "Editor/EditorModule.h"

#include "Editor/Timeline/TimelinePlayback.h"
#include "Editor/Timeline/TimelinePersistence.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr float kTimelineDefaultHeight = 220.0f;
constexpr float kTimelineMinOpenHeight = 140.0f;
constexpr float kTimelineMaxOpenHeight = 420.0f;
constexpr float kTimelineMinimumWorkspaceHeight = 180.0f;
constexpr float kTimelineResizeGripHeight = 8.0f;
constexpr float kTimelineLabelColumnWidth = 220.0f;
constexpr float kTimelineRowHeight = 30.0f;
constexpr float kTimelineNodeRowHeight = 27.0f;
constexpr float kTimelinePropertyRowHeight = 24.0f;
constexpr float kTimelineHeaderHeight = 66.0f;
constexpr float kTimelineRulerHeight = 34.0f;

float ClampTimelineTargetHeight(float targetHeight, float workspaceHeight) {
    const float maxHeight = std::min(kTimelineMaxOpenHeight, std::max(0.0f, workspaceHeight - kTimelineMinimumWorkspaceHeight));
    if (maxHeight <= 1.0f) {
        return 0.0f;
    }
    const float minHeight = std::min(kTimelineMinOpenHeight, maxHeight);
    return std::clamp(targetHeight, minHeight, maxHeight);
}

int FrameFromMouseX(float mouseX, float trackMinX, float trackWidth, int durationFrames) {
    if (durationFrames <= 1 || trackWidth <= 1.0f) {
        return 0;
    }
    const float t = std::clamp((mouseX - trackMinX) / trackWidth, 0.0f, 1.0f);
    return static_cast<int>(std::round(t * static_cast<float>(durationFrames - 1)));
}

float XFromFrame(int frame, float trackMinX, float trackWidth, int durationFrames) {
    if (durationFrames <= 1 || trackWidth <= 1.0f) {
        return trackMinX;
    }
    const float t = std::clamp(
        static_cast<float>(frame) / static_cast<float>(durationFrames - 1),
        0.0f,
        1.0f);
    return trackMinX + t * trackWidth;
}

ImU32 WithAlpha(ImVec4 color, float alpha) {
    color.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

void DrawClippedText(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    ImU32 color,
    const std::string& text) {
    ImGui::PushClipRect(min, max, true);
    drawList->AddText(ImVec2(min.x, min.y), color, text.c_str());
    ImGui::PopClipRect();
}

const Stack::Timeline::AnimatableParameterDefinition* FindDefinitionForTarget(
    const std::vector<Stack::Timeline::AnimatableParameterDefinition>& definitions,
    const Stack::Timeline::AnimatableParameterTarget& target) {
    auto it = std::find_if(
        definitions.begin(),
        definitions.end(),
        [&target](const Stack::Timeline::AnimatableParameterDefinition& definition) {
            return Stack::Timeline::SameTarget(definition.target, target);
        });
    return it == definitions.end() ? nullptr : &(*it);
}

std::string BuildParameterComboLabel(const Stack::Timeline::AnimatableParameterDefinition& definition) {
    return definition.nodeLabel + " / " + definition.parameterLabel;
}

std::string BuildTimelineNodeLabel(const EditorNodeGraph::Node& node) {
    if (!node.title.empty()) {
        return node.title;
    }

    if (node.kind == EditorNodeGraph::NodeKind::Layer) {
        if (const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(node.layerType)) {
            return descriptor->displayName ? descriptor->displayName : "Layer";
        }
    }

    return "Node " + std::to_string(node.id);
}

void DrawTimelineKeyframeDiamond(
    ImDrawList* drawList,
    const ImVec2& center,
    float radius,
    ImU32 color) {
    drawList->AddQuadFilled(
        ImVec2(center.x, center.y - radius),
        ImVec2(center.x + radius, center.y),
        ImVec2(center.x, center.y + radius),
        ImVec2(center.x - radius, center.y),
        color);
}

void TimelineTooltip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}

bool ContainsInt(const std::vector<int>& values, int value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void ToggleInt(std::vector<int>& values, int value) {
    auto it = std::find(values.begin(), values.end(), value);
    if (it == values.end()) {
        values.push_back(value);
    } else {
        values.erase(it);
    }
}

bool TrackTargetsNode(const Stack::Timeline::TimelineTrack& track, int nodeId) {
    return track.target.nodeId == nodeId;
}

bool TrackTargetsParameter(
    const Stack::Timeline::TimelineTrack& track,
    const Stack::Timeline::AnimatableParameterTarget& target) {
    return Stack::Timeline::SameTarget(track.target, target);
}

bool TryReadJsonFloat(const nlohmann::json& value, const std::string& key, float& outValue) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number()) {
        return false;
    }

    outValue = it->get<float>();
    return true;
}

} // namespace

Stack::Timeline::TimelineAnimationState& EditorModule::GetGraphAnimation() {
    return IsEditingRawLayerMaskGraph() ? m_RawLayerMaskWorkspace->animation : m_Project->timeline;
}
const Stack::Timeline::TimelineAnimationState& EditorModule::GetGraphAnimation() const {
    return IsEditingRawLayerMaskGraph() ? m_RawLayerMaskWorkspace->animation : m_Project->timeline;
}
std::string EditorModule::GetEditedGraphId() const {
    return IsEditingRawLayerMaskGraph() ? m_RawLayerMaskWorkspace->layerId : "project";
}
std::vector<EditorModule::CachedCompositeChainState> EditorModule::BuildTimelineGraphChains() const {
    std::vector<CachedCompositeChainState> chains;
    for (const auto& info : GetNodeGraph().GetCompletedChains()) {
        CachedCompositeChainState chain; chain.info = info;
        const auto* output = GetNodeGraph().FindNode(info.outputNodeId);
        chain.label = output ? output->title : "Output";
        chains.push_back(std::move(chain));
    }
    return chains;
}

std::vector<Stack::Timeline::AnimatableParameterDefinition> EditorModule::BuildTimelineAnimatableParametersForChain(
    const EditorNodeGraph::CompletedChainInfo& chain) const {
    std::vector<Stack::Timeline::AnimatableParameterDefinition> parameters;
    for (int nodeId : chain.nodeIds) {
        const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
        if (!node) {
            continue;
        }

        const LayerBase* layer = nullptr;
        if (node->kind == EditorNodeGraph::NodeKind::Layer &&
            node->layerIndex >= 0 &&
            node->layerIndex < static_cast<int>(GetLayers().size()) &&
            GetLayers()[node->layerIndex]) {
            layer = GetLayers()[node->layerIndex].get();
        }

        std::vector<Stack::Timeline::AnimatableParameterDefinition> nodeParameters =
            Stack::Timeline::CollectAnimatableParametersForNode(*node, layer, GetEditedGraphId());
        parameters.insert(
            parameters.end(),
            std::make_move_iterator(nodeParameters.begin()),
            std::make_move_iterator(nodeParameters.end()));
    }
    return parameters;
}

bool EditorModule::EnsureTimelineSelectedParameter(
    const std::vector<Stack::Timeline::AnimatableParameterDefinition>& parameters) {
    if (parameters.empty()) {
        m_TimelineUi.selectedParameterTarget = {};
        return false;
    }

    if (FindDefinitionForTarget(parameters, m_TimelineUi.selectedParameterTarget)) {
        return true;
    }

    m_TimelineUi.selectedParameterTarget = parameters.front().target;
    return true;
}

bool EditorModule::AddTimelineKeyframeForSelectedParameter() {
    if (!Stack::Timeline::IsValidTarget(m_TimelineUi.selectedParameterTarget)) {
        return false;
    }

    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(m_TimelineUi.selectedParameterTarget.nodeId);
    if (!node) {
        return false;
    }

    const LayerBase* layer = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(GetLayers().size()) &&
        GetLayers()[node->layerIndex]) {
        layer = GetLayers()[node->layerIndex].get();
    }

    float value = 0.0f;
    if (!Stack::Timeline::TryReadAnimatableParameterValue(
            *node,
            layer,
            m_TimelineUi.selectedParameterTarget.parameterId,
            value)) {
        return false;
    }

    Stack::Timeline::SetOrReplaceKeyframe(
        GetGraphAnimation(),
        m_TimelineUi.selectedParameterTarget,
        m_TimelineUi.currentFrame,
        value);
    MarkGraphEdited();
    ClearTimelineLiveEditPreview();
    MarkTimelineFrameRenderDirty();
    return true;
}

nlohmann::json EditorModule::SerializeTimelinePersistence() const {
    Stack::Timeline::TimelineDocumentState document;
    document.currentFrame = m_TimelineUi.currentFrame;
    document.durationFrames = m_TimelineUi.durationFrames;
    document.framesPerSecond = m_TimelineUi.framesPerSecond;
    document.animation = m_Project->timeline;
    return Stack::Timeline::SerializeTimelineDocument(document);
}

void EditorModule::DeserializeTimelinePersistence(const nlohmann::json& pipelineData) {
    m_Project->timeline = {};
    m_TimelineUi.selectedParameterTarget = {};
    m_TimelineUi.playing = false;
    m_TimelineUi.loopPlayback = true;
    m_TimelineUi.settingsPopupOpen = false;
    m_TimelineUi.playbackFrameAccumulator = 0.0;
    ClearTimelineLiveEditPreview();

    if (!pipelineData.is_object()) {
        return;
    }

    const nlohmann::json timeline = pipelineData.value("editorTimeline", nlohmann::json::object());
    const Stack::Timeline::TimelineDocumentState document =
        Stack::Timeline::DeserializeTimelineDocument(
            timeline,
            [this](const Stack::Timeline::AnimatableParameterTarget& target) {
                const EditorNodeGraph::Node* node = m_Project->graph.FindNode(target.nodeId);
                if (!node) {
                    return false;
                }

                const LayerBase* layer = nullptr;
                if (node->kind == EditorNodeGraph::NodeKind::Layer &&
                    node->layerIndex >= 0 &&
                    node->layerIndex < static_cast<int>(m_Project->layers.size()) &&
                    m_Project->layers[node->layerIndex]) {
                    layer = m_Project->layers[node->layerIndex].get();
                }

                const std::vector<Stack::Timeline::AnimatableParameterDefinition> definitions =
                    Stack::Timeline::CollectAnimatableParametersForNode(*node, layer);
                return std::any_of(
                    definitions.begin(),
                    definitions.end(),
                    [&target](const Stack::Timeline::AnimatableParameterDefinition& definition) {
                        return Stack::Timeline::SameTarget(definition.target, target);
                    });
            });

    m_TimelineUi.currentFrame = document.currentFrame;
    m_TimelineUi.durationFrames = document.durationFrames;
    m_TimelineUi.framesPerSecond = document.framesPerSecond;
    m_Project->timeline = document.animation;
}

void EditorModule::SetTimelineFrame(int frame) {
    const int clampedFrame = Stack::Timeline::ClampTimelineFrame(frame, m_TimelineUi.durationFrames);
    if (clampedFrame == m_TimelineUi.currentFrame) {
        return;
    }

    ClearTimelineLiveEditPreview();
    m_TimelineUi.currentFrame = clampedFrame;
    MarkTimelineFrameRenderDirty();
}

void EditorModule::StepTimelineFrame(int frameDelta) {
    m_TimelineUi.playing = false;
    m_TimelineUi.playbackFrameAccumulator = 0.0;
    const int finalFrame = std::max(0, m_TimelineUi.durationFrames - 1);
    const int wrapEndFrame = m_TimelineUi.loopPlayback
        ? ResolveTimelinePlaybackEndFrame()
        : finalFrame;
    SetTimelineFrame(Stack::Timeline::ResolveTimelineStepFrame(
        m_TimelineUi.currentFrame,
        frameDelta,
        m_TimelineUi.durationFrames,
        wrapEndFrame));
}

void EditorModule::ToggleTimelinePlayback() {
    m_TimelineUi.playing = !m_TimelineUi.playing;
    m_TimelineUi.playbackFrameAccumulator = 0.0;
    if (m_TimelineUi.playing) {
        ClearTimelineLiveEditPreview();
    }
    if (m_TimelineUi.playing &&
        m_TimelineUi.loopPlayback &&
        m_TimelineUi.currentFrame > ResolveTimelinePlaybackEndFrame()) {
        SetTimelineFrame(0);
    }
}

void EditorModule::StopTimelinePlayback(bool resetToStart) {
    m_TimelineUi.playing = false;
    m_TimelineUi.playbackFrameAccumulator = 0.0;
    if (resetToStart) {
        SetTimelineFrame(0);
    }
}

int EditorModule::ResolveTimelinePlaybackEndFrame() const {
    const int finalFrame = std::max(0, m_TimelineUi.durationFrames - 1);
    const int lastKeyframeFrame = Stack::Timeline::FindLastTimelineKeyframeFrame(GetGraphAnimation());
    const std::size_t distinctKeyframeFrames =
        Stack::Timeline::CountDistinctTimelineKeyframeFrames(GetGraphAnimation());
    if (distinctKeyframeFrames >= 2 && lastKeyframeFrame > 0) {
        return std::clamp(lastKeyframeFrame, 0, finalFrame);
    }
    return finalFrame;
}

void EditorModule::MarkTimelineFrameRenderDirty() {
    MarkRenderRefreshDirty();

    if (IsEditingRawLayerMaskGraph() || GetGraphAnimation().tracks.empty()) {
        return;
    }

    const auto timelineChains = BuildTimelineGraphChains();
    std::vector<int> affectedOutputNodeIds;
    for (const CachedCompositeChainState& chain : timelineChains) {
        const bool affected = std::any_of(
            GetGraphAnimation().tracks.begin(),
            GetGraphAnimation().tracks.end(),
            [&chain](const Stack::Timeline::TimelineTrack& track) {
                return Stack::Timeline::TrackAffectsCompletedChain(track, chain.info);
            });
        if (affected) {
            affectedOutputNodeIds.push_back(chain.info.outputNodeId);
        }
    }

    if (!affectedOutputNodeIds.empty()) {
        MarkCompositeOutputsDirty(affectedOutputNodeIds);
    }
}

void EditorModule::ClearTimelineLiveEditPreview() {
    m_TimelineUi.liveEditPreviewFrame = -1;
    m_TimelineUi.liveEditPreviewTargets.clear();
}

void EditorModule::AddTimelineLiveEditPreviewTarget(const Stack::Timeline::AnimatableParameterTarget& target) {
    if (!Stack::Timeline::IsValidTarget(target)) {
        return;
    }

    if (m_TimelineUi.liveEditPreviewFrame != m_TimelineUi.currentFrame) {
        m_TimelineUi.liveEditPreviewTargets.clear();
        m_TimelineUi.liveEditPreviewFrame = m_TimelineUi.currentFrame;
    }

    const auto it = std::find_if(
        m_TimelineUi.liveEditPreviewTargets.begin(),
        m_TimelineUi.liveEditPreviewTargets.end(),
        [&target](const Stack::Timeline::AnimatableParameterTarget& existing) {
            return Stack::Timeline::SameTarget(existing, target);
        });
    if (it == m_TimelineUi.liveEditPreviewTargets.end()) {
        m_TimelineUi.liveEditPreviewTargets.push_back(target);
    }
}

bool EditorModule::UpdateTimelineExistingKeyframesForLayerEdit(
    const EditorNodeGraph::Node& node,
    const nlohmann::json& before,
    const nlohmann::json& after) {
    if (!m_TimelineUi.open ||
        GetGraphAnimation().tracks.empty() ||
        node.kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }

    bool updatedAnyKeyframe = false;
    const std::vector<Stack::Timeline::AnimatableParameterDefinition> definitions =
        Stack::Timeline::CollectAnimatableParametersForNode(node, nullptr, GetEditedGraphId());
    for (const Stack::Timeline::AnimatableParameterDefinition& definition : definitions) {
        if (definition.storageKey.empty()) {
            continue;
        }

        float beforeValue = definition.defaultValue;
        float afterValue = definition.defaultValue;
        const bool hasBefore = TryReadJsonFloat(before, definition.storageKey, beforeValue);
        if (!TryReadJsonFloat(after, definition.storageKey, afterValue)) {
            continue;
        }
        if (hasBefore && std::abs(beforeValue - afterValue) <= 0.000001f) {
            continue;
        }

        const bool updatedKeyframe = Stack::Timeline::UpdateExistingKeyframeValue(
            GetGraphAnimation(),
            definition.target,
            m_TimelineUi.currentFrame,
            afterValue);
        if (updatedKeyframe) {
            updatedAnyKeyframe = true;
        } else {
            AddTimelineLiveEditPreviewTarget(definition.target);
        }
    }

    if (updatedAnyKeyframe) {
        MarkGraphEdited();
        MarkTimelineFrameRenderDirty();
    }
    return updatedAnyKeyframe;
}

float EditorModule::UpdateTimelinePanelHeight(float workspaceHeight) {
    if (m_TimelineUi.targetHeight <= 0.0f) {
        m_TimelineUi.targetHeight = kTimelineDefaultHeight;
    }

    const float clampedTargetHeight = ClampTimelineTargetHeight(m_TimelineUi.targetHeight, workspaceHeight);
    if (clampedTargetHeight <= 1.0f) {
        m_TimelineUi.currentHeight = 0.0f;
        return 0.0f;
    }

    m_TimelineUi.targetHeight = clampedTargetHeight;
    const float desiredHeight = m_TimelineUi.open ? m_TimelineUi.targetHeight : 0.0f;
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    const float blend = std::clamp(dt * 14.0f, 0.0f, 1.0f);
    m_TimelineUi.currentHeight += (desiredHeight - m_TimelineUi.currentHeight) * blend;
    if (std::abs(m_TimelineUi.currentHeight - desiredHeight) < 0.5f) {
        m_TimelineUi.currentHeight = desiredHeight;
    }
    return m_TimelineUi.currentHeight <= 1.0f ? 0.0f : m_TimelineUi.currentHeight;
}

void EditorModule::RenderTimelinePanel(const ImVec2& workspacePos, const ImVec2& workspaceSize, float timelineHeight) {
    if (timelineHeight <= 1.0f || workspaceSize.x <= 1.0f) {
        return;
    }

    m_TimelineUi.durationFrames = std::max(1, m_TimelineUi.durationFrames);
    m_TimelineUi.framesPerSecond = std::clamp(m_TimelineUi.framesPerSecond, 1, 240);
    const int normalizedCurrentFrame =
        Stack::Timeline::ClampTimelineFrame(m_TimelineUi.currentFrame, m_TimelineUi.durationFrames);
    if (normalizedCurrentFrame != m_TimelineUi.currentFrame) {
        ClearTimelineLiveEditPreview();
        m_TimelineUi.currentFrame = normalizedCurrentFrame;
    }
    if (m_TimelineUi.playing) {
        const int playbackEndFrame = ResolveTimelinePlaybackEndFrame();
        const int playbackDurationFrames = m_TimelineUi.loopPlayback
            ? playbackEndFrame + 1
            : m_TimelineUi.durationFrames;
        if (m_TimelineUi.loopPlayback && m_TimelineUi.currentFrame > playbackEndFrame) {
            SetTimelineFrame(0);
        }
        Stack::Timeline::TimelinePlaybackAdvanceResult playback =
            Stack::Timeline::AdvanceTimelinePlayback(
                m_TimelineUi.currentFrame,
                playbackDurationFrames,
                m_TimelineUi.framesPerSecond,
                ImGui::GetIO().DeltaTime,
                m_TimelineUi.loopPlayback,
                m_TimelineUi.playbackFrameAccumulator);
        if (playback.frameChanged) {
            SetTimelineFrame(playback.frame);
        }
        if (playback.shouldStop) {
            m_TimelineUi.playing = false;
        }
    } else {
        m_TimelineUi.playbackFrameAccumulator = 0.0;
    }
    const int frameBeforeInteraction = m_TimelineUi.currentFrame;

    const auto timelineChains = BuildTimelineGraphChains();
    const CachedCompositeChainState* selectedChain = nullptr;
    for (const CachedCompositeChainState& chain : timelineChains) {
        if (chain.info.outputNodeId == m_TimelineUi.selectedOutputNodeId) {
            selectedChain = &chain;
            break;
        }
    }
    if (!selectedChain) {
        m_TimelineUi.selectedOutputNodeId = -1;
        m_TimelineUi.selectedParameterTarget = {};
    }
    std::vector<Stack::Timeline::AnimatableParameterDefinition> selectedParameters;
    if (selectedChain) {
        selectedParameters = BuildTimelineAnimatableParametersForChain(selectedChain->info);
        EnsureTimelineSelectedParameter(selectedParameters);
    }

    const ImVec2 panelPos(workspacePos.x, workspacePos.y + workspaceSize.y - timelineHeight);
    const ImVec2 panelSize(workspaceSize.x, timelineHeight);
    const ImVec4 workspaceColor = GetWorkspaceBaseColor();
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 disabledTextColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImVec4 accentColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const ImVec4 frameBgColor = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 headerHoveredColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
    const ImVec4 headerActiveColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);

    ImGui::SetCursorScreenPos(panelPos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, workspaceColor);
    ImGui::BeginChild(
        "EditorTimelinePane",
        panelSize,
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 childMin = ImGui::GetWindowPos();
    const ImVec2 childMax(childMin.x + panelSize.x, childMin.y + panelSize.y);
    drawList->AddLine(childMin, ImVec2(childMax.x, childMin.y), WithAlpha(accentColor, 0.35f), 1.0f);

    ImGui::InvisibleButton("TimelineResizeGrip", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), kTimelineResizeGripHeight));
    const bool resizeHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (resizeHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive()) {
        m_TimelineUi.open = true;
        m_TimelineUi.targetHeight = ClampTimelineTargetHeight(
            m_TimelineUi.targetHeight - ImGui::GetIO().MouseDelta.y,
            workspaceSize.y);
    }

    const ImVec2 gripMin = ImGui::GetItemRectMin();
    const ImVec2 gripMax = ImGui::GetItemRectMax();
    const ImU32 gripColor = WithAlpha(accentColor, resizeHovered ? 0.7f : 0.3f);
    drawList->AddLine(
        ImVec2(gripMin.x + 12.0f, (gripMin.y + gripMax.y) * 0.5f),
        ImVec2(gripMax.x - 12.0f, (gripMin.y + gripMax.y) * 0.5f),
        gripColor,
        1.0f);

    const Stack::Timeline::AnimatableParameterDefinition* selectedDefinition =
        FindDefinitionForTarget(selectedParameters, m_TimelineUi.selectedParameterTarget);
    const std::string selectedParameterLabel = selectedDefinition
        ? BuildParameterComboLabel(*selectedDefinition)
        : std::string("No animatable target");

    ImGui::BeginChild(
        "TimelineHeader",
        ImVec2(0.0f, kTimelineHeaderHeight),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    if (ImGui::Button(m_TimelineUi.playing ? "||##TimelinePlayback" : ">##TimelinePlayback", ImVec2(30.0f, 0.0f))) {
        ToggleTimelinePlayback();
    }
    TimelineTooltip(m_TimelineUi.playing ? "Pause timeline" : "Play timeline");
    ImGui::SameLine();
    if (ImGui::Button("[]##TimelinePlaybackStop", ImVec2(30.0f, 0.0f))) {
        StopTimelinePlayback(true);
    }
    TimelineTooltip("Stop and return to frame 0");
    ImGui::SameLine();
    if (ImGui::Button("|<##TimelinePreviousFrame", ImVec2(34.0f, 0.0f))) {
        StepTimelineFrame(-1);
    }
    TimelineTooltip("Previous frame");
    ImGui::SameLine();
    if (ImGui::Button(">|##TimelineNextFrame", ImVec2(34.0f, 0.0f))) {
        StepTimelineFrame(1);
    }
    TimelineTooltip("Next frame");
    ImGui::SameLine();
    ImGui::Checkbox("##TimelineLoopPlayback", &m_TimelineUi.loopPlayback);
    TimelineTooltip("Loop playback");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(86.0f);
    if (ImGui::InputInt("Frame##TimelineFrame", &m_TimelineUi.currentFrame, 1, 10)) {
        StopTimelinePlayback(false);
        m_TimelineUi.currentFrame = Stack::Timeline::ClampTimelineFrame(m_TimelineUi.currentFrame, m_TimelineUi.durationFrames);
    }
    ImGui::SameLine();
    if (ImGui::Button("...##TimelineSettings", ImVec2(30.0f, 0.0f))) {
        m_TimelineUi.settingsPopupOpen = true;
    }
    TimelineTooltip("Timeline settings");

    const float addKeyWidth = 64.0f;
    const float targetComboWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - addKeyWidth - 8.0f);
    ImGui::SetNextItemWidth(targetComboWidth);
    ImGui::BeginDisabled(selectedParameters.empty());
    if (ImGui::BeginCombo("##TimelineKeyframeTarget", selectedParameterLabel.c_str())) {
        for (const Stack::Timeline::AnimatableParameterDefinition& definition : selectedParameters) {
            const bool selected = Stack::Timeline::SameTarget(definition.target, m_TimelineUi.selectedParameterTarget);
            const std::string label = BuildParameterComboLabel(definition);
            if (ImGui::Selectable(label.c_str(), selected)) {
                m_TimelineUi.selectedParameterTarget = definition.target;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!selectedDefinition);
    if (ImGui::Button("+ Key##TimelineAddKeyframe")) {
        AddTimelineKeyframeForSelectedParameter();
    }
    TimelineTooltip("Add keyframe for selected target at current frame");
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::EndChild();

    if (m_TimelineUi.settingsPopupOpen) {
        ImGui::OpenPopup("Timeline Settings##TimelineSettingsPopup");
        m_TimelineUi.settingsPopupOpen = false;
    }

    if (ImGui::BeginPopupModal(
            "Timeline Settings##TimelineSettingsPopup",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Current frame##TimelineSettingsFrame", &m_TimelineUi.currentFrame, 1, 10)) {
            StopTimelinePlayback(false);
            m_TimelineUi.currentFrame =
                Stack::Timeline::ClampTimelineFrame(m_TimelineUi.currentFrame, m_TimelineUi.durationFrames);
        }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Duration frames##TimelineSettingsDuration", &m_TimelineUi.durationFrames, 1, 30)) {
            m_TimelineUi.durationFrames = std::clamp(m_TimelineUi.durationFrames, 1, 100000);
            StopTimelinePlayback(false);
            m_TimelineUi.currentFrame =
                Stack::Timeline::ClampTimelineFrame(m_TimelineUi.currentFrame, m_TimelineUi.durationFrames);
        }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("FPS##TimelineSettingsFps", &m_TimelineUi.framesPerSecond, 1, 5)) {
            m_TimelineUi.playbackFrameAccumulator = 0.0;
            m_TimelineUi.framesPerSecond = std::clamp(m_TimelineUi.framesPerSecond, 1, 240);
        }

        ImGui::Checkbox("Loop playback##TimelineSettingsLoop", &m_TimelineUi.loopPlayback);

        ImGui::Spacing();
        if (ImGui::Button("Close##TimelineSettingsClose", ImVec2(96.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float labelWidth = std::min(kTimelineLabelColumnWidth, std::max(120.0f, availableWidth * 0.36f));
    const ImVec2 rulerPos = ImGui::GetCursorScreenPos();
    const ImVec2 rulerSize(std::max(1.0f, availableWidth), kTimelineRulerHeight);
    const float trackMinX = rulerPos.x + labelWidth;
    const float trackWidth = std::max(1.0f, rulerSize.x - labelWidth);
    const ImVec2 rulerTrackMin(trackMinX, rulerPos.y);
    const ImVec2 rulerTrackMax(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y);

    drawList->AddRectFilled(rulerPos, ImVec2(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y), WithAlpha(frameBgColor, 0.55f), 4.0f);
    drawList->AddLine(ImVec2(trackMinX, rulerPos.y), ImVec2(trackMinX, rulerPos.y + rulerSize.y), WithAlpha(disabledTextColor, 0.35f), 1.0f);

    ImGui::InvisibleButton("TimelineRulerScrub", rulerSize);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemActivated() || ImGui::IsItemActive()) {
        m_TimelineUi.playing = false;
        m_TimelineUi.playbackFrameAccumulator = 0.0;
        m_TimelineUi.currentFrame = FrameFromMouseX(ImGui::GetIO().MousePos.x, trackMinX, trackWidth, m_TimelineUi.durationFrames);
    }

    const int tickStep = std::max(1, m_TimelineUi.durationFrames / 12);
    for (int frame = 0; frame < m_TimelineUi.durationFrames; frame += tickStep) {
        const float x = XFromFrame(frame, trackMinX, trackWidth, m_TimelineUi.durationFrames);
        const bool major = frame == 0 || frame + tickStep >= m_TimelineUi.durationFrames || frame % (tickStep * 2) == 0;
        const float tickHeight = major ? 18.0f : 10.0f;
        drawList->AddLine(
            ImVec2(x, rulerPos.y + rulerSize.y - tickHeight),
            ImVec2(x, rulerPos.y + rulerSize.y - 4.0f),
            WithAlpha(disabledTextColor, major ? 0.55f : 0.28f),
            1.0f);
        if (major) {
            char label[32];
            std::snprintf(label, sizeof(label), "%d", frame);
            drawList->AddText(ImVec2(x + 4.0f, rulerPos.y + 6.0f), WithAlpha(disabledTextColor, 0.85f), label);
        }
    }
    if (m_TimelineUi.durationFrames > 1) {
        const int finalFrame = m_TimelineUi.durationFrames - 1;
        const float x = XFromFrame(finalFrame, trackMinX, trackWidth, m_TimelineUi.durationFrames);
        drawList->AddLine(
            ImVec2(x, rulerPos.y + rulerSize.y - 18.0f),
            ImVec2(x, rulerPos.y + rulerSize.y - 4.0f),
            WithAlpha(disabledTextColor, 0.55f),
            1.0f);
    }

    const float playheadX = XFromFrame(m_TimelineUi.currentFrame, trackMinX, trackWidth, m_TimelineUi.durationFrames);
    drawList->AddTriangleFilled(
        ImVec2(playheadX, rulerPos.y + 2.0f),
        ImVec2(playheadX - 5.0f, rulerPos.y + 10.0f),
        ImVec2(playheadX + 5.0f, rulerPos.y + 10.0f),
        WithAlpha(accentColor, 0.95f));
    drawList->AddLine(
        ImVec2(playheadX, rulerPos.y + 2.0f),
        ImVec2(playheadX, rulerTrackMax.y),
        WithAlpha(accentColor, 0.85f),
        1.5f);

    const float rowsHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("TimelineRows", ImVec2(0.0f, rowsHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImDrawList* rowsDrawList = ImGui::GetWindowDrawList();
    const ImVec2 rowsOrigin = ImGui::GetCursorScreenPos();
    const float rowsWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float rowsLabelWidth = std::min(kTimelineLabelColumnWidth, std::max(120.0f, rowsWidth * 0.36f));
    const float rowsTrackMinX = rowsOrigin.x + rowsLabelWidth;
    const float rowsTrackWidth = std::max(1.0f, rowsWidth - rowsLabelWidth);

    if (timelineChains.empty()) {
        ImGui::Dummy(ImVec2(1.0f, 8.0f));
        ImGui::TextDisabled("No completed output chains");
    } else {
        const auto collectNodeParameters =
            [this](const EditorNodeGraph::Node& node) -> std::vector<Stack::Timeline::AnimatableParameterDefinition> {
                const LayerBase* layer = nullptr;
                if (node.kind == EditorNodeGraph::NodeKind::Layer &&
                    node.layerIndex >= 0 &&
                    node.layerIndex < static_cast<int>(GetLayers().size()) &&
                    GetLayers()[node.layerIndex]) {
                    layer = GetLayers()[node.layerIndex].get();
                }
                return Stack::Timeline::CollectAnimatableParametersForNode(node, layer, GetEditedGraphId());
            };

        const auto drawRowBase =
            [&](const ImVec2& rowMin, const ImVec2& rowMax, bool selected, int rowIndex, float depthAlpha) {
                const ImVec2 trackMin(rowsTrackMinX, rowMin.y);
                const ImVec2 trackMax(rowMin.x + rowsWidth, rowMax.y);
                const ImU32 rowBg = selected
                    ? WithAlpha(headerActiveColor, 0.45f)
                    : WithAlpha(frameBgColor, rowIndex % 2 == 0 ? 0.38f * depthAlpha : 0.24f * depthAlpha);
                rowsDrawList->AddRectFilled(rowMin, rowMax, rowBg, 3.0f);
                rowsDrawList->AddRectFilled(trackMin, trackMax, WithAlpha(frameBgColor, 0.18f * depthAlpha), 3.0f);
                rowsDrawList->AddLine(trackMin, ImVec2(trackMin.x, trackMax.y), WithAlpha(disabledTextColor, 0.25f), 1.0f);

                for (int frame = 0; frame < m_TimelineUi.durationFrames; frame += tickStep) {
                    const float x = XFromFrame(frame, rowsTrackMinX, rowsTrackWidth, m_TimelineUi.durationFrames);
                    rowsDrawList->AddLine(
                        ImVec2(x, rowMin.y + 3.0f),
                        ImVec2(x, rowMax.y - 3.0f),
                        WithAlpha(disabledTextColor, 0.12f),
                        1.0f);
                }
            };

        const auto drawRowPlayhead = [&](const ImVec2& rowMin, const ImVec2& rowMax) {
            const float rowPlayheadX = XFromFrame(m_TimelineUi.currentFrame, rowsTrackMinX, rowsTrackWidth, m_TimelineUi.durationFrames);
            rowsDrawList->AddLine(
                ImVec2(rowPlayheadX, rowMin.y + 2.0f),
                ImVec2(rowPlayheadX, rowMax.y - 2.0f),
                WithAlpha(accentColor, 0.70f),
                1.2f);
        };

        const auto drawTrackKeyframes =
            [&](const auto& trackPredicate, float keyframeCenterY, float radius, float alpha) {
                for (const Stack::Timeline::TimelineTrack& track : GetGraphAnimation().tracks) {
                    if (!trackPredicate(track)) {
                        continue;
                    }

                    for (const Stack::Timeline::TimelineKeyframe& keyframe : track.keyframes) {
                        if (keyframe.frame < 0 || keyframe.frame >= m_TimelineUi.durationFrames) {
                            continue;
                        }

                        const float keyframeX = XFromFrame(keyframe.frame, rowsTrackMinX, rowsTrackWidth, m_TimelineUi.durationFrames);
                        DrawTimelineKeyframeDiamond(
                            rowsDrawList,
                            ImVec2(keyframeX, keyframeCenterY),
                            radius,
                            WithAlpha(accentColor, alpha));
                    }
                }
            };

        const auto drawExpandGlyph = [&](const ImVec2& rowMin, float centerY, bool collapsed, float indent) {
            const ImVec2 glyphPos(rowMin.x + 8.0f + indent, centerY - 7.0f);
            rowsDrawList->AddText(
                glyphPos,
                WithAlpha(disabledTextColor, 0.95f),
                collapsed ? ">" : "v");
        };

        const auto rowToggleClicked = [](const ImVec2& rowMin, float indent) {
            return ImGui::GetIO().MousePos.x <= rowMin.x + 30.0f + indent;
        };

        int rowIndex = 0;
        for (const CachedCompositeChainState& chain : timelineChains) {
            const int outputNodeId = chain.info.outputNodeId;
            const bool selected = outputNodeId == m_TimelineUi.selectedOutputNodeId;
            const bool chainCollapsed = ContainsInt(m_TimelineUi.collapsedChainOutputNodeIds, outputNodeId);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const ImVec2 rowMax(rowMin.x + rowsWidth, rowMin.y + kTimelineRowHeight);
            const ImVec2 labelMin(rowMin.x + 28.0f, rowMin.y + 7.0f);
            const ImVec2 labelMax(rowMin.x + rowsLabelWidth - 8.0f, rowMax.y - 4.0f);

            drawRowBase(rowMin, rowMax, selected, rowIndex, 1.0f);
            drawExpandGlyph(rowMin, (rowMin.y + rowMax.y) * 0.5f, chainCollapsed, 0.0f);

            const float keyframeCenterY = (rowMin.y + rowMax.y) * 0.5f;
            drawTrackKeyframes(
                [&chain](const Stack::Timeline::TimelineTrack& track) {
                    return Stack::Timeline::TrackAffectsCompletedChain(track, chain.info);
                },
                keyframeCenterY,
                4.2f,
                selected ? 0.72f : 0.48f);

            drawRowPlayhead(rowMin, rowMax);

            const std::string label = chain.label.empty() ? BuildCompositeChainLabel(chain.info) : chain.label;
            DrawClippedText(rowsDrawList, labelMin, labelMax, selected ? WithAlpha(textColor, 1.0f) : WithAlpha(textColor, 0.88f), label);

            char meta[96];
            std::snprintf(meta, sizeof(meta), "Output %d  |  %zu nodes", outputNodeId, chain.info.nodeIds.size());
            const ImVec2 metaSize = ImGui::CalcTextSize(meta);
            rowsDrawList->AddText(
                ImVec2(std::max(rowsTrackMinX + 8.0f, rowMax.x - metaSize.x - 12.0f), rowMin.y + 7.0f),
                WithAlpha(disabledTextColor, 0.85f),
                meta);

            ImGui::InvisibleButton(("##TimelineRow_" + std::to_string(outputNodeId)).c_str(), ImVec2(rowsWidth, kTimelineRowHeight));
            if (ImGui::IsItemHovered()) {
                rowsDrawList->AddRect(rowMin, rowMax, WithAlpha(headerHoveredColor, 0.65f), 3.0f);
            }
            if (ImGui::IsItemClicked()) {
                if (rowToggleClicked(rowMin, 0.0f)) {
                    ToggleInt(m_TimelineUi.collapsedChainOutputNodeIds, outputNodeId);
                } else {
                    m_TimelineUi.selectedOutputNodeId = outputNodeId;
                    m_TimelineUi.selectedParameterTarget = {};
                    SetCompositeSelectedOutputNodeId(outputNodeId);
                }
            }

            ++rowIndex;
            if (chainCollapsed) {
                continue;
            }

            for (int nodeId : chain.info.nodeIds) {
                const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
                if (!node) {
                    continue;
                }

                const std::vector<Stack::Timeline::AnimatableParameterDefinition> nodeParameters =
                    collectNodeParameters(*node);
                if (nodeParameters.empty()) {
                    continue;
                }

                const bool nodeCollapsed = ContainsInt(m_TimelineUi.collapsedNodeIds, nodeId);
                const bool nodeSelected = GetNodeGraph().GetSelectedNodeId() == nodeId &&
                    outputNodeId == m_TimelineUi.selectedOutputNodeId;
                const ImVec2 nodeRowMin = ImGui::GetCursorScreenPos();
                const ImVec2 nodeRowMax(nodeRowMin.x + rowsWidth, nodeRowMin.y + kTimelineNodeRowHeight);
                const ImVec2 nodeLabelMin(nodeRowMin.x + 44.0f, nodeRowMin.y + 5.0f);
                const ImVec2 nodeLabelMax(nodeRowMin.x + rowsLabelWidth - 8.0f, nodeRowMax.y - 3.0f);

                drawRowBase(nodeRowMin, nodeRowMax, nodeSelected, rowIndex, 0.84f);
                drawExpandGlyph(nodeRowMin, (nodeRowMin.y + nodeRowMax.y) * 0.5f, nodeCollapsed, 16.0f);
                drawTrackKeyframes(
                    [nodeId](const Stack::Timeline::TimelineTrack& track) {
                        return TrackTargetsNode(track, nodeId);
                    },
                    (nodeRowMin.y + nodeRowMax.y) * 0.5f,
                    3.8f,
                    nodeSelected ? 0.78f : 0.52f);
                drawRowPlayhead(nodeRowMin, nodeRowMax);

                DrawClippedText(
                    rowsDrawList,
                    nodeLabelMin,
                    nodeLabelMax,
                    nodeSelected ? WithAlpha(textColor, 1.0f) : WithAlpha(textColor, 0.86f),
                    BuildTimelineNodeLabel(*node));

                char nodeMeta[64];
                std::snprintf(nodeMeta, sizeof(nodeMeta), "%zu values", nodeParameters.size());
                const ImVec2 nodeMetaSize = ImGui::CalcTextSize(nodeMeta);
                rowsDrawList->AddText(
                    ImVec2(std::max(rowsTrackMinX + 8.0f, nodeRowMax.x - nodeMetaSize.x - 12.0f), nodeRowMin.y + 5.0f),
                    WithAlpha(disabledTextColor, 0.75f),
                    nodeMeta);

                ImGui::InvisibleButton(
                    ("##TimelineNodeRow_" + std::to_string(outputNodeId) + "_" + std::to_string(nodeId)).c_str(),
                    ImVec2(rowsWidth, kTimelineNodeRowHeight));
                if (ImGui::IsItemHovered()) {
                    rowsDrawList->AddRect(nodeRowMin, nodeRowMax, WithAlpha(headerHoveredColor, 0.58f), 3.0f);
                }
                if (ImGui::IsItemClicked()) {
                    if (rowToggleClicked(nodeRowMin, 16.0f)) {
                        ToggleInt(m_TimelineUi.collapsedNodeIds, nodeId);
                    } else {
                        m_TimelineUi.selectedOutputNodeId = outputNodeId;
                        m_TimelineUi.selectedParameterTarget = nodeParameters.front().target;
                        SelectGraphNode(nodeId);
                    }
                }

                ++rowIndex;
                if (nodeCollapsed) {
                    continue;
                }

                for (const Stack::Timeline::AnimatableParameterDefinition& definition : nodeParameters) {
                    const bool propertySelected =
                        Stack::Timeline::SameTarget(definition.target, m_TimelineUi.selectedParameterTarget) &&
                        outputNodeId == m_TimelineUi.selectedOutputNodeId;
                    const ImVec2 propertyRowMin = ImGui::GetCursorScreenPos();
                    const ImVec2 propertyRowMax(propertyRowMin.x + rowsWidth, propertyRowMin.y + kTimelinePropertyRowHeight);
                    const ImVec2 propertyLabelMin(propertyRowMin.x + 60.0f, propertyRowMin.y + 4.0f);
                    const ImVec2 propertyLabelMax(propertyRowMin.x + rowsLabelWidth - 8.0f, propertyRowMax.y - 3.0f);

                    drawRowBase(propertyRowMin, propertyRowMax, propertySelected, rowIndex, 0.66f);
                    drawTrackKeyframes(
                        [&definition](const Stack::Timeline::TimelineTrack& track) {
                            return TrackTargetsParameter(track, definition.target);
                        },
                        (propertyRowMin.y + propertyRowMax.y) * 0.5f,
                        4.4f,
                        propertySelected ? 0.98f : 0.78f);
                    drawRowPlayhead(propertyRowMin, propertyRowMax);

                    DrawClippedText(
                        rowsDrawList,
                        propertyLabelMin,
                        propertyLabelMax,
                        propertySelected ? WithAlpha(textColor, 1.0f) : WithAlpha(textColor, 0.80f),
                        definition.parameterLabel);

                    ImGui::InvisibleButton(
                        ("##TimelinePropertyRow_" + std::to_string(outputNodeId) + "_" +
                            std::to_string(definition.target.nodeId) + "_" + definition.target.parameterId).c_str(),
                        ImVec2(rowsWidth, kTimelinePropertyRowHeight));
                    if (ImGui::IsItemHovered()) {
                        rowsDrawList->AddRect(propertyRowMin, propertyRowMax, WithAlpha(headerHoveredColor, 0.54f), 3.0f);
                    }
                    if (ImGui::IsItemClicked()) {
                        m_TimelineUi.selectedOutputNodeId = outputNodeId;
                        m_TimelineUi.selectedParameterTarget = definition.target;
                        SelectGraphNode(nodeId);
                    }

                    ++rowIndex;
                }
            }
        }
    }

    ImGui::EndChild();
    if (m_TimelineUi.currentFrame != frameBeforeInteraction &&
        !GetGraphAnimation().tracks.empty()) {
        ClearTimelineLiveEditPreview();
        MarkTimelineFrameRenderDirty();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
