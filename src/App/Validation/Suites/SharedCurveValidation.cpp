#include "App/Validation/ValidationImageUtils.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLLoader.h"
#include "Raw/RawLoader.h"
#include "Raw/Curves/CurveAuthoring.h"
#include "Raw/RawAssetCapture.h"
#include "Raw/StrokeValueMatcher.h"
#include "Assets/MaskResampling.h"
#include "Project/RawPendingMask.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Project/RawCapturedMask.h"
#include "Project/GraphImageContracts.h"
#include "Editor/NodeEditors/RawOperationInputCache.h"
#include "Editor/RawLayerTools.h"
#include "Editor/Internal/RawLab/RawLabHistogramCache.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <chrono>

namespace Stack::Validation {
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Check(const std::filesystem::path& path) {
    using namespace RawRecipe;
    auto raw=std::make_shared<Raw::RawImageData>();
    Require(Raw::RawLoader::LoadFile(path.string(),*raw),"Cannot read requested RAW.");
    RenderPipeline pipeline;pipeline.Initialize();pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);pipeline.SetGraphCacheBudget(256ull*1024*1024);
    auto recipe=MakeDefaultRecipe(path.string());recipe.rgbDenoise.enabled=false;
    recipe.technical.mosaicDenoise.enabled=false;recipe.viewTransform.layerJson["enabled"]=false;
    RenderGraphNode source;source.nodeId=1;source.kind=RenderGraphNodeKind::RawDevelopment;
    source.rawDevelopment.recipe=recipe;source.rawDevelopment.embeddedRawData=raw;
    RenderGraphSnapshot graph;graph.nodes.push_back(source);graph.outputNodeId=1;graph.outputSocketId="imageOut";
    int width=0,height=0;
    const auto render=[&](const SceneTone& tone) {
        graph.nodes[0].rawDevelopment.recipe.finishTone.layerJson["luminanceTone"]=SerializeSceneTone(tone);
        pipeline.SetPreviewMaxDimension(768);pipeline.Resize(64,64);pipeline.ExecuteGraph(graph);
        Require(pipeline.GetOutputTexture()!=0 && !pipeline.GetLastGraphExecutionStats().allocationFailed,"Curve GPU render failed.");
        width=pipeline.GetCanvasWidth();height=pipeline.GetCanvasHeight();
        auto pixels=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),width,height);
        Require(!pixels.empty(),"Curve GPU readback was empty.");
        for(float value:pixels)Require(std::isfinite(value),"Curve GPU output contains non-finite values.");
        return pixels;
    };
    pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::FinishToneInput,192);
    // Unified graph tools request their measurement through an explicit image
    // endpoint, even when whole-recipe diagnostics are disabled.
    graph.rawLayerScopeNodeId=1;graph.rawLayerScopeSocketId="imageOut";
    const auto baseline=render({});
    {
        using namespace Editor::RawLabInternal;
        const auto& scope=pipeline.GetRawDevelopmentGraphScopeReadback();
        Require(scope.valid,"Real-image tone measurements are unavailable.");
        RawLabHistogramCache cache;
        const auto equal=[](const auto& a,const auto& b) {
            return a.valid==b.valid && a.luma==b.luma && a.red==b.red && a.green==b.green && a.blue==b.blue;
        };
        for(float low:{-16.f,-8.f}) {
            const auto direct=BuildRawLabFinishToneHistogram(scope,scope.stage,recipe.technical.workingSpace,1,low,16,.18f);
            Require(equal(direct,cache.FinishTone(scope,1,scope.stage,recipe.technical.workingSpace,1,low,16,.18f)),
                "Caching or changing the plotted domain changed histogram bins.");
            Require(equal(direct,cache.FinishTone(scope,1,scope.stage,recipe.technical.workingSpace,1,low,16,.18f)),
                "Repeated histogram reuse changed its bins.");
            Require(equal(direct,cache.FinishTone(scope,2,scope.stage,recipe.technical.workingSpace,1,low,16,.18f)),
                "A new measurement revision changed histogram bins.");
        }
        Require(!cache.FinishTone({},0,scope.stage,recipe.technical.workingSpace,1,-8,16,.18f).valid,
            "Clearing measurements retained the old histogram.");
        std::cout<<"Real-image histogram cache: exact bins, domain changes, new publications and clearing passed.\n";
    }
    pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::None,0);
    graph.rawLayerScopeNodeId=0;graph.rawLayerScopeSocketId.clear();
    SceneTone tone;tone.curve={{0,0,{1},{1}},{.35f,.31f,{1},{1}},{.55f,.62f,{1},{1}},{1,1,{1},{1}}};
    const auto edited=render(tone);Require(edited.size()==baseline.size(),"Curve changed image dimensions.");
    double squared=0;size_t count=0;float peak=0;
    for(size_t i=0;i+3<baseline.size();i+=4) {
        const std::array<float,3> rgb{baseline[i],baseline[i+1],baseline[i+2]};
        const auto expected=ApplySceneTone(tone,rgb,{.2627002f,.6779981f,.0593017f});
        const float baseY=.2627002f*rgb[0]+.6779981f*rgb[1]+.0593017f*rgb[2];
        const float expectedY=.2627002f*expected[0]+.6779981f*expected[1]+.0593017f*expected[2];
        const float actualY=.2627002f*edited[i]+.6779981f*edited[i+1]+.0593017f*edited[i+2];
        if(baseY>1e-5f && expectedY>1e-5f && actualY>1e-5f) {
            const float error=std::abs(std::log2(actualY/expectedY));squared+=error*error;peak=std::max(peak,error);++count;
        }
    }
    const double rmse=std::sqrt(squared/std::max<size_t>(1,count));
    std::cout<<"Direct tone GPU/CPU EV RMSE "<<rmse<<", peak "<<peak<<", pixels "<<count<<'\n';
    Require(count>100 && rmse<.015,"Direct tone GPU/CPU results disagree.");
    tone.curve=AttenuateCurve(tone.curve,0,true);const auto neutral=render(tone);
    float difference=0;for(size_t i=0;i<neutral.size();++i)difference=std::max(difference,std::abs(neutral[i]-baseline[i]));
    std::cout<<"Tone amount zero maximum difference "<<difference<<'\n';
    Require(difference<.0001f,"Tone amount zero changed the source.");
    auto& local=graph.nodes[0].rawDevelopment.recipe.localRange;
    local.enabled=true;
    local.points={{-8,0,{1},{1}},{-2,1,{1},{1}},{2,-.5f,{1},{1}},{6,0,{1},{1}}};
    const auto localEdited=render({});
    float localDifference=0;
    for(size_t i=0;i<baseline.size();++i)localDifference=std::max(localDifference,std::abs(localEdited[i]-baseline[i]));
    Require(localDifference>.0001f,"Local EV authored curve did not affect GPU output.");
    const auto localNeutral=AttenuateCurve(RawLocalRangeBezierPoints(local),0,false);
    local.points.clear();
    for(const auto& point:localNeutral)local.points.push_back({local.minEv+point.x*(local.maxEv-local.minEv),-4+8*point.y,point.incoming,point.outgoing});
    const auto localRestored=render({});
    difference=0;
    for(size_t i=0;i<baseline.size();++i)difference=std::max(difference,std::abs(localRestored[i]-baseline[i]));
    std::cout<<"Local EV edited maximum difference "<<localDifference<<"; amount zero difference "<<difference<<'\n';
    Require(difference<.0001f,"Local EV amount zero changed the source.");
    {
        // Moving exposure into Local EV must not change which input tones its
        // histogram and image probe report for the same rendered curve.
        RenderGraphSnapshot scopeGraph;
        scopeGraph.nodes.push_back(source);
        RenderGraphNode operation;operation.nodeId=3;operation.kind=RenderGraphNodeKind::RawOperation;
        operation.rawOperation=MakeGraphOperation(GraphOperationKind::LocalEv);
        operation.rawWorkingSpace=recipe.technical.workingSpace;
        auto localRecipe=recipe;localRecipe.preToneExposureEv=1.f;
        localRecipe.localRange.enabled=true;
        localRecipe.localRange.points={{-8,0,{1},{1}},{-2,1,{1},{1}},{2,-.5f,{1},{1}},{6,0,{1},{1}}};
        WriteGraphOperation(operation.rawOperation,localRecipe);
        scopeGraph.nodes.push_back(operation);
        scopeGraph.links.push_back({1,"imageOut",3,"imageIn"});
        scopeGraph.outputNodeId=3;scopeGraph.outputSocketId="imageOut";
        scopeGraph.rawLayerScopeNodeId=1;scopeGraph.rawLayerScopeSocketId="imageOut";
        scopeGraph.rawLayerScopeOperationId=3;
        {
            Editor::RawOperationInputCache interpretations;
            const Project::NodeEditTarget target{"curve-validation",Project::kRawBackgroundId,"local-ev"};
            const GraphModel::Endpoint input{Project::kRawBackgroundId,"source","imageOut"};
            for(const auto space:{Raw::RawWorkingSpace::LinearSrgbD65,Raw::RawWorkingSpace::LinearRec2020D65}) {
                auto contractGraph=scopeGraph;
                contractGraph.nodes[0].semanticDescriptor=Project::RawLayerSceneDescriptor(space);
                Project::BindGraphImageContracts(contractGraph);
                const auto interpreted=Project::RawOperationInputWorkingSpace(contractGraph.links[0].semanticDescriptor);
                Require(interpreted && *interpreted==space && contractGraph.nodes[1].rawWorkingSpace==space,
                    "The RAW operation ignored its actual input color basis.");
                interpretations.Store(target,input,interpreted);
                const auto* cached=interpretations.Find(target,input);
                Require(cached && cached->workingSpace==interpreted,"RAW input interpretation cache changed color basis.");
            }
            Require(!interpretations.Find({"other-project",target.layerId,target.nodeUuid},input) &&
                !interpretations.Find(target,{input.graphId,"another-source",input.portId}),
                "RAW input interpretation leaked across projects or rewired endpoints.");
            auto unsupported=scopeGraph;
            unsupported.nodes[0].semanticDescriptor=Project::RawLayerSceneDescriptor(recipe.technical.workingSpace);
            unsupported.nodes[0].semanticDescriptor.color.value.identity="display-p3-d65";
            Project::BindGraphImageContracts(unsupported);
            Require(unsupported.unavailableOutputs.count(3)!=0,"Unsupported RAW input color silently used the project basis.");
            unsupported=scopeGraph;
            Project::BindGraphImageContracts(unsupported);
            Require(unsupported.unavailableOutputs.count(3)!=0,"Unknown RAW input color silently used the project basis.");
            std::cout<<"RAW input basis: supported spaces, editor cache identity and unsupported input diagnostics passed.\n";
        }
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::LocalRangeInput,192);
        pipeline.ExecuteGraph(scopeGraph);
        const auto internalScope=pipeline.GetRawDevelopmentGraphScopeReadback();
        const auto internalPixels=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),pipeline.GetCanvasWidth(),pipeline.GetCanvasHeight());
        localRecipe.preToneExposureEv=0;
        WriteGraphOperation(scopeGraph.nodes[1].rawOperation,localRecipe);
        RenderGraphNode exposure;exposure.nodeId=2;exposure.kind=RenderGraphNodeKind::RawOperation;
        exposure.rawOperation=MakeGraphOperation(GraphOperationKind::Exposure);
        exposure.rawWorkingSpace=recipe.technical.workingSpace;
        auto exposureRecipe=recipe;exposureRecipe.preToneExposureEv=1.f;
        WriteGraphOperation(exposure.rawOperation,exposureRecipe);
        scopeGraph.nodes.push_back(exposure);
        scopeGraph.links={{1,"imageOut",2,"imageIn"},{2,"imageOut",3,"imageIn"}};
        scopeGraph.rawLayerScopeNodeId=2;
        pipeline.ExecuteGraph(scopeGraph);
        const auto& precedingScope=pipeline.GetRawDevelopmentGraphScopeReadback();
        const auto precedingPixels=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),pipeline.GetCanvasWidth(),pipeline.GetCanvasHeight());
        Require(internalScope.valid && precedingScope.valid && !internalScope.controlSignal.empty() &&
            internalScope.controlSignal.size()==precedingScope.controlSignal.size(),"Local EV exposure measurements are unavailable.");
        float signalDifference=0,pixelDifference=0;
        for(size_t i=0;i<internalScope.controlSignal.size();++i)
            signalDifference=std::max(signalDifference,std::abs(internalScope.controlSignal[i]-precedingScope.controlSignal[i]));
        Require(!internalPixels.empty() && internalPixels.size()==precedingPixels.size(),"Local EV exposure output dimensions disagree.");
        for(size_t i=0;i<internalPixels.size();++i)
            pixelDifference=std::max(pixelDifference,std::abs(internalPixels[i]-precedingPixels[i]));
        Require(signalDifference<.0001f && pixelDifference<.0001f,
            "Local EV histogram/probe measures a different exposure than the rendered curve input.");
        std::cout<<"Local EV internal/preceding exposure: control signal difference "<<signalDifference
            <<", pixel difference "<<pixelDifference<<'\n';
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::None,0);
    }
    RawAssetCapture::Selection stroke;stroke.shape=RawAssetCapture::Shape::Stroke;
    stroke.path={{.15f,.15f},{.4f,.65f},{.8f,.3f}};
    const auto data=RawAssetCapture::Capture(baseline,width,height,recipe.technical.workingSpace,stroke,{});
    Require(data.samples && data.samples->width>100,"Real-image stroke did not retain intervening pixels.");
    stroke.output=RawAssetCapture::Output::ColorMatch;
    const auto mask=RawAssetCapture::Capture(baseline,width,height,recipe.technical.workingSpace,stroke,{});
    Require(mask.data && mask.data->Valid(true),"Real-image color match mask is invalid.");
    {
        using RawAssetCapture::StrokeValueMatcher;
        for(int mode=0;mode<3;++mode) {
            StrokeValueMatcher match(mode!=1,mode!=0,1);
            match.Add({.18f,0,0});match.Add({2.f,.4f,.4f});match.Build();
            Require(match.Coverage({.18f,0,0})==1,"An exact captured value lost full coverage.");
            std::array<float,3> near{.18f,0,0};
            if(mode!=1) near[0]=float(.18*(std::pow(2.,1+.15*.25)-1));
            else near[1]=.005f;
            const float coverage=match.Coverage(near);
            Require(coverage>.8f && coverage<.9f,"Matching still quantizes coverage or shifts the exact samples.");
            Require(match.Coverage({.7f,.2f,.2f})==0,"Separated sample groups filled the unsampled gap.");
        }
        for(auto output:{RawAssetCapture::Output::LumaMatch,RawAssetCapture::Output::BothMatch}) {
            auto selection=stroke;selection.output=output;
            const auto captured=RawAssetCapture::Capture(baseline,width,height,recipe.technical.workingSpace,selection,{});
            Require(std::any_of(captured.data->values.begin(),captured.data->values.end(),[](float x){return x>.001f && x<.999f && std::abs(x-.5f)>.01f;}),
                "Real-image match coverage still has only three levels.");
        }
        const auto reduced=Assets::ResampleMaskCoverage(mask.data->values.data(),width,height,width/2,height/2);
        double expected=0;for(int y=0;y<2;++y)for(int x=0;x<2;++x)expected+=mask.data->values[size_t(y)*width+x]/4.;
        Require(std::abs(reduced[0]-expected)<.000001,"Mask reduction did not preserve pixel-area coverage.");
        const auto native=Assets::ResampleMaskCoverage(mask.data->values.data(),width,height,width,height,true);
        Require(native[0]==mask.data->values[size_t(height-1)*width],"Native mask resampling changed pixels or orientation.");
        std::cout<<"Continuous brightness/color matching, separate ranges and coverage-preserving resampling passed.\n";
    }
    using namespace Project;
    RawLayerStackState state;
    const auto initial=FindRawOperation(state.background,GraphOperationKind::LocalEv)->instanceUuid;
    std::string error;
    const auto applied=ApplyRawCapturedMask(state,kRawBackgroundId,initial,*mask.data,"Captured coverage",error);
    Require(!applied.operationUuid.empty() && applied.operationUuid!=initial,error.c_str());
    const auto* publication=FindRawMaskOutput(state,applied.mask);
    const auto* texture=publication?FindRawPublishedNode(state.background,*publication):nullptr;
    Require(texture && texture->customMask.rasterLayer==mask.data->values,"Captured pixels changed entering Graph.");
    const auto* receiving=FindRawOperation(state.background,GraphOperationKind::LocalEv,applied.operationUuid);
    const auto* attachment=state.background.graph.FindInputLink(receiving->id,"maskIn");
    const auto* blur=attachment?state.background.graph.FindNode(attachment->fromNodeId):nullptr;
    Require(blur && blur->kind==EditorNodeGraph::NodeKind::Layer &&
        blur->layerParameters.value("type",std::string{})=="GaussianBlur" &&
        blur->layerParameters.value("amount",0.f)==100.f,
        "Captured mask did not create Gaussian Blur at 100.");
    Require(state.background.graph.FindInputLink(blur->id,"imageIn")->fromNodeId==texture->id,
        "The default blur is not between capture and operation.");
    const auto stateCopy=state;
    const auto* textureCopy=FindRawPublishedNode(stateCopy.background,*FindRawMaskOutput(stateCopy,applied.mask));
    Require(textureCopy->customMask.rasterLayer.SharesStorage(texture->customMask.rasterLayer),
        "Copying the document duplicated immutable mask pixels.");
    const auto comparisonStarted=std::chrono::steady_clock::now();
    Require(SerializeRawLayerStack(state,false)==SerializeRawLayerStack(stateCopy,false),"Complete saved-mask comparison failed.");
    const auto comparisonMiddle=std::chrono::steady_clock::now();
    Require(SameRawLayerStackState(state,stateCopy),"Lightweight mask history comparison failed.");
    const auto comparisonEnded=std::chrono::steady_clock::now();
    std::cout<<"Real captured-mask unchanged-state comparison: complete serialization "
        <<std::chrono::duration<double,std::milli>(comparisonMiddle-comparisonStarted).count()
        <<" ms; shared-raster comparison "
        <<std::chrono::duration<double,std::milli>(comparisonEnded-comparisonMiddle).count()<<" ms.\n";
    auto replaced=stateCopy;
    auto* changedTexture=replaced.background.graph.FindNode(textureCopy->id);
    auto changedPixels=changedTexture->customMask.rasterLayer.Values();
    changedPixels[0]=changedPixels[0]>.5f?0.f:1.f;
    changedTexture->customMask.rasterLayer=std::move(changedPixels);
    Require(!SameRawLayerStackState(state,replaced) && texture->customMask.rasterLayer==mask.data->values,
        "Replacing mask pixels changed a retained document or escaped comparison.");
    RawLayerStackDocument history;
    Require(history.Apply(state,error),error.c_str());
    history.BeginGesture();
    auto historyCandidate=history.State();
    historyCandidate.background.graph.FindNode(textureCopy->id)->customMask.invert=true;
    Require(history.Apply(std::move(historyCandidate),error),error.c_str());
    history.EndGesture();
    Require(history.Undo(&error),error.c_str());
    const auto* undone=FindRawPublishedNode(history.State().background,*FindRawMaskOutput(history.State(),applied.mask));
    Require(undone && !undone->customMask.invert && undone->customMask.rasterLayer.SharesStorage(texture->customMask.rasterLayer),
        "Undo changed coverage or duplicated retained raster storage.");
    {
        RawLayerStackDocument pendingHistory;
        auto pendingState=pendingHistory.State();
        Assets::FloatData black;black.width=black.height=black.channels=1;black.units="coverage";black.values={0};
        const auto original=FindRawOperation(pendingState.background,GraphOperationKind::LocalEv)->instanceUuid;
        const auto inserted=ApplyRawCapturedMask(pendingState,kRawBackgroundId,original,black,"Unfinished capture",error);
        Require(!inserted.operationUuid.empty(),error.c_str());
        const auto* pendingMask=FindRawPublishedNode(pendingState.background,*FindRawMaskOutput(pendingState,inserted.mask));
        const auto maskUuid=pendingMask->instanceUuid;const int maskId=pendingMask->id;
        pendingState.background.graph.FindNode(maskId)->rawCoverage["captureToken"]="test-capture";
        Require(pendingHistory.Apply(pendingState,error),error.c_str());
        RawPendingMask pending{pendingHistory.State(),kRawBackgroundId,inserted.operationUuid,maskUuid,"test-capture"};
        auto edited=pendingHistory.State();
        auto changed=ReadRawLayerOperation(edited.background,inserted.operationUuid);changed.preToneExposureEv=1;
        Require(WriteRawLayerOperation(edited.background,inserted.operationUuid,changed,error),error.c_str());
        Require(pending.Matches(edited),"Editing the new adjustment invalidated its capture input.");
        Require(pendingHistory.Apply(edited,error),error.c_str());
        auto upstream=edited;changed=ReadRawLayerOperation(upstream.background,original);changed.preToneExposureEv=2;
        Require(WriteRawLayerOperation(upstream.background,original,changed,error),error.c_str());
        Require(!pending.Matches(upstream),"An upstream edit was accepted as unchanged capture input.");
        auto completedAsset=mask;completedAsset.id="completed-capture";completedAsset.name="Brightness match";
        Require(pendingHistory.CompleteMaskCapture(kRawBackgroundId,maskUuid,"test-capture",completedAsset,error),error.c_str());
        const auto& completedRaster=pendingHistory.State().background.graph.FindNode(maskId)->customMask.rasterLayer;
        Require(completedRaster.data()==mask.data->values.data() && completedRaster.Fingerprint()==mask.data->Fingerprint(),
            "Completing a captured mask copied its immutable asset pixels or changed its identity.");
        Require(ReadRawLayerOperation(pendingHistory.State().background,inserted.operationUuid).preToneExposureEv==1,
            "Capture completion overwrote edits made while pending.");
        Require(pendingHistory.Undo(&error),error.c_str());
        Require(pendingHistory.State().background.graph.FindNode(maskId)->customMask.rasterLayer==mask.data->values,
            "Undo restored unfinished coverage after successful completion.");
        Require(pendingHistory.Undo(&error),error.c_str());
        Require(!pending.Matches(pendingHistory.State()),"Undoing creation retained the capture target.");
        Require(!pendingHistory.CompleteMaskCapture(kRawBackgroundId,maskUuid,"test-capture",completedAsset,error),
            "Late completion resurrected an undone mask.");
        Require(pendingHistory.Redo(&error),error.c_str());
        Require(pendingHistory.State().background.graph.FindNode(maskId)->customMask.rasterLayer==mask.data->values,
            "Redo did not restore completed mask pixels.");
        std::cout<<"Pending capture edits, source invalidation, completion, undo/redo and late-result rejection passed.\n";
    }
    Require(state.background.maskOutputs.size()==1,"Capture left a disposable shape generator in Graph.");
    Require(Editor::RawLayerToolNodes(state.background.graph).size()==5,"The captured texture appeared in Alt.");
    graph.nodes[0].rawDevelopment.recipe=recipe;
    graph.nodes.resize(1);graph.outputNodeId=1;graph.outputSocketId="imageOut";
    const bool lowered=LowerRawLayerStack(graph,state,1,1,error);
    Require(lowered,error.c_str());
    const auto compiled=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& node){return node.kind==RenderGraphNodeKind::CustomMask;});
    Require(compiled!=graph.nodes.end() && compiled->customMask.rasterLayer.SharesStorage(texture->customMask.rasterLayer),
        "Lowering a graph copied immutable mask pixels into its worker request.");
    const bool thumbnailConfigured=ConfigureRawLayerThumbnail(graph,kRawBackgroundId,applied.mask.outputId,error);
    Require(thumbnailConfigured,error.c_str());
    pipeline.Resize(17,11); // A previous branch must not determine a captured mask's extent.
    pipeline.SetPreviewMaxDimension(768);pipeline.ExecuteGraph(graph);
    Require(pipeline.GetCanvasWidth()==width && pipeline.GetCanvasHeight()==height,"Captured mask inherited a previous branch extent.");
    const auto preview=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),pipeline.GetCanvasWidth(),pipeline.GetCanvasHeight());
    Require(!preview.empty(),"Captured mask GPU preview is empty.");
    float minimum=1,maximum=0;
    for(size_t i=0;i+3<preview.size();i+=4) {
        Require(std::isfinite(preview[i]) && preview[i]>=0 && preview[i]<=1,
            "Captured mask GPU coverage is invalid.");
        minimum=std::min(minimum,preview[i]);maximum=std::max(maximum,preview[i]);
    }
    Require(maximum>minimum+.1f,"Captured mask preview lost its coverage variation.");
    pipeline.SetPreviewMaxDimension(384);pipeline.ExecuteGraph(graph);
    Require(pipeline.GetCanvasWidth()==width/2 && pipeline.GetCanvasHeight()==height/2,
        "Captured mask did not follow the image preview resolution.");
    const auto half=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),pipeline.GetCanvasWidth(),pipeline.GetCanvasHeight());
    const auto reference=Assets::ResampleMaskCoverage(mask.data->values.data(),width,height,width/2,height/2,true);
    for(size_t i=0;i<reference.size();++i) Require(std::abs(half[i*4]-reference[i])<.00001f,"GPU mask upload lost filtered coverage.");
    pipeline.SetPreviewMaxDimension(768);pipeline.ExecuteGraph(graph);
    Require(pipeline.GetCanvasWidth()==width && pipeline.GetCanvasHeight()==height,
        "A smaller cached mask survived a higher-resolution request.");
    Require(pipeline.GetLastGraphExecutionStats().maskCacheMisses==0,
        "Returning to an unchanged mask extent regenerated coverage.");
    {
        auto blurred=graph;
        const auto node=std::find_if(blurred.nodes.begin(),blurred.nodes.end(),[](const auto& n){
            return n.kind==RenderGraphNodeKind::Layer && n.layerJson.value("type",std::string{})=="GaussianBlur";});
        Require(node!=blurred.nodes.end(),"Blur was omitted from the render graph.");
        blurred.outputNodeId=node->nodeId;blurred.outputSocketId="imageOut";
        const auto start=std::chrono::steady_clock::now();
        pipeline.ExecuteGraph(blurred);glFinish();
        const auto pixels=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),width,height);
        const auto end=std::chrono::steady_clock::now();
        Require(pixels.size()==size_t(width)*height*4,"Blur did not preserve the requested extent.");
        double peak=0;
        // Independent reference to the former 2D Gaussian on real captured
        // coverage, including clamped borders. No downsampling approximation.
        for(int y:{0,height/2,height-1}) for(int x:{0,width/3,width-1}) {
            double sum=0,weightSum=0;
            for(int dy=-100;dy<=100;++dy) for(int dx=-100;dx<=100;++dx) {
                const double w=std::exp(-double(dx*dx+dy*dy)/5000.);
                sum+=preview[(size_t(std::clamp(y+dy,0,height-1))*width+std::clamp(x+dx,0,width-1))*4]*w;
                weightSum+=w;
            }
            peak=std::max(peak,std::abs(pixels[(size_t(y)*width+x)*4]-sum/weightSum));
        }
        Require(peak<.0001,"Separable blur changed Gaussian coverage.");
        const auto reuseStart=std::chrono::steady_clock::now();
        pipeline.ExecuteGraph(blurred);glFinish();
        const auto reused=std::chrono::steady_clock::now();
        Require(pipeline.GetLastGraphExecutionStats().maskCacheMisses==0 &&
            pipeline.GetLastGraphExecutionStats().imageCacheMisses==0,"Unchanged blur was rendered again.");
        pipeline.SetPreviewMaxDimension(384);pipeline.ExecuteGraph(blurred);
        pipeline.SetPreviewMaxDimension(768);pipeline.ExecuteGraph(blurred);
        Require(pipeline.GetLastGraphExecutionStats().maskCacheMisses==0 &&
            pipeline.GetLastGraphExecutionStats().imageCacheMisses==0,"Changing preview extent discarded unchanged blur.");
        std::cout<<"Captured Gaussian 100: "<<std::chrono::duration<double,std::milli>(end-start).count()
            <<" ms with readback; reused "<<std::chrono::duration<double,std::milli>(reused-reuseStart).count()
            <<" ms; Gaussian reference peak error "<<peak<<".\n";
        pipeline.ExecuteGraph(graph);
    }
    int thumbnailWidth=0,thumbnailHeight=0;
    const auto thumbnail=pipeline.GetMaskPreviewPixels(thumbnailWidth,thumbnailHeight,128);
    Require(!thumbnail.empty() && thumbnailWidth>0 && thumbnailHeight>0,"Captured mask thumbnail is empty.");
    unsigned char low=255,high=0;
    for(size_t i=0;i+3<thumbnail.size();i+=4) {
        Require(thumbnail[i]==thumbnail[i+1] && thumbnail[i]==thumbnail[i+2] && thumbnail[i+3]==255,
            "Captured mask thumbnail is not opaque grayscale.");
        low=std::min(low,thumbnail[i]);high=std::max(high,thumbnail[i]);
    }
    Require(int(high)-int(low)>25,"Captured mask thumbnail lost its coverage variation.");
    const auto layerId=AddRawAdjustmentLayer(state);
    auto* other=FindRawAdjustmentLayer(state,layerId);
    const int adjustment=other->graph.AddRawOperationNode(GraphOperationKind::Exposure,{})->id;
    const auto uuid=other->graph.FindNode(adjustment)->instanceUuid;
    const auto direct=ApplyRawCapturedMask(state,layerId,uuid,*mask.data,"Second coverage",error);
    Require(direct.operationUuid==uuid,"A captured mask duplicated a non-Background operation.");
    {
        Assets::FloatData wide;wide.width=8193;wide.height=wide.channels=1;wide.units="coverage";
        wide.values.assign(mask.data->values.begin(),mask.data->values.begin()+wide.width);
        auto wideState=state;
        const auto large=ApplyRawCapturedMask(wideState,kRawBackgroundId,initial,wide,"Native-width coverage",error);
        Require(!large.operationUuid.empty(),error.c_str());
        RawLayerStackState restored;
        Require(DeserializeRawLayerStack(SerializeRawLayerStack(wideState),restored,error),error.c_str());
        const auto* restoredMask=FindRawPublishedNode(restored.background,*FindRawMaskOutput(restored,large.mask));
        Require(restoredMask && restoredMask->customMask.width==wide.width && restoredMask->customMask.height==1,
            "A native captured mask was truncated at the old painting-canvas limit.");
        for(size_t i=0;i<wide.values.size();++i) Require(std::abs(restoredMask->customMask.rasterLayer[i]-wide.values[i])<=1.f/65535,
            "High-bit-depth mask persistence lost coverage precision.");
    }
    RawLayerStackState reopened;
    const bool reopenedOk=DeserializeRawLayerStack(SerializeRawLayerStack(state),reopened,error);
    Require(reopenedOk,error.c_str());
    Require(FindRawMaskOutput(reopened,applied.mask)!=nullptr &&
        FindRawAdjustmentLayer(reopened,layerId)->graph.FindInputLink(adjustment,"maskIn")!=nullptr,
        "Captured coverage failed save/reopen.");
    std::cout<<"Captured mask application, output-only texture, GPU grayscale thumbnail and save/reopen passed.\n";
    std::cout<<"Real-image stroke samples "<<data.samples->width<<"; color mask "<<mask.data->width<<'x'<<mask.data->height<<" passed.\n";
}
}
bool ValidateSharedCurves(const std::filesystem::path& path) {
    if(!glfwInit())return false;
    glfwDefaultWindowHints();glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(64,64,"Shared curves validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}glfwMakeContextCurrent(window);
    bool passed=false;
    try{Require(LoadGLFunctions(),"OpenGL initialization failed.");Check(path);passed=true;}
    catch(const std::exception& error){std::cerr<<"Shared curves validation failed: "<<error.what()<<'\n';}
    glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);glfwTerminate();return passed;
}
}
