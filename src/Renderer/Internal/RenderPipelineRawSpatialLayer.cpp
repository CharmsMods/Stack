#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include <stdexcept>

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderRawSpatialLayer(
    const Stack::Renderer::GraphExecution::GraphExecutionContext& context,
    const RenderGraphNode& node, unsigned int input) {
    using namespace Stack::RawRecipe;
    using namespace Stack::Renderer;
    using namespace Stack::Renderer::GraphExecution;
    const auto recipe = DeserializeRecipe(node.layerJson.at("recipe"));
    const auto type = node.layerJson.at("type").get<std::string>();
    GraphNodeRenderResult result;
    result.texture = input;
    const auto key = MakeNodeSocketKey(node.nodeId, "imageOut");
    const auto found = context.imageFingerprintCache.find(key);
    if (found == context.imageFingerprintCache.end())
        throw std::runtime_error("RAW spatial stage has no input identity");
    const auto fingerprint = found->second;
    if (type == "RawOutputCrop") {
        RenderRawDevelopmentOutputCropStage(result, recipe.cropRotation,
            key + ":output-crop", fingerprint);
        return result;
    }
    throw std::runtime_error("Unsupported RAW output stage.");
}
