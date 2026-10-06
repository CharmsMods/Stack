#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphEditorCommands.h"

#include "Editor/Layers/ToneLayers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <optional>

using Stack::Editor::AddGraphNode;

namespace {

EditorNodeGraph::MaskCombineMode ToGraphMaskCombineMode(ToneCurveScopeMaskAction action) {
    switch (action) {
        case ToneCurveScopeMaskAction::Add: return EditorNodeGraph::MaskCombineMode::Add;
        case ToneCurveScopeMaskAction::Subtract: return EditorNodeGraph::MaskCombineMode::Subtract;
        case ToneCurveScopeMaskAction::Intersect:
        case ToneCurveScopeMaskAction::NewMask:
        default: return EditorNodeGraph::MaskCombineMode::Intersect;
    }
}

} // namespace

void EditorModule::AddScopeNodeAt(EditorNodeGraph::ScopeKind scopeKind, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddScopeNode(scopeKind, graphPosition); });
}

void EditorModule::AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind maskKind, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddMaskGeneratorNode(maskKind, graphPosition); });
}

void EditorModule::AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode combineMode, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddMaskCombineNode(combineMode, graphPosition); });
}

void EditorModule::AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddMaskUtilityNode(utilityKind, graphPosition); });
}

void EditorModule::AddCustomMaskNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::CustomMaskPayload payload;
    if (m_Pipeline.GetCanvasWidth() > 0 && m_Pipeline.GetCanvasHeight() > 0) {
        payload.width = std::clamp(m_Pipeline.GetCanvasWidth(), 1, 4096);
        payload.height = std::clamp(m_Pipeline.GetCanvasHeight(), 1, 4096);
    }
    const int nodeId = AddGraphNode(*this,[&](auto& graph) {
        return graph.AddCustomMaskNode(std::move(payload),graphPosition);
    });
    if (nodeId > 0) SwitchToComplexNodeSubWindow(nodeId);
}

void EditorModule::AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddImageToMaskNode(converterKind, graphPosition); });
}

bool EditorModule::CreateToneCurveSelectionMask(
    int toneCurveNodeId,
    float low,
    float high,
    float softness,
    const std::array<float, 4>& sampleRgba,
    float sampleLuma,
    float sampleU,
    float sampleV,
    float toneSimilarity,
    float colorSimilarity,
    float regionRadius,
    float regionFeather,
    float edgeSensitivity,
    float localCoherence,
    ToneCurveScopeMaskAction action) {
    const bool startNewScopedMask = action == ToneCurveScopeMaskAction::NewMask;
    bool appendedSample = false;
    bool duplicateSample = false;
    bool sampleCapacityReached = false;
    int selectedId = 0;
    int sampleCount = 0;
    std::string error;
    const bool applied = Stack::Editor::ApplyGraphCommand(*this,[&](auto& graph) {
        const auto* owner = graph.FindNode(toneCurveNodeId);
        if (!owner) throw std::runtime_error("The tone operation no longer exists.");
        const bool ownerIsRawDevelop = owner->kind == EditorNodeGraph::NodeKind::RawDevelop;
        const bool curve = (owner->kind == EditorNodeGraph::NodeKind::Layer && owner->layerType == LayerType::ToneCurve) ||
            (owner->kind == EditorNodeGraph::NodeKind::RawOperation &&
                (owner->rawOperation.kind == Stack::RawRecipe::GraphOperationKind::LuminanceTone ||
                 owner->rawOperation.kind == Stack::RawRecipe::GraphOperationKind::RgbCurves));
        const auto position = owner->position;
        int sourceId = 0;
        std::string sourcePort;
        if (ownerIsRawDevelop) {
            if (!owner->rawDevelop.integratedToneEnabled || !graph.FindInputLink(toneCurveNodeId,EditorNodeGraph::kRawInputSocketId))
                throw std::runtime_error("Develop needs a RAW input and its finish stage enabled before creating a tone scope mask.");
            sourceId = toneCurveNodeId;
            sourcePort = EditorNodeGraph::kPreFinishImageOutputSocketId;
        } else if (curve) {
            const auto* input = graph.FindInputLink(toneCurveNodeId,EditorNodeGraph::kImageInputSocketId);
            if (!input) throw std::runtime_error("Connect the tone operation's image input before creating a scope mask.");
            sourceId = input->fromNodeId;
            sourcePort = input->fromSocketId;
        } else throw std::runtime_error("This operation does not support tone scope masks.");
        const auto* existing = graph.FindInputLink(toneCurveNodeId,EditorNodeGraph::kMaskInputSocketId);
        const std::optional<EditorNodeGraph::Link> previous = existing ? std::optional<EditorNodeGraph::Link>(*existing) : std::nullopt;
        int maskNodeId = 0;
        int combineId = 0;
        if (previous && !startNewScopedMask) {
            const auto isScope = [&](int id) {
                const auto* n = graph.FindNode(id);
                return n && n->kind == EditorNodeGraph::NodeKind::ImageToMask && n->title == "Tone Scope Mask";
            };
            if (isScope(previous->fromNodeId)) maskNodeId = previous->fromNodeId;
            else if (const auto* n = graph.FindNode(previous->fromNodeId); n && n->kind == EditorNodeGraph::NodeKind::MaskCombine) {
                for (const auto* port : {EditorNodeGraph::kMaskCombineInputASocketId,EditorNodeGraph::kMaskCombineInputBSocketId}) {
                    const auto* input = graph.FindInputLink(n->id,port);
                    if (input && isScope(input->fromNodeId)) { maskNodeId = input->fromNodeId; combineId = n->id; break; }
                }
            }
        }
        const bool reusedExistingToneScopeMask = maskNodeId != 0;
        if (!maskNodeId) {
            const auto* created = graph.AddImageToMaskNode(EditorNodeGraph::ImageToMaskKind::SampledRange,
                {position.x-250.f,position.y+135.f});
            if (!created) throw std::runtime_error("The tone scope mask could not be created.");
            maskNodeId = created->id;
        }
        const auto connect = [&](int from, const std::string& output, int to, const std::string& input) {
            std::string reason;
            if (!graph.TryConnectSockets(from,output,to,input,&reason)) throw std::runtime_error(reason);
        };
        connect(sourceId,sourcePort,maskNodeId,EditorNodeGraph::kImageInputSocketId);
        if (startNewScopedMask || !previous || previous->fromNodeId == maskNodeId) {
            connect(maskNodeId,EditorNodeGraph::kMaskOutputSocketId,toneCurveNodeId,EditorNodeGraph::kMaskInputSocketId);
        } else if (combineId) {
            graph.SetMaskCombineMode(combineId,ToGraphMaskCombineMode(action));
        } else {
            const auto* combine = graph.AddMaskCombineNode(ToGraphMaskCombineMode(action),
                {position.x-125.f,position.y+140.f});
            if (!combine) throw std::runtime_error("The tone scope combination could not be created.");
            combineId = combine->id;
            graph.FindNode(combineId)->title = "Tone Scope Combine";
            connect(previous->fromNodeId,previous->fromSocketId,combineId,EditorNodeGraph::kMaskCombineInputASocketId);
            connect(maskNodeId,EditorNodeGraph::kMaskOutputSocketId,combineId,EditorNodeGraph::kMaskCombineInputBSocketId);
            connect(combineId,EditorNodeGraph::kMaskOutputSocketId,toneCurveNodeId,EditorNodeGraph::kMaskInputSocketId);
        }
        auto* maskNode = graph.FindNode(maskNodeId);
        const float clampedLow = std::clamp(std::min(low, high), 0.0f, 1.0f);
        const float clampedHigh = std::clamp(std::max(low, high), 0.0f, 1.0f);
        const float clampedSampleRgb[3] = {
            std::clamp(sampleRgba[0], 0.0f, 16.0f),
            std::clamp(sampleRgba[1], 0.0f, 16.0f),
            std::clamp(sampleRgba[2], 0.0f, 16.0f)
        };
        const float clampedSampleLuma = std::clamp(sampleLuma, 0.0f, 16.0f);
    
        graph.SetImageToMaskKind(
            maskNode->id,
            EditorNodeGraph::ImageToMaskKind::SampledRange);
        maskNode->title = "Tone Scope Mask";
        maskNode->imageToMaskSettings.low = clampedLow;
        maskNode->imageToMaskSettings.high = std::max(clampedLow + 0.0001f, clampedHigh);
        maskNode->imageToMaskSettings.softness = std::clamp(softness, 0.0f, 0.5f);
        maskNode->imageToMaskSettings.invert = false;
        maskNode->imageToMaskSettings.sampleU = std::clamp(sampleU, 0.0f, 1.0f);
        maskNode->imageToMaskSettings.sampleV = std::clamp(sampleV, 0.0f, 1.0f);
        maskNode->imageToMaskSettings.toneSimilarity = std::clamp(toneSimilarity, 0.02f, 0.35f);
        maskNode->imageToMaskSettings.colorSimilarity = std::clamp(colorSimilarity, 0.02f, 0.50f);
        maskNode->imageToMaskSettings.regionRadius = std::clamp(regionRadius, 0.05f, 1.0f);
        maskNode->imageToMaskSettings.regionFeather = std::clamp(regionFeather, 0.0f, 1.0f);
        maskNode->imageToMaskSettings.edgeSensitivity = std::clamp(edgeSensitivity, 0.0f, 1.0f);
        maskNode->imageToMaskSettings.localCoherence = std::clamp(localCoherence, 0.0f, 1.0f);
    
        auto clearExtraSamples = [&](EditorNodeGraph::ImageToMaskSettings& settings) {
            for (int i = 0; i < 4; ++i) {
                settings.extraSampleRgb[i][0] = 0.5f;
                settings.extraSampleRgb[i][1] = 0.5f;
                settings.extraSampleRgb[i][2] = 0.5f;
                settings.extraSampleLuma[i] = 0.5f;
            }
        };
        auto resetPrimarySample = [&](EditorNodeGraph::ImageToMaskSettings& settings) {
            settings.sampleCount = 1;
            settings.sampleRgb[0] = clampedSampleRgb[0];
            settings.sampleRgb[1] = clampedSampleRgb[1];
            settings.sampleRgb[2] = clampedSampleRgb[2];
            settings.sampleLuma = clampedSampleLuma;
            clearExtraSamples(settings);
        };
        auto sampleMatches = [&](const EditorNodeGraph::ImageToMaskSettings& settings, int sampleIndex) {
            if (sampleIndex <= 0) {
                return std::abs(settings.sampleRgb[0] - clampedSampleRgb[0]) < 0.0005f &&
                    std::abs(settings.sampleRgb[1] - clampedSampleRgb[1]) < 0.0005f &&
                    std::abs(settings.sampleRgb[2] - clampedSampleRgb[2]) < 0.0005f &&
                    std::abs(settings.sampleLuma - clampedSampleLuma) < 0.0005f;
            }
            const int extraIndex = sampleIndex - 1;
            return std::abs(settings.extraSampleRgb[extraIndex][0] - clampedSampleRgb[0]) < 0.0005f &&
                std::abs(settings.extraSampleRgb[extraIndex][1] - clampedSampleRgb[1]) < 0.0005f &&
                std::abs(settings.extraSampleRgb[extraIndex][2] - clampedSampleRgb[2]) < 0.0005f &&
                std::abs(settings.extraSampleLuma[extraIndex] - clampedSampleLuma) < 0.0005f;
        };
    
        const bool allowSampleAppend = !startNewScopedMask && reusedExistingToneScopeMask;
        EditorNodeGraph::ImageToMaskSettings& imageToMaskSettings = maskNode->imageToMaskSettings;
        if (imageToMaskSettings.sampleCount < 1 || imageToMaskSettings.sampleCount > 5) {
            imageToMaskSettings.sampleCount = 1;
        }
        if (allowSampleAppend && imageToMaskSettings.sampleCount >= 1) {
            for (int i = 0; i < imageToMaskSettings.sampleCount; ++i) {
                if (sampleMatches(imageToMaskSettings, i)) {
                    duplicateSample = true;
                    break;
                }
            }
            if (!duplicateSample) {
                if (imageToMaskSettings.sampleCount < 5) {
                    const int extraIndex = imageToMaskSettings.sampleCount - 1;
                    imageToMaskSettings.extraSampleRgb[extraIndex][0] = clampedSampleRgb[0];
                    imageToMaskSettings.extraSampleRgb[extraIndex][1] = clampedSampleRgb[1];
                    imageToMaskSettings.extraSampleRgb[extraIndex][2] = clampedSampleRgb[2];
                    imageToMaskSettings.extraSampleLuma[extraIndex] = clampedSampleLuma;
                    imageToMaskSettings.sampleCount += 1;
                    appendedSample = true;
                } else {
                    sampleCapacityReached = true;
                }
            }
        } else {
            resetPrimarySample(imageToMaskSettings);
        }
    
        if (!allowSampleAppend && !appendedSample) {
            resetPrimarySample(imageToMaskSettings);
        } else if (imageToMaskSettings.sampleCount <= 0) {
            resetPrimarySample(imageToMaskSettings);
        }
        selectedId = ownerIsRawDevelop ? toneCurveNodeId : maskNodeId;
        sampleCount = imageToMaskSettings.sampleCount;
        if (!reusedExistingToneScopeMask && IsEditingRawLayerMaskGraph()) {
            auto* output = graph.AddOutputNode({position.x+100.f,position.y+240.f});
            if (!output) throw std::runtime_error("The named mask output could not be created.");
            output->title = "Tone Scope Mask";
            output->outputSettings.maskOutput = true;
            connect(maskNodeId,EditorNodeGraph::kMaskOutputSocketId,output->id,EditorNodeGraph::kImageInputSocketId);
        }
        graph.SelectNode(selectedId);
    },&error);
    if (!applied) {
        PostNotification(UiNotificationSeverity::Error,error,"tone-curve-mask-create");
        return false;
    }
    SelectGraphNode(selectedId);
    if (sampleCapacityReached || duplicateSample) {
        Stack::Notifications::NoticeSpec partial;
        partial.severity = Stack::Notifications::Severity::Warning;
        partial.outcome = Stack::Notifications::Outcome::Partial;
        partial.message = sampleCapacityReached
            ? "Tone scope mask already has five samples. Refine settings were updated, but no additional sample was added."
            : "That sampled tone is already present in the tone scope mask. Refine settings were updated.";
        partial.dedupeKey = "tone-curve-mask-create";
        GetNotifier().Post(std::move(partial));
    } else {
        PostNotification(UiNotificationSeverity::Success,
            appendedSample ? "Tone scope mask refined with sample " + std::to_string(sampleCount) + " of 5."
                : "Created a tone scope mask from the sampled tone and color range.","tone-curve-mask-create");
    }
    return true;
}
