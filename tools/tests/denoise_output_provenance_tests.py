#!/usr/bin/env python3
"""Generate bounded no-GPU regression checks for output denoise provenance.

Production scope, evaluator cache/control-flow shells, borrowed-result wrappers,
cache ownership methods, and output publication are extracted at generation time.
Only graph kernel dispatch, scalar readback, GL/resource services, and unrelated
scope diagnostics are substituted. RAW cache-hit/publication blocks are exercised
as extracted blocks, not a replacement denoise lifecycle. These tests establish
metadata propagation, not shader/model output, real cache fingerprints, or GL.
"""
import argparse
from pathlib import Path


def block(source, marker):
    start = source.index(marker) if isinstance(marker, str) else marker
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def between(source, first, last):
    return source[source.index(first):source.index(last, source.index(first))]


def replace_between(source, first, last, replacement):
    start = source.index(first)
    end = source.index(last, start)
    return source[:start] + replacement + source[end:]


parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
internal = root / 'src/Renderer/Internal'
read = lambda name: (internal / name).read_text()
header = (root / 'src/Renderer/RenderPipeline.h').read_text()
scope = block(read('RenderPipelineGraphExecutionRuntime.h'), '    struct DenoiseProvenance {') + ';'
cache_structs = '\n'.join(block(header, '    struct ' + name + ' {') + ';'
                         for name in ['CachedGraphTexture', 'CachedGraphScalar', 'GraphNodeRenderResult'])
image = block(read('RenderPipelineGraphExecutionEvaluateImage.cpp'),
              'void GraphExecutionRuntime::BindImageEvaluation()')
# Preserve the real borrowed-return wrapper and both call sites. Replace the
# large shader/node dispatch with deterministic callbacks to actual evaluators.
wrapper = between(image, '        const auto withBorrowedInputProvenance', '        bool pointwiseFused')
branches = '\n'.join(block(image, 'if (node.kind == RenderGraphNodeKind::' + kind + ')')
                     for kind in ['RawDevelopment', 'RawDevelop', 'RawOperation', 'RawProjectSourceSet'])
branches = branches.replace('\nif (node.kind', '\nelse if (node.kind')
image = replace_between(image, '        Stack::Renderer::ScopedOwnedGLTextureExceptionCleanup',
                        '        if (pipeline.m_ShouldCancelRender && pipeline.m_ShouldCancelRender()) {',
                        wrapper + branches + r'''
        else {
            const auto generated = RunKernel(node);
            result = generated.texture;
            resultOwned = generated.owned;
        }
''')
mask = block(read('RenderPipelineGraphExecutionEvaluateMask.cpp'),
             'void GraphExecutionRuntime::BindMaskEvaluation()')
channel_split = block(mask, 'if (node.kind == RenderGraphNodeKind::ChannelSplit)')
mask = replace_between(mask, '        Stack::Renderer::ScopedOwnedGLTextureExceptionCleanup',
                       '        if (pipeline.m_ShouldCancelRender && pipeline.m_ShouldCancelRender()) {', channel_split + r'''
        else {
            const auto generated = RunKernel(node);
            result = generated.texture;
            resultOwned = generated.owned;
        }
''')
scalar = block(read('RenderPipelineGraphExecutionEvaluateScalar.cpp'),
               'void GraphExecutionRuntime::BindScalarEvaluation()')
# Uniform arithmetic is outside this test. Field Mean retains its actual input
# evaluation, cache lookup, success/failure handling, and persistent publication.
scalar = replace_between(scalar, '        if (node.kind == RenderGraphNodeKind::DataMath &&',
                         '        if (node.kind != RenderGraphNodeKind::FieldMean', '')
scalar = replace_between(scalar, '        constexpr int kReadbackRows', '        value = mean.value;', r'''
        const FakeMean mean{node.scalarValue, 4};
''')
execute = block(read('RenderPipelineGraphExecutionRuntime.cpp'), 'void GraphExecutionRuntime::Execute()')
execute = execute[:execute.index('    pipeline.PruneInactiveGraphCache(')] + '\n}'
scope_work = block(execute, '    if (graph.rawLayerScopeNodeId > 0')
execute = execute.replace(scope_work, r'''
    if (graph.rawLayerScopeNodeId > 0)
        (void)evalImage(graph.rawLayerScopeNodeId, graph.rawLayerScopeSocketId);
''')
cache = read('RenderPipelineGraphTextureCache.cpp')
cache_methods = '\n'.join(block(cache, marker) for marker in [
    'void RenderPipeline::DeleteGraphCacheEntry(',
    'void RenderPipeline::ReleaseGraphCacheEntry(',
    'bool RenderPipeline::StoreGraphCacheEntry(',
    'void RenderPipeline::TouchGraphCacheEntry(',
    'RenderPipeline::CachedGraphTexture* RenderPipeline::FindGraphCacheEntry('])
raw_cache = read('RenderPipelineRawDevelopStageCache.cpp')
raw_methods = '\n'.join(block(raw_cache, marker) for marker in [
    'RenderPipeline::CachedGraphTexture RenderPipeline::FindRawDevelopStageCacheEntry(',
    'bool RenderPipeline::StoreRawDevelopStageCacheEntry(',
    'void RenderPipeline::InvalidateRawDevelopStageCacheEntry('])
raw_methods += '\n' + block(read('RenderPipelineViewportDependency.cpp'),
    'RenderPipeline::CachedGraphTexture RenderPipeline::FindRawViewportDenoiseInput(')
provisional = block(read('RenderPipelineRawRgbDenoise.cpp'),
                    'unsigned int RenderPipeline::RenderProvisionalRawRgbDenoiseOutput(')
# Real metadata adoption/publication fragments. Outer GPU/crop orchestration is
# deliberately absent; the exercise names distinguish this from a full render.
raw_node = read('RenderPipelineGraphRawDevelopmentNode.cpp')
raw_hit = block(raw_node, '    const auto adoptCachedStage =') + ';\n        (void)adoptCachedStage(cached);'
crop_source = read('RenderPipelineGraphRawDevelopmentOutputStages.cpp')
crop_method = block(crop_source, 'void RenderPipeline::RenderRawDevelopmentOutputCropStage(')
crop_hit = block(crop_method, '    if (cached.texture != 0 && cached.width > 0 && cached.height > 0)')
crop_publish = block(crop_method, '    if (cropRendered)')
region_source = read('RenderPipelineViewportRegion.cpp')
region_hit = block(region_source, '    if (cached.texture)')
region_publish = between(region_source, '    if (result.owned) glDeleteTextures', '\n}')

pre = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using GLuint = unsigned int;
using GLint = int;
constexpr int GL_TRUE=1, GL_TEXTURE_2D=1, GL_TEXTURE_BINDING_2D=2,
    GL_TEXTURE_RED_SIZE=3, GL_TEXTURE_GREEN_SIZE=4, GL_TEXTURE_BLUE_SIZE=5,
    GL_TEXTURE_ALPHA_SIZE=6;
unsigned int nextTexture = 100;
void glDeleteTextures(int, const unsigned int*) {}
void glBindTexture(int, unsigned int) {}
void glGetTexLevelParameteriv(int, int, int, int* result) { *result = 16; }
int glIsTexture(unsigned int texture) { return texture ? GL_TRUE : 0; }
namespace Raw {
struct ViewportRegion { int identity=0; bool Valid() const { return identity!=0; } };
}
namespace Stack::RawRecipe {
enum class RawDenoiseDiagnosticMode { None, Other };
struct RawDevelopmentRecipe {
    struct { RawDenoiseDiagnosticMode diagnosticMode=RawDenoiseDiagnosticMode::None; } rgbDenoise;
};
}
namespace Stack::Renderer {
namespace RawDevelopmentCache {
struct Fingerprints { std::size_t neutralPlacement; };
Fingerprints BuildStageFingerprints(const Stack::RawRecipe::RawDevelopmentRecipe&,int preview) {
    return {preview ? 502u : 501u};
}
}
unsigned int CopyViewportTexture(unsigned int texture,int,int,int,int) { return texture ? ++nextTexture:0; }
struct ViewportPreviewGeometry {
    int resident=0; bool Valid() const { return resident!=0; }
};
namespace GLState {
struct TextureBinding { TextureBinding(int,int) {} void Restore() const {} };
}
struct ScopedGLTexture {
    unsigned int value=0;
    ScopedGLTexture() = default;
    explicit ScopedGLTexture(unsigned int texture):value(texture) {}
    void Reset(unsigned int texture) { value=texture; }
    unsigned int Get() const { return value; }
    unsigned int Release() { return std::exchange(value,0); }
    explicit operator bool() const { return value!=0; }
};
}
namespace Stack::NodeMath {
enum class ArithmeticDomain { Images, Values };
enum class KnowledgeState { Unknown, Known };
enum class ImageComponent { Alpha };
bool HasImageComponent(int components, ImageComponent) { return components==4; }
}
struct Descriptor {
    struct { Stack::NodeMath::KnowledgeState state=Stack::NodeMath::KnowledgeState::Known; int value=3; } presentImageComponents;
};
namespace EditorNodeGraph {
const std::string kValueOutputSocketId="valueOut", kImageOutputSocketId="imageOut",
    kPreFinishImageOutputSocketId="preFinish", kMatchExtentInputSocketId="extent",
    kReductionFieldInputSocketId="field";
std::string ParameterInputSocketId(const std::string& id) { return "uniform:"+id; }
}
enum class RenderGraphNodeKind { Image, RawSource, RawDevelopment, RawDecode,
    RawDevelop, RawOperation, Layer, Output, MaskGenerator, MaskCombine,
    MaskUtility, CustomMask, ImageToMask, ChannelSplit, ConstantChannel,
    RawDetailAutoMask, RawDetailFusion, HdrMerge, Mfsr, RawProjectSourceSet,
    Lut, Mix, TechnicalImage, Reformat, DataMath, ImageGenerator, ChannelCombine,
    Value, FieldMean };
struct RenderGraphLink { int fromNodeId=0; std::string fromSocketId="imageOut"; Descriptor semanticDescriptor; };
struct Parameter { std::string id="amount"; };
struct RenderGraphNode {
    int nodeId=0;
    RenderGraphNodeKind kind=RenderGraphNodeKind::Image;
    struct { bool enabled=true; } rawOperation;
    struct { bool embeddedRawData=true; } rawDevelopment;
    struct { bool resultAvailable=true,quarantined=false; std::string unavailableStatus; } rawProjectSourceSet;
    struct LayerFlags {
        bool enabled=true, visible=true;
        bool value(const char* key, bool) const {
            return std::string(key)=="enabled" ? enabled : visible;
        }
    } layerJson;
    std::vector<Parameter> uniformParameters;
    int maskCanvasSourceNodeId=0;
    struct { Stack::NodeMath::ArithmeticDomain domain=Stack::NodeMath::ArithmeticDomain::Images; } dataMathSettings;
    double scalarValue=0.5;
    std::string definitionId="fixture";
    std::size_t revision=1;
    bool pending=false, owned=true, fails=false, borrowed=false, splitMissingAlphaIsOpaque=true, repeatInput=false;
    // Fake kernels call these actual evaluators to express sample dependencies.
    std::vector<int> images, masks, scalars;
    std::unordered_map<std::string, RenderGraphLink> links;
};
struct RenderGraphSnapshot {
    std::unordered_map<int,std::string> unavailableOutputs;
    int outputNodeId=1, rawLayerScopeNodeId=0;
    std::string outputSocketId="imageOut", rawLayerScopeSocketId="imageOut";
    bool rawWorkspaceLocalRangeTargetSampleRequested=false;
    std::string rawWorkspaceLocalRangeOverlayMode;
};
struct GraphExecutionContext {
    const std::unordered_map<int,const RenderGraphNode*>& nodes;
    const RenderGraphLink* FindInputLink(int id, const std::string& socket) const {
        auto node=nodes.find(id);
        if(node==nodes.end()) return nullptr;
        auto found=node->second->links.find(socket);
        return found==node->second->links.end() ? nullptr : &found->second;
    }
    bool IsActiveNode(int) const { return true; }
};
bool IsScalarRenderSocket(const GraphExecutionContext&, int, const std::string& socket) {
    return socket=="maskOut" || socket=="field";
}
std::string MakeNodeSocketKey(int id,const std::string& socket) {
    return std::to_string(id)+":"+socket;
}
struct ScheduledGraphOutput { int nodeId; std::string socketId="imageOut"; };
struct FakeMean { double value; std::size_t sampleCount; };
struct Stats {
    int imageCacheHits=0,imageCacheMisses=0,maskCacheHits=0,maskCacheMisses=0,
        reductionCacheHits=0,reductionCacheMisses=0,reductionPasses=0,
        lastSpecializedFailureNodeId=0,lastReductionFailureNodeId=0,rawStageCacheHits=0,rawNativeDenoiseReuses=0;
    std::string lastSpecializedFailure,lastReductionFailure;
    struct Reduction { int id; double value; std::uint64_t samples; bool cached; std::string definition; };
    std::vector<Reduction> reductions;
};
constexpr std::size_t kManualRawSubstageHistoryLimit=2;
bool IsTransientExposureDependentCacheKey(const std::string&) { return false; }
bool IsManualRawSubstageCacheKey(const std::string&) { return false; }
std::size_t ResolveRawDevelopStageCacheMaxEntries(int,int) { return 4; }
class RenderPipeline {
public:
'''
pipeline = r'''
    int m_Width=4,m_Height=4,m_BaseCanvasWidth=4,m_BaseCanvasHeight=4,m_PreviewMaxDimension=2;
    unsigned int m_SourceTexture=10,m_ExternalOutputTexture=0,m_OutputTexture=0,
        m_GraphSourceTexture=0;
    int m_GraphSourceWidth=0,m_GraphSourceHeight=0,referenceSource=0;
    bool m_OutputDenoisePending=false,m_RawDevelopmentCachePrewarmActive=false,
        m_RawDevelopmentCachePrewarmCompleted=false,
        m_HasProvisionalRawRgbDenoiseOutput=false,m_LastRawRgbDenoiseOutputPending=false,
        m_RawDevelopmentGlobalExposureInteraction=false, exposureSucceeds=true;
    // Simulate a still-running shared owner without consulting it for metadata.
    struct OwnerState { bool pending=true, deferred=true; } owner;
    Raw::ViewportRegion m_RawViewportAppliedRegion;
    Stack::Renderer::ViewportPreviewGeometry m_CurrentGraphImageGeometry,m_GraphViewportGeometry;
    std::function<bool()> m_ShouldCancelRender;
    Stats m_LastGraphExecutionStats;
    std::uint64_t m_GraphResourceUseSerial=0,m_RawDevelopStageCacheBudgetBytes=1024*1024,
        m_RawDevelopmentCachePrewarmByteBudget=1024*1024,m_RawDevelopmentCachePrewarmStoredBytes=0;
    std::unordered_map<std::string,CachedGraphTexture> m_GraphImageCache,m_GraphMaskCache;
    std::unordered_map<std::string,CachedGraphScalar> m_GraphScalarCache;
    std::unordered_map<std::string,std::vector<CachedGraphTexture>> m_RawDevelopStageImageCache;
    std::unordered_set<std::string> m_LastGraphImageCacheHits;
    std::function<GraphNodeRenderResult(const RenderGraphNode&)> kernel;
    PRODUCTION_GETTER
    std::uint64_t EstimateGraphTargetBytes(int width,int height) const { return width*height*8; }
    unsigned int CloneTextureForGraphCache(unsigned int source,int,int) { return source ? ++nextTexture:0; }
    unsigned int CloneTextureForRawDevelopStageCache(unsigned int source) { return source ? ++nextTexture:0; }
    void DeleteGraphCacheEntry(CachedGraphTexture&);
    void ReleaseGraphCacheEntry(std::unordered_map<std::string,CachedGraphTexture>&,const std::string&);
    bool StoreGraphCacheEntry(std::unordered_map<std::string,CachedGraphTexture>&,const std::string&,
        unsigned int,std::size_t,bool,bool=false);
    void TouchGraphCacheEntry(CachedGraphTexture&);
    CachedGraphTexture* FindGraphCacheEntry(std::unordered_map<std::string,CachedGraphTexture>&,
        const std::string&,std::size_t);
    CachedGraphTexture FindRawDevelopStageCacheEntry(const std::string&,std::size_t);
    bool StoreRawDevelopStageCacheEntry(const std::string&,unsigned int,std::size_t,bool=false,bool=false);
    void InvalidateRawDevelopStageCacheEntry(const std::string&,std::size_t);
    CachedGraphTexture FindRawViewportDenoiseInput(const std::string&,
        const Stack::RawRecipe::RawDevelopmentRecipe&,bool&);
    void DeleteRawDevelopStageCacheEntry(CachedGraphTexture& entry) { DeleteGraphCacheEntry(entry); }
    std::uint64_t RawDevelopStageCacheTotalBytes() const { return 0; }
    std::uint64_t TrimRawDevelopStageCacheToBudget(std::uint64_t current,std::uint64_t,unsigned int) { return current; }
    void TrimRawDevelopStageCacheVector(std::vector<CachedGraphTexture>&,std::size_t) {}
    int FindReferenceSourceNode(const GraphExecutionContext&,int) const { return referenceSource; }
    unsigned int RenderRawDevelopmentExposure(unsigned int input,float) { return input && exposureSucceeds ? ++nextTexture:0; }
    unsigned int RenderProvisionalRawRgbDenoiseOutput(unsigned int);
    bool RenderChannelSplit(unsigned int,int,unsigned int) { return true; }
    GraphNodeRenderResult RenderRawDevelopmentGraphNode(const RenderGraphNode& node,std::size_t) {
        return kernel(node);
    }
    template<class ImageEval,class MaskEval>
    GraphNodeRenderResult BorrowingKernel(const RenderGraphNode& node,const ImageEval& image,const MaskEval& mask) {
        unsigned int input=0;
        for(int id:node.images) { auto value=image(id,"imageOut"); if(!input) input=value; }
        if(node.rawOperation.enabled) for(int id:node.masks) (void)mask(id,"maskOut");
        // Model delegated ToneCurve-style evaluation after optional masks: the
        // wrapper must retain the FIRST successful input's provenance snapshot.
        if(node.repeatInput && !node.images.empty()) (void)image(node.images.front(),"imageOut");
        return {node.fails ? 0 : node.borrowed ? input : ++nextTexture,
            !node.borrowed,false};
    }
    template<class ImageEval,class MaskEval>
    GraphNodeRenderResult RenderRawOperation(const GraphExecutionContext&,const RenderGraphNode& node,
        const ImageEval& image,const MaskEval& mask) { return BorrowingKernel(node,image,mask); }
    template<class ImageEval,class MaskEval,class Fingerprint>
    GraphNodeRenderResult RenderRawDevelopGraphNode(const GraphExecutionContext&,const RenderGraphNode& node,
        const std::string&,std::size_t,std::unordered_map<std::string,unsigned int>&,
        const ImageEval& image,const MaskEval& mask,const Fingerprint&) { return BorrowingKernel(node,image,mask); }
    GraphNodeRenderResult AdoptRawCached(const CachedGraphTexture& cached) {
        GraphNodeRenderResult result;
        RAW_HIT
        return result;
    }
    void AdoptCropCached(GraphNodeRenderResult& result,const CachedGraphTexture& cached) {
        CROP_HIT
    }
    void PublishCrop(GraphNodeRenderResult& result,const std::string& cacheKey,std::size_t fingerprint) {
        const auto inputTexture=result.texture;
        Stack::Renderer::ScopedGLTexture croppedTexture(++nextTexture);
        const bool cropRendered=true;
        CROP_PUBLISH
    }
    void AdoptRegionCached(GraphNodeRenderResult& result,const CachedGraphTexture& cached) {
        const Raw::ViewportRegion region{7};
        REGION_HIT
    }
    void PublishRegion(GraphNodeRenderResult& result,const std::string& cacheKey,std::size_t upstreamFingerprint) {
        const Raw::ViewportRegion region{7};
        Stack::Renderer::ScopedGLTexture target(++nextTexture);
        REGION_PUBLISH
    }
};
'''
for marker, code in [('PRODUCTION_GETTER',block(header,'bool IsOutputDenoisePending()')),('RAW_HIT',raw_hit),('CROP_HIT',crop_hit),('CROP_PUBLISH',crop_publish),
                     ('REGION_HIT',region_hit),('REGION_PUBLISH',region_publish)]:
    pipeline = pipeline.replace(marker,code)
runtime = r'''
class GraphExecutionRuntime {
public:
    SCOPE
    RenderPipeline& pipeline;
    RenderGraphSnapshot graph;
    std::unordered_map<int,RenderGraphNode> authored;
    std::unordered_map<int,const RenderGraphNode*> nodes;
    GraphExecutionContext executionContext{nodes};
    bool rawDevelopmentSideEffectsRequested=false;
    std::unordered_map<std::string,unsigned int> imageCache,maskCache;
    std::unordered_map<std::string,double> scalarCache;
    std::unordered_map<std::string,bool> imageDenoisePending,maskDenoisePending,scalarDenoisePending;
    std::unordered_map<std::string,std::pair<int,int>> localTextureExtents;
    std::unordered_map<std::string,std::size_t> scalarSampleCounts;
    std::unordered_set<std::string> visitingImages,visitingMasks,visitingScalars;
    std::unordered_map<std::string,Stack::Renderer::ViewportPreviewGeometry> viewportGeometry;
    std::vector<unsigned int> viewportTemporaryTextures;
    std::vector<ScheduledGraphOutput> physicalOutputs;
    DenoiseProvenance denoiseProvenance;
    std::function<unsigned int(int,const std::string&)> evalImage,evalMask;
    std::function<bool(int,const std::string&,double&)> evalScalar;
    std::function<std::size_t(int,const std::string&)> fingerprintImage,fingerprintMask,fingerprintScalar;
    explicit GraphExecutionRuntime(RenderPipeline& owner):pipeline(owner) {
        fingerprintImage=fingerprintMask=fingerprintScalar=[this](int id,const std::string&) {
            return authored.at(id).revision;
        };
        pipeline.kernel=[this](const RenderGraphNode& node) { return RunKernel(node); };
        BindImageEvaluation(); BindMaskEvaluation(); BindScalarEvaluation();
    }
    RenderGraphNode& Add(int id,RenderGraphNodeKind kind=RenderGraphNodeKind::Image) {
        auto& node=authored[id]; node.nodeId=id; node.kind=kind; nodes[id]=&node; return node;
    }
    const RenderGraphLink* findInputLink(int id,const std::string& socket) const {
        return executionContext.FindInputLink(id,socket);
    }
    bool ApplyUniformInputs(const RenderGraphNode& authored,RenderGraphNode&) {
        for(const auto& parameter:authored.uniformParameters) {
            auto link=findInputLink(authored.nodeId,EditorNodeGraph::ParameterInputSocketId(parameter.id));
            double value=0;
            if(link && !evalScalar(link->fromNodeId,link->fromSocketId,value)) return false;
        }
        return true;
    }
    std::pair<int,int> ProjectMaskCanvasExtent(const RenderGraphNode&) const { return {4,4}; }
    void RecordViewportGeometry(int,const std::string&,bool) {}
    void PreparePhysicalSchedule() {}
    unsigned int createTarget() { return ++nextTexture; }
    template<class Callback> bool renderPassToTexture(unsigned int&,Callback&& callback) { return callback(1u); }
    RenderPipeline::GraphNodeRenderResult RunKernel(const RenderGraphNode& node) {
        for(int id:node.images) (void)evalImage(id,"imageOut");
        for(int id:node.masks) (void)evalMask(id,"maskOut");
        for(int id:node.scalars) { double value; (void)evalScalar(id,"valueOut",value); }
        // This is the fake model boundary, not a replacement provenance rule.
        // Pending production RAW results are marked by the extracted passthrough.
        if(node.kind==RenderGraphNodeKind::RawDevelopment && node.pending) {
            const auto texture=pipeline.RenderProvisionalRawRgbDenoiseOutput(10);
            return {texture,true,pipeline.m_LastRawRgbDenoiseOutputPending};
        }
        return {node.fails ? 0 : ++nextTexture,node.owned,false};
    }
    void BindImageEvaluation(); void BindMaskEvaluation(); void BindScalarEvaluation(); void Execute();
};
'''.replace('SCOPE',scope)
checks = r'''
void AddPending(GraphExecutionRuntime& runtime,int id) {
    runtime.Add(id,RenderGraphNodeKind::RawDevelopment).pending=true;
}
void AddMaskAndScalar(GraphExecutionRuntime& runtime,int raw,int mask,int scalar) {
    AddPending(runtime,raw);
    runtime.Add(mask,RenderGraphNodeKind::ImageToMask).images={raw};
    runtime.Add(scalar,RenderGraphNodeKind::FieldMean).links["field"]={mask,"maskOut",{}};
}
void Uniform(GraphExecutionRuntime& runtime,int target,int scalar) {
    auto& node=runtime.authored.at(target);
    node.uniformParameters={{"amount"}};
    node.links["uniform:amount"]={scalar,"valueOut",{}};
}
void CheckOutputIsolation() {
    for(bool mainPending:{false,true}) for(bool auxiliaryFirst:{false,true}) {
        RenderPipeline pipeline;
        GraphExecutionRuntime runtime(pipeline);
        runtime.Add(1,RenderGraphNodeKind::RawDevelopment).pending=mainPending;
        runtime.Add(2,RenderGraphNodeKind::RawDevelopment).pending=!mainPending;
        runtime.Add(3,RenderGraphNodeKind::RawDevelopment).pending=!mainPending;
        runtime.physicalOutputs=auxiliaryFirst
            ? std::vector<ScheduledGraphOutput>{{2},{1}} : std::vector<ScheduledGraphOutput>{{1},{2}};
        runtime.graph.rawLayerScopeNodeId=3;
        pipeline.referenceSource=2;
        runtime.Execute();
        Require(pipeline.IsOutputDenoisePending()==mainPending,
            "Only the selected output may determine pending, across scheduled and later auxiliary roots.");
        Require(pipeline.m_OutputTexture!=0,"Isolation scenario must publish pixels.");
    }
    RenderPipeline pipeline;
    for(int recipe=0;recipe<4;++recipe) {
        GraphExecutionRuntime runtime(pipeline);
        const auto kind=recipe==1 ? RenderGraphNodeKind::RawOperation : recipe==2
            ? RenderGraphNodeKind::Image : RenderGraphNodeKind::RawDevelopment;
        auto& main=runtime.Add(1,kind);
        main.pending=recipe==0; main.revision=recipe+1;
        if(recipe==1) {
            runtime.Add(2); main.images={2}; main.borrowed=true; main.rawOperation.enabled=false;
        }
        runtime.Execute();
        Require(pipeline.IsOutputDenoisePending()==(recipe==0),
            "Disabled, removed, and no-denoise recipes must not inherit a prior owner's pending bit.");
        Require(pipeline.owner.pending && pipeline.owner.deferred,"An old model owner remains pending in this test.");
    }
}
void CheckSourceSetResults() {
    for(bool pending:{false,true}) for(bool available:{false,true}) {
        RenderPipeline pipeline;
        GraphExecutionRuntime runtime(pipeline);
        auto& main=runtime.Add(1,RenderGraphNodeKind::RawProjectSourceSet);
        main.pending=pending; main.rawProjectSourceSet.resultAvailable=available;
        runtime.Execute();
        Require(pipeline.IsOutputDenoisePending()==(pending && available),
            "RAW project source sets must forward developed result provenance only for available output.");
        Require((pipeline.m_OutputTexture!=0)==available,
            "Unavailable fused source-set outputs must not substitute provisional source pixels.");
    }
}
void CheckCachePropagation() {
    RenderPipeline pipeline;
    for(int pass=0;pass<4;++pass) {
        const bool pending=pass<2;
        GraphExecutionRuntime runtime(pipeline);
        AddMaskAndScalar(runtime,1,2,3);
        runtime.authored.at(1).pending=pending;
        runtime.authored.at(3).scalarValue=pending ? 0.5 : 0.0;
        runtime.Add(4).scalars={3};
        // Fingerprints are fixture inputs, not the production hashing under test.
        for(auto& [id,node]:runtime.authored) node.revision=pending ? 1 : 2;
        runtime.graph.outputNodeId=4;
        runtime.Execute();
        Require(pipeline.IsOutputDenoisePending()==pending,"Image to mask to scalar to image must propagate pending.");
        for(int twice=0;twice<2;++twice) {
            Require(runtime.evalImage(1,"imageOut") && runtime.denoiseProvenance.pending==pending,
                "Image local/persistent cache must restore pending.");
            Require(runtime.evalMask(2,"maskOut") && runtime.denoiseProvenance.pending==pending,
                "Mask local/persistent cache must restore pending.");
            double value=0;
            Require(runtime.evalScalar(3,"valueOut",value) && runtime.denoiseProvenance.pending==pending &&
                value==(pending ? 0.5 : 0.0),
                "Scalar local/persistent cache must restore pending, including a valid numeric value.");
        }
        Require(pipeline.m_GraphImageCache.at("1:imageOut").denoisePending==pending &&
            pipeline.m_GraphMaskCache.at("2:maskOut").denoisePending==pending &&
            pipeline.m_GraphScalarCache.at("3:valueOut").denoisePending==pending,
            "All persistent cache kinds must retain provenance with values.");
    }
    Require(pipeline.m_LastGraphExecutionStats.imageCacheHits>0 &&
        pipeline.m_LastGraphExecutionStats.maskCacheHits>0 &&
        pipeline.m_LastGraphExecutionStats.reductionCacheHits>0,
        "The warm pass must actually hit all three production persistent caches.");
    // Uniforms execute before an image persistent lookup. A settled cached
    // result replaces their temporary mark instead of OR-ing a discarded input.
    for(bool settled:{false,true}) {
        RenderPipeline p;
        GraphExecutionRuntime runtime(p);
        AddMaskAndScalar(runtime,1,2,3);
        runtime.Add(4); Uniform(runtime,4,3);
        p.StoreGraphCacheEntry(p.m_GraphImageCache,"4:imageOut",91,1,true,!settled);
        runtime.graph.outputNodeId=4; runtime.Execute();
        Require(p.IsOutputDenoisePending()==!settled,
            "Persistent image provenance must replace uniform prelookup marks.");
        Require(runtime.scalarDenoisePending.at("3:valueOut"),"The prelookup uniform really evaluated pending samples.");
    }
}
void CheckFailureAndExtent() {
    RenderPipeline pipeline;
    GraphExecutionRuntime runtime(pipeline);
    AddPending(runtime,1);
    auto& failed=runtime.Add(2); failed.images={1}; failed.fails=true;
    runtime.Add(3).images={2}; runtime.graph.outputNodeId=3;
    runtime.Execute();
    Require(!pipeline.IsOutputDenoisePending() && !runtime.imageDenoisePending.at("2:imageOut"),
        "A failed child cannot taint a successful consumer that discards its failure.");
    runtime.Add(4,RenderGraphNodeKind::MaskGenerator).links["extent"]={1,"imageOut",{}};
    Require(runtime.evalMask(4,"maskOut") && !runtime.denoiseProvenance.pending,
        "A generated mask must not inherit denoise from an extent-only dependency.");
    runtime.Add(5,RenderGraphNodeKind::ImageToMask).images={1};
    Require(runtime.evalMask(5,"maskOut") && runtime.denoiseProvenance.pending,
        "A sample-dependent mask still must inherit denoise.");
    // Default failure, explicit failed Return, and exceptions all roll back.
    for(int failure=0;failure<3;++failure) {
        GraphExecutionRuntime::DenoiseProvenance state;
        bool parent=false,child=true;
        {
            GraphExecutionRuntime::DenoiseProvenance::Scope outer(state,parent);
            try {
                GraphExecutionRuntime::DenoiseProvenance::Scope inner(state,child);
                state.pending=true;
                if(failure==1) (void)inner.Return(0u);
                if(failure==2) throw std::runtime_error("synthetic child failure");
            } catch(const std::runtime_error&) {}
            outer.Return(1u);
        }
        Require(!child && !parent && !state.pending,"Failed scopes must not publish or propagate pending.");
    }
}
void CheckSynthesizedAlpha() {
    for(int components:{3,4}) for(const std::string socket:{"r","g","b","a"}) {
        RenderPipeline pipeline;
        GraphExecutionRuntime runtime(pipeline);
        AddPending(runtime,1);
        auto& channel=runtime.Add(2,RenderGraphNodeKind::ChannelSplit);
        channel.links["imageIn"]={1,"imageOut",{}};
        channel.links["imageIn"].semanticDescriptor.presentImageComponents.value=components;
        Require(runtime.evalMask(2,socket)!=0 && runtime.denoiseProvenance.pending==(components==4 || socket!="a"),
            "Synthesized opaque alpha must be settled; real RGB and alpha samples retain input provenance.");
    }
}
void CheckBorrowedResults() {
    for(auto kind:{RenderGraphNodeKind::RawOperation,RenderGraphNodeKind::RawDevelop})
    for(bool borrowed:{false,true}) for(bool uniform:{false,true}) for(bool inputPending:{false,true}) {
        RenderPipeline pipeline;
        GraphExecutionRuntime runtime(pipeline);
        auto& input=runtime.Add(1,RenderGraphNodeKind::RawDevelopment); input.pending=inputPending;
        AddMaskAndScalar(runtime,2,3,4);
        auto& target=runtime.Add(5,kind); target.images={1}; target.masks={3}; target.borrowed=borrowed;
        target.repeatInput=true;
        if(uniform) Uniform(runtime,5,4);
        runtime.graph.outputNodeId=5; runtime.Execute();
        Require(pipeline.IsOutputDenoisePending()==(!borrowed || uniform || inputPending),
            "Borrowed RAW wrappers drop unused masks, but retain real input/uniform dependencies and owned unions.");
    }
    RenderPipeline pipeline;
    GraphExecutionRuntime runtime(pipeline);
    runtime.Add(1);
    AddMaskAndScalar(runtime,2,3,4);
    auto& disabled=runtime.Add(5,RenderGraphNodeKind::RawOperation);
    disabled.images={1}; disabled.borrowed=true; disabled.rawOperation.enabled=false;
    Uniform(runtime,5,4); runtime.graph.outputNodeId=5; runtime.Execute();
    Require(!pipeline.IsOutputDenoisePending() && runtime.scalarCache.empty(),
        "Disabled RAW operations must not evaluate denoising uniforms.");
    RenderPipeline hiddenPipeline;
    GraphExecutionRuntime hiddenRuntime(hiddenPipeline);
    hiddenRuntime.Add(1);
    AddMaskAndScalar(hiddenRuntime,2,3,4);
    auto& hidden=hiddenRuntime.Add(5,RenderGraphNodeKind::Layer);
    hidden.images={1}; hidden.layerJson.visible=false;
    Uniform(hiddenRuntime,5,4); hiddenRuntime.graph.outputNodeId=5; hiddenRuntime.Execute();
    Require(!hiddenPipeline.IsOutputDenoisePending() && hiddenRuntime.scalarCache.empty(),
        "Hidden layers must not evaluate denoising uniforms.");
}
void CheckNativeProjection() {
    for(bool pending:{false,true}) for(bool temporaryExpected:{false,true}) {
        RenderPipeline pipeline;
        pipeline.StoreRawDevelopStageCacheEntry("native",++nextTexture,501,true,pending);
        pipeline.m_RawDevelopStageImageCache.at("native").front().viewportNativeDependency=true;
        pipeline.StoreRawDevelopStageCacheEntry("native",++nextTexture,502,true,true);
        if(temporaryExpected) pipeline.m_RawDevelopStageCacheBudgetBytes=0;
        bool temporary=false;
        const auto projected=pipeline.FindRawViewportDenoiseInput("native",{},temporary);
        Require(projected.texture && projected.denoisePending==pending,
            "Only completed native pixels may replace a provisional preview, with settled provenance.");
        Require(temporary==(!pending && temporaryExpected),
            "Native projection fallback must retain correct temporary ownership.");
        Require(pending || projected.width==2,
            "A completed native dependency must produce the requested preview extent.");
        if(!pending && !temporaryExpected)
            Require(projected.viewportNativeDerived,"Cached projection must record native derivation.");
    }
}
void CheckRawStageAndGeometry() {
    for(bool pending:{false,true}) {
        RenderPipeline pipeline;
        const auto texture=pipeline.RenderProvisionalRawRgbDenoiseOutput(10);
        Require(texture && pipeline.m_LastRawRgbDenoiseOutputPending,
            "A real provisional passthrough marks only successful output pending.");
        Require(pipeline.StoreRawDevelopStageCacheEntry("neutral",texture,101,true,pending),
            "RAW stage cache publication must succeed.");
        auto cached=pipeline.FindRawDevelopStageCacheEntry("neutral",101);
        auto result=pipeline.AdoptRawCached(cached);
        Require(result.texture && result.denoisePending==pending,
            "RAW neutral-stage reuse after downstream edits must restore exact provenance.");
        // Publication preserves pending on cold cropped/regional outputs;
        // adoption replaces a deliberately contrary caller's stale bit.
        pipeline.m_Width=2; pipeline.m_Height=3;
        pipeline.PublishCrop(result,"crop",102);
        auto cropped=pipeline.FindRawDevelopStageCacheEntry("crop",102);
        Require(cropped.denoisePending==pending && cropped.width==2 && cropped.height==3,
            "Cropped publication must store provenance with its own extent.");
        result.denoisePending=!pending;
        pipeline.AdoptCropCached(result,cropped);
        Require(result.denoisePending==pending && pipeline.m_Width==2 && pipeline.m_Height==3,
            "Cropped warm-cache adoption must replace stale provenance.");
        pipeline.PublishRegion(result,"region",103);
        auto regional=pipeline.FindRawDevelopStageCacheEntry("region",103);
        result.denoisePending=!pending;
        pipeline.AdoptRegionCached(result,regional);
        Require(result.denoisePending==pending && regional.denoisePending==pending &&
            pipeline.m_RawViewportAppliedRegion.Valid(),
            "Regional publication and warm-cache adoption must preserve exact provenance.");
        // Store replaces an existing fingerprint: completed model results must
        // supersede provisional provenance instead of retaining a sticky bit.
        pipeline.StoreRawDevelopStageCacheEntry("neutral",++nextTexture,101,true,false);
        Require(!pipeline.AdoptRawCached(pipeline.FindRawDevelopStageCacheEntry("neutral",101)).denoisePending,
            "Completed RAW stage replacement must clear provisional provenance.");
    }
    RenderPipeline pipeline;
    pipeline.exposureSucceeds=false;
    Require(!pipeline.RenderProvisionalRawRgbDenoiseOutput(10) && !pipeline.m_LastRawRgbDenoiseOutputPending,
        "Failed provisional output may not publish pending provenance.");
    pipeline.m_OutputDenoisePending=true; pipeline.m_OutputTexture=0;
    Require(!pipeline.IsOutputDenoisePending(),"A failed/reset output cannot expose stale pending metadata.");
    // The cache variant machinery must retain flags with each resident region,
    // rather than borrowing the last active region's metadata.
    pipeline.m_CurrentGraphImageGeometry.resident=1;
    pipeline.StoreGraphCacheEntry(pipeline.m_GraphImageCache,"viewport",201,11,true,true);
    pipeline.m_CurrentGraphImageGeometry.resident=2;
    pipeline.StoreGraphCacheEntry(pipeline.m_GraphImageCache,"viewport",202,12,true,false);
    auto first=pipeline.FindGraphCacheEntry(pipeline.m_GraphImageCache,"viewport",11);
    Require(first && first->texture==201 && first->denoisePending && first->graphGeometry.resident==1,
        "Restoring a regional cache variant must restore its pending metadata.");
    auto second=pipeline.FindGraphCacheEntry(pipeline.m_GraphImageCache,"viewport",12);
    Require(second && second->texture==202 && !second->denoisePending && second->graphGeometry.resident==2,
        "Restoring the settled region must not retain the previous region's bit.");
}
int main() {
    try {
        CheckOutputIsolation(); CheckCachePropagation(); CheckFailureAndExtent(); CheckSourceSetResults();
        CheckBorrowedResults(); CheckRawStageAndGeometry(); CheckSynthesizedAlpha(); CheckNativeProjection();
        std::cout << "Denoise output provenance checks passed (CPU extraction; no GL/model execution).\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
'''
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(pre + cache_structs + pipeline + cache_methods + raw_methods +
                       provisional + runtime + image + mask + scalar + execute + checks)
