#pragma once
#include "Raw/RawWorkspace.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Project/NodeEditTarget.h"
#include "NodeMath/UniformParameter.h"
#include "Editor/Internal/RawLab/RawFloatingInputTrace.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeEditors/NodeEditorState.h"

namespace Stack::Editor {
struct RawWorkspaceEditContext {
    Stack::Editor::RawLabInternal::RawEditabilityTrace editabilityTrace;
    std::string rawAdjustmentLayerId;
    std::string rawOperationUuid;
    std::optional<Project::NodeEditTarget> nodeTarget;
    std::uint64_t nodeRevision = 0;
    int presentationNodeId = -1;
    // Custom non-recipe editors change this payload draft. Fallback controls
    // retain their graph draft; image buffers are not copied for that route.
    NodeEditorState* editorState = nullptr; // Supplied by the drawing host, never retained by work jobs.
    template<class T> T& State() { return editorState->Get<T>(nodeTarget.value()); }
    std::optional<EditorNodeGraph::Node> nodeDraft;
    nlohmann::json nodeSettings;
    std::unordered_set<std::string> drivenParameters;
    std::unordered_map<std::string,NodeMath::UniformParameterFeedback> uniformParameters;
    bool IsParameterDriven(const std::string& id) const { return drivenParameters.count(id) != 0; }
    // Endpoints are authored identities, never transient canvas node numbers.
    // A request copies these with nodeTarget/nodeRevision before leaving UI.
    std::unordered_map<std::string,GraphModel::Endpoint> inputs;
    NodeMath::ValueDescriptor inputDescriptor;
    RawLabInternal::RawLabGraphHistogram inputHistogram;
    std::uint64_t inputHistogramRevision = 0;
    const GraphModel::Endpoint* Input(const std::string& port) const {
        const auto found=inputs.find(port);
        return found==inputs.end()?nullptr:&found->second;
    }
    const Stack::RawWorkspace::SourceRecord* source = nullptr;
    Stack::RawWorkspace::RawPanelState panelState;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    // The authored document recipe remains immutable while this context
    // owns its one editable value copy.
    const Stack::RawRecipe::RawDevelopmentRecipe* persistedRecipe = nullptr;
    Stack::RawWorkspace::RawProjectMode resolvedMode =
        Stack::RawWorkspace::RawProjectMode::Unknown;
    std::string multiFrameSourceSetId;
    std::string error;
    bool multiFrameResult = false;
    bool canEdit = false;
};
}
