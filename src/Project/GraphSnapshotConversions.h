#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include "Renderer/MaskRenderTypes.h"

namespace Stack::Project::GraphSnapshotInternal {

RenderMaskGeneratorKind ToRenderMaskKind(EditorNodeGraph::MaskGeneratorKind kind);
RenderMaskSettings ToRenderMaskSettings(const EditorNodeGraph::MaskGeneratorSettings& settings);
RenderMixBlendMode ToRenderMixBlendMode(EditorNodeGraph::MixBlendMode mode);
RenderMaskUtilityKind ToRenderMaskUtilityKind(EditorNodeGraph::MaskUtilityKind kind);
RenderMaskCombineMode ToRenderMaskCombineMode(EditorNodeGraph::MaskCombineMode mode);
RenderCustomMaskObjectType ToRenderCustomMaskObjectType(EditorNodeGraph::CustomMaskObjectType type);
RenderCustomMaskOperation ToRenderCustomMaskOperation(EditorNodeGraph::CustomMaskOperation operation);
RenderCustomMaskPayload ToRenderCustomMaskPayload(const EditorNodeGraph::CustomMaskPayload& payload);
RenderImageToMaskKind ToRenderImageToMaskKind(EditorNodeGraph::ImageToMaskKind kind);
RenderImageGeneratorKind ToRenderImageGeneratorKind(EditorNodeGraph::ImageGeneratorKind kind);
RenderMaskUtilitySettings ToRenderMaskUtilitySettings(const EditorNodeGraph::MaskUtilitySettings& settings);
RenderImageToMaskSettings ToRenderImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings);
RenderImageGeneratorSettings ToRenderImageGeneratorSettings(const EditorNodeGraph::ImageGeneratorSettings& settings);
RenderFrequencyResponseSettings ToRenderFrequencyResponseSettings(const EditorNodeGraph::FrequencyResponseSettings& settings);

} // namespace Stack::Project::GraphSnapshotInternal
