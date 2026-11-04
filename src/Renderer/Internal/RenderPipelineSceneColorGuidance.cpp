#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "NodeMath/ImageOperationInput.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <unordered_map>
#include <unordered_set>

namespace {
struct SourceGuide {
    std::shared_ptr<const Raw::SceneColorGuide> source;
    Stack::Renderer::SceneColorGuideTransform transform;
};
// Only operations whose grid and edge structure are unchanged may carry source
// guidance. A blur must not regain a sharp source-color edge in a later tone.
// In particular, a reformat/crop or a blend of unrelated sources must never
// borrow the currently selected RAW's map just because its dimensions match.
bool PreservesGuideGeometry(const RenderGraphNode& node) {
    if(node.kind==RenderGraphNodeKind::RawOperation || node.kind==RenderGraphNodeKind::Lut ||
        node.kind==RenderGraphNodeKind::Output)return true;
    if(node.kind!=RenderGraphNodeKind::Layer)return false;
    const auto type=node.layerJson.value("type",std::string());
    return type=="ToneCurve" || type=="Exposure" || type=="Noise" ||
        type=="ColorBalance" || type=="Saturation" || type=="Vibrance";
}
}

Stack::Renderer::SceneColorGuideCache::Lease RenderPipeline::ResolveSceneColorGuidance(
    const Stack::Renderer::GraphExecution::GraphExecutionContext& context,int nodeId,
    const std::string& socketId) {
    using namespace Stack::Renderer;
    using namespace Stack::Renderer::GraphExecution;
    if(m_CurrentGraphImageGeometry.resident.Partial())return {};
    std::unordered_map<std::string,SourceGuide> resolved;
    std::unordered_set<std::string> visiting;
    std::function<SourceGuide(int,const std::string&)> resolve;
    resolve=[&](int id,const std::string& socket)->SourceGuide {
        const auto key=MakeNodeSocketKey(id,socket);
        if(const auto cached=resolved.find(key);cached!=resolved.end())return cached->second;
        if(visiting.size()>=128 || !visiting.insert(key).second)return {};
        struct Leave {std::unordered_set<std::string>& set;std::string key;~Leave(){set.erase(key);}} leave{visiting,key};
        const auto found=context.nodes.find(id);
        if(found==context.nodes.end())return {};
        const auto& node=*found->second;
        SourceGuide result;
        if(node.kind==RenderGraphNodeKind::RawDevelopment || node.kind==RenderGraphNodeKind::RawProjectSourceSet) {
            const auto& raw=node.rawDevelopment.embeddedRawData;
            const auto& recipe=node.rawDevelopment.recipe;
            if(raw && raw->sceneColorGuide && !recipe.cropRotation.cropEnabled &&
                recipe.rgbDenoise.diagnosticMode==Stack::RawRecipe::RawDenoiseDiagnosticMode::None) {
                result.source=raw->sceneColorGuide;
                result.transform=BuildSceneColorGuideTransform(raw->metadata,Stack::RawRecipe::ToRawDevelopSettings(recipe),
                    Stack::RawRecipe::BuildColorCalibrationTransform(recipe.colorCalibration,recipe.technical.workingSpace));
            }
        } else if(node.kind==RenderGraphNodeKind::Mix) {
            const auto* a=context.FindInputLink(id,"imageA");
            const auto* b=context.FindInputLink(id,"imageB");
            if(a && b) {
                auto first=resolve(a->fromNodeId,a->fromSocketId);
                const auto second=resolve(b->fromNodeId,b->fromSocketId);
                if(first.source && first.source==second.source && first.transform==second.transform)result=std::move(first);
            }
        } else if(PreservesGuideGeometry(node)) {
            const auto* input=context.FindInputLink(id,"imageIn");
            if(input && Stack::NodeMath::DescribeImageOperationInput(input->semanticDescriptor).colorRelationships)
                result=resolve(input->fromNodeId,input->fromSocketId);
            if(result.source && node.kind==RenderGraphNodeKind::RawOperation && node.rawOperation.enabled &&
                socket!="inputImageOut" && node.rawOperation.kind==Stack::RawRecipe::GraphOperationKind::Calibration) {
                // Animated/uniform calibration values are evaluated elsewhere;
                // don't substitute the authored values for the actual transform.
                if(std::any_of(node.uniformParameters.begin(),node.uniformParameters.end(),[&](const auto& parameter) {
                    return context.FindInputLink(id,EditorNodeGraph::ParameterInputSocketId(parameter.id))!=nullptr;
                }))return {};
                const auto recipe=Stack::RawRecipe::ReadGraphOperation(node.rawOperation);
                const auto calibration=Stack::RawRecipe::BuildColorCalibrationTransform(recipe.colorCalibration,node.rawWorkingSpace);
                if(calibration.active) {
                    // A masked calibration has a spatially varying transform.
                    // A single matrix cannot describe the colors on both sides.
                    if(context.FindInputLink(id,"maskIn"))return {};
                    const auto before=result.transform.matrix;
                    result.transform.matrix.fill(0);
                    for(int r=0;r<3;++r)for(int c=0;c<3;++c)for(int k=0;k<3;++k)
                        result.transform.matrix[3*r+c]+=calibration.matrix[3*r+k]*before[3*k+c];
                }
            }
        }
        resolved.emplace(key,result);
        return result;
    };
    const auto source=resolve(nodeId,socketId);
    return m_SceneColorGuideCache.Prepare(source.source,source.transform,m_ShouldCancelRender);
}
