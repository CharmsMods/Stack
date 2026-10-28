#pragma once
#include "Project/NodeEditTarget.h"
#include "Renderer/RenderPipeline.h"
#include "Editor/NodeEditors/NodeInputAnalysisIdentity.h"
#include <imgui.h>
#include <algorithm>
#include <optional>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Stack::Editor {
struct NodeEditorPanelAnalysis {
    std::shared_ptr<const RawDevelopmentGraphScopeReadback> readback;
    std::size_t acceptedFingerprint = 0, attemptedFingerprint = 0;
    std::uint64_t revision = 0;
};
struct NodeEditorPanelInstance {
    Project::NodeEditTarget target;
    std::string key, ordinal;
};
struct NodeEditorPanelGroup {
    std::string definitionId, label;
    std::vector<NodeEditorPanelInstance> instances;
    std::string selectedKey;
    float measuredHeight = 0.f;
    float measuredWidth = 0.f;
    float measuredFont = 0.f;
};
// Transient, document-owned presentation. The catalog contains no node pointers.
struct NodeEditorPanelState {
    std::string document;
    std::vector<std::pair<std::string, std::uint64_t>> structure;
    std::vector<NodeEditorPanelGroup> groups;
    std::optional<Project::NodeEditTarget> lastTarget;
    std::optional<Project::NodeEditTarget> gestureTarget;
    std::optional<Project::NodeEditTarget> analysisTarget;
    bool visible = false;
    double floatingRevealStarted = -1.;
    float Reveal(double now, bool reduced) const {
        if (visible) return 0.f;
        if (reduced || floatingRevealStarted < 0.) return 1.f;
        const float t = static_cast<float>(std::clamp((now-floatingRevealStarted)/.18,0.,1.));
        return t*t*(3.f-2.f*t);
    }
};
}
