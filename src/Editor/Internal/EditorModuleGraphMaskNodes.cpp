#include "Editor/EditorModule.h"

#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

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
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddScopeNode(scopeKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind maskKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskGeneratorNode(maskKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode combineMode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskCombineNode(combineMode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskUtilityNode(utilityKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddCustomMaskNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    EditorNodeGraph::CustomMaskPayload payload;
    if (m_Pipeline.GetCanvasWidth() > 0 && m_Pipeline.GetCanvasHeight() > 0) {
        payload.width = std::clamp(m_Pipeline.GetCanvasWidth(), 1, 4096);
        payload.height = std::clamp(m_Pipeline.GetCanvasHeight(), 1, 4096);
    }
    payload.rasterLayer.assign(
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height),
        0.0f);

    if (EditorNodeGraph::Node* node = m_NodeGraph.AddCustomMaskNode(std::move(payload), graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
        SwitchToComplexNodeSubWindow(node->id);
    }
}

void EditorModule::AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageToMaskNode(converterKind, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
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
    EditorNodeGraph::Node* toneCurveNode = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!toneCurveNode) {
        return false;
    }

    int maskOwnerNodeId = toneCurveNodeId;
    int sourceImageNodeId = -1;
    std::string sourceImageSocketId;
    const EditorNodeGraph::Link* maskInput = nullptr;
    if (toneCurveNode->kind == EditorNodeGraph::NodeKind::Layer &&
        toneCurveNode->layerType == LayerType::ToneCurve) {
        const EditorNodeGraph::Link* imageInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!imageInput) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Tone Curve needs an image input before a tone scope mask can be created.",
                "tone-curve-mask-create");
            return false;
        }
        sourceImageNodeId = imageInput->fromNodeId;
        sourceImageSocketId = imageInput->fromSocketId;
        maskInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kMaskInputSocketId);
    } else if (toneCurveNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        if (!toneCurveNode->rawDevelop.integratedToneEnabled) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Develop needs its finish stage enabled before creating a tone scope mask.",
                "tone-curve-mask-create");
            return false;
        }
        if (!m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kRawInputSocketId)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Develop needs a RAW input before a finish scope mask can be created.",
                "tone-curve-mask-create");
            return false;
        }
        sourceImageNodeId = toneCurveNodeId;
        sourceImageSocketId = EditorNodeGraph::kPreFinishImageOutputSocketId;
        maskInput = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kMaskInputSocketId);
    } else {
        return false;
    }

    EditorNodeGraph::Node* maskNode = nullptr;
    EditorNodeGraph::Node* combineNode = nullptr;
    const bool hadExistingMaskInput = maskInput != nullptr;
    const bool startNewScopedMask = action == ToneCurveScopeMaskAction::NewMask;
    const EditorNodeGraph::MaskCombineMode requestedCombineMode = ToGraphMaskCombineMode(action);
    bool reusedExistingToneScopeMask = false;
    if (maskInput) {
        combineNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
        if (combineNode && combineNode->kind == EditorNodeGraph::NodeKind::MaskCombine) {
            const EditorNodeGraph::Link* inputA = m_NodeGraph.FindInputLink(combineNode->id, EditorNodeGraph::kMaskCombineInputASocketId);
            const EditorNodeGraph::Link* inputB = m_NodeGraph.FindInputLink(combineNode->id, EditorNodeGraph::kMaskCombineInputBSocketId);
            for (const EditorNodeGraph::Link* input : { inputA, inputB }) {
                if (!input) {
                    continue;
                }
                EditorNodeGraph::Node* candidate = m_NodeGraph.FindNode(input->fromNodeId);
                if (candidate &&
                    candidate->kind == EditorNodeGraph::NodeKind::ImageToMask &&
                    candidate->title == "Tone Scope Mask") {
                    maskNode = candidate;
                    reusedExistingToneScopeMask = true;
                    break;
                }
            }
        } else {
            maskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
            if (!maskNode || maskNode->kind != EditorNodeGraph::NodeKind::ImageToMask || maskNode->title != "Tone Scope Mask") {
                maskNode = nullptr;
            } else {
                reusedExistingToneScopeMask = true;
            }
        }
    } else {
        const EditorNodeGraph::Vec2 position{
            toneCurveNode->position.x - 250.0f,
            toneCurveNode->position.y + 135.0f
        };
        maskNode = m_NodeGraph.AddImageToMaskNode(EditorNodeGraph::ImageToMaskKind::Luminance, position);
        if (!maskNode) {
            return false;
        }
    }

    if (!maskNode) {
        const EditorNodeGraph::Vec2 position{
            toneCurveNode->position.x - 250.0f,
            toneCurveNode->position.y + 135.0f
        };
        maskNode = m_NodeGraph.AddImageToMaskNode(EditorNodeGraph::ImageToMaskKind::Luminance, position);
        if (!maskNode) {
            return false;
        }
        maskNode->title = "Tone Scope Mask";
    } else if (maskNode->title.empty()) {
        maskNode->title = "Tone Scope Mask";
    }

    const EditorNodeGraph::Link* maskImageInput = m_NodeGraph.FindInputLink(maskNode->id, EditorNodeGraph::kImageInputSocketId);
    if (!maskImageInput ||
        maskImageInput->fromNodeId != sourceImageNodeId ||
        maskImageInput->fromSocketId != sourceImageSocketId) {
        if (maskImageInput) {
            RemoveGraphLink(maskImageInput->fromNodeId, maskImageInput->fromSocketId, maskNode->id, EditorNodeGraph::kImageInputSocketId);
        }
        std::string errorMessage;
        if (!ConnectGraphSockets(
                sourceImageNodeId,
                sourceImageSocketId,
                maskNode->id,
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to connect the tone scope mask to the finish-stage input." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    }

    if (startNewScopedMask && maskInput) {
        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);
        combineNode = nullptr;
        maskInput = nullptr;
    }

    if (!maskInput) {
        std::string errorMessage;
        if (!ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to connect the tone scope mask to the finish-stage target." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    } else if (!combineNode && maskInput->fromNodeId != maskNode->id) {
        const EditorNodeGraph::Node* existingMaskNode = m_NodeGraph.FindNode(maskInput->fromNodeId);
        const EditorNodeGraph::Vec2 combinePosition{
            toneCurveNode->position.x - 125.0f,
            toneCurveNode->position.y + 140.0f
        };
        combineNode = m_NodeGraph.AddMaskCombineNode(requestedCombineMode, combinePosition);
        if (!combineNode) {
            return false;
        }
        combineNode->title = "Tone Scope Combine";

        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);

        std::string errorMessage;
        if (!ConnectGraphSockets(
                existingMaskNode ? existingMaskNode->id : maskInput->fromNodeId,
                maskInput->fromSocketId,
                combineNode->id,
                EditorNodeGraph::kMaskCombineInputASocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                combineNode->id,
                EditorNodeGraph::kMaskCombineInputBSocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                combineNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to combine the existing mask with the new tone scope mask." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
    } else if (combineNode &&
               (m_NodeGraph.HasLink(maskNode->id, EditorNodeGraph::kMaskOutputSocketId, combineNode->id, EditorNodeGraph::kMaskCombineInputASocketId) ||
                m_NodeGraph.HasLink(maskNode->id, EditorNodeGraph::kMaskOutputSocketId, combineNode->id, EditorNodeGraph::kMaskCombineInputBSocketId))) {
        combineNode->maskCombineMode = requestedCombineMode;
    } else if (maskInput && maskInput->fromNodeId != maskNode->id) {
        const EditorNodeGraph::Vec2 combinePosition{
            toneCurveNode->position.x - 125.0f,
            toneCurveNode->position.y + 140.0f
        };
        EditorNodeGraph::Node* nestedCombine = m_NodeGraph.AddMaskCombineNode(requestedCombineMode, combinePosition);
        if (!nestedCombine) {
            return false;
        }
        nestedCombine->title = "Tone Scope Combine";

        RemoveGraphLink(maskInput->fromNodeId, maskInput->fromSocketId, maskOwnerNodeId, EditorNodeGraph::kMaskInputSocketId);

        std::string errorMessage;
        if (!ConnectGraphSockets(
                maskInput->fromNodeId,
                maskInput->fromSocketId,
                nestedCombine->id,
                EditorNodeGraph::kMaskCombineInputASocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                maskNode->id,
                EditorNodeGraph::kMaskOutputSocketId,
                nestedCombine->id,
                EditorNodeGraph::kMaskCombineInputBSocketId,
                &errorMessage) ||
            !ConnectGraphSockets(
                nestedCombine->id,
                EditorNodeGraph::kMaskOutputSocketId,
                maskOwnerNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Failed to refine the existing mask with a new tone scope component." : errorMessage,
                "tone-curve-mask-create");
            return false;
        }
        combineNode = nestedCombine;
    }

    const float clampedLow = std::clamp(std::min(low, high), 0.0f, 1.0f);
    const float clampedHigh = std::clamp(std::max(low, high), 0.0f, 1.0f);
    const float clampedSampleRgb[3] = {
        std::clamp(sampleRgba[0], 0.0f, 16.0f),
        std::clamp(sampleRgba[1], 0.0f, 16.0f),
        std::clamp(sampleRgba[2], 0.0f, 16.0f)
    };
    const float clampedSampleLuma = std::clamp(sampleLuma, 0.0f, 16.0f);

    maskNode->imageToMaskKind = EditorNodeGraph::ImageToMaskKind::SampledRange;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*maskNode);
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

    bool appendedSample = false;
    bool duplicateSample = false;
    bool sampleCapacityReached = false;
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
    SelectGraphNode(
        toneCurveNode->kind == EditorNodeGraph::NodeKind::RawDevelop
            ? maskOwnerNodeId
            : maskNode->id);
    MarkRenderDirty(maskOwnerNodeId);
    if (sampleCapacityReached) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Tone scope mask already has five samples. Refine settings were updated, but no additional sample was added.",
            "tone-curve-mask-create");
    } else if (duplicateSample) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "That sampled tone is already present in the tone scope mask. Refine settings were updated.",
            "tone-curve-mask-create");
    } else {
        QueueUiNotification(
            UiNotificationSeverity::Success,
            appendedSample
                ? ("Tone scope mask refined with sample " + std::to_string(imageToMaskSettings.sampleCount) + " of 5.")
                : (startNewScopedMask
                    ? "Created a new scoped tone mask from the sampled tone and color range."
                    : (!hadExistingMaskInput
                        ? "Created a scoped tone mask from the sampled tone and color range."
                        : (action == ToneCurveScopeMaskAction::Add
                        ? "Added a sampled tone scope component to the existing mask."
                        : (action == ToneCurveScopeMaskAction::Subtract
                            ? "Subtracted a sampled tone scope component from the existing mask."
                            : "Intersected the existing mask with a sampled tone scope component.")))),
            "tone-curve-mask-create");
    }
    return true;
}
