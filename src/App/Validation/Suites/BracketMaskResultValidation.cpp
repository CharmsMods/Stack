#include "App/Validation/ValidationImageUtils.h"
#include "Project/BracketGraphMasks.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "Raw/DepthMaskManifest.h"
#include "Raw/RawLoader.h"
#include "Raw/RawMetadataSerialization.h"
#include "Persistence/BracketDngMaster.h"
#include "Persistence/BracketMaster.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/MultiFrameProjectCreation.h"
#include "Persistence/BracketRgbMaster.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>

namespace Stack::Validation {
namespace {
void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}

void VerifyMaster(const std::filesystem::path& input,const std::filesystem::path& directory) {
    using namespace Project;
    std::filesystem::create_directories(directory);
    const auto copy=directory/("copied"+input.extension().string());
    std::filesystem::copy_file(input,copy,std::filesystem::copy_options::overwrite_existing);
    Raw::RawImageData pixels;
    Check(Raw::RawLoader::LoadFile(copy.u8string(),pixels),pixels.metadata.error);
    const auto catalog=ReadBracketMasterMaskSource(copy);
    Check(catalog && catalog->reconstructionDepth && pixels.reconstructionDepth,"Independent master directory unavailable");
    Check(catalog->reconstructionDepth->masks.size()==pixels.reconstructionDepth->masks.size(),"Master mask count changed");
    RawLayerStackState state;Check(OfferBracketMasks(state,*catalog),"Could not restore standalone graph");
    RenderGraphNode source;source.nodeId=1;source.kind=RenderGraphNodeKind::RawDevelopment;
    source.rawDevelopment.recipe=RawRecipe::MakeDefaultRecipe(copy.u8string());
    RenderGraphSnapshot graph;graph.nodes.push_back(source);graph.outputNodeId=1;
    std::string error;
    Check(LowerRawLayerStack(graph,state,1,1,error,0,120,30,nullptr,nullptr,catalog.get()),error);
    for(std::size_t i=0;i<catalog->reconstructionDepth->masks.size();++i) {
        const auto& m=catalog->reconstructionDepth->masks[i];
        const auto& stored=pixels.reconstructionDepth->masks[i];
        Check(!m.pixels && m.id==stored.id && *m.Read()==*stored.Read(),"Standalone lazy coverage differs");
        Check(ConfigureRawLayerThumbnail(graph,kRawBackgroundId,state.background.maskOutputs.at(i).id,error),error);
        const auto n=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.nodeId==graph.outputNodeId;});
        Check(n!=graph.nodes.end() && n->customMask.depthMask && n->customMask.depthMask->id==m.id &&
            n->customMask.depthOrientation==pixels.metadata.orientation,"Standalone graph bound an incorrect mask");
    }
    std::cout<<"Opened copied "<<input.extension().string()<<" with "<<state.background.maskOutputs.size()
        <<" registered mask sources at orientation "<<pixels.metadata.orientation<<"."<<std::endl;
}
void Verify(const std::filesystem::path& file,unsigned groups,const std::filesystem::path& dir) {
    using namespace Project;
    Raw::Bracketing::BracketingResult result;
    std::ifstream stream(file,std::ios::binary);
    Raw::Bracketing::Serialization::ReadResult(stream,std::filesystem::file_size(file),groups,result,dir/"inspection");
    std::filesystem::create_directories(dir);
    auto raw=result.raw;
    const auto depth=raw->reconstructionDepth;
    Check(bool(depth),"Missing depth status");
    std::cout<<depth->message<<"\n";
    auto recipe=RawRecipe::MakeDefaultRecipe("bracketing://result");recipe.rgbDenoise.enabled=false;
    recipe.technical.mosaicDenoise.enabled=false;
    RenderPipeline pipeline;pipeline.Initialize();pipeline.SetPreviewMaxDimension(1000);
    pipeline.SetRawDevelopmentAnalysisEnabled(false);pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    RenderGraphNode source;source.nodeId=1;source.kind=RenderGraphNodeKind::RawDevelopment;
    source.rawDevelopment.recipe=recipe;source.rawDevelopment.embeddedRawData=raw;
    RawLayerStackState state;std::string error;unsigned revision=0;
    const auto lower=[&]{
        RenderGraphSnapshot snapshot;snapshot.nodes.push_back(source);snapshot.outputNodeId=1;snapshot.outputSocketId="imageOut";
        Check(LowerRawLayerStack(snapshot,state,1,++revision,error),error);
        return snapshot;
    };
    int w=0,h=0;
    const auto render=[&](const RenderGraphSnapshot& graph){
        pipeline.ExecuteGraph(graph);w=pipeline.GetCanvasWidth();h=pipeline.GetCanvasHeight();
        Check(!pipeline.GetLastGraphExecutionStats().allocationFailed,"Graph GPU allocation failed");
        auto values=ReadTextureRgbaFloat(pipeline.GetOutputTexture(),w,h);Check(!values.empty(),"Empty graph output");
        return values;
    };
    const auto picture=[&](const char* name,const std::vector<float>& values) {
        std::vector<std::uint8_t> bytes(values.size(),255);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<3;++c) {
            const float v=values[4*(std::size_t(h-1-y)*w+x)+c];
            bytes[4*(std::size_t(y)*w+x)+c]=std::uint8_t(std::lround(std::clamp(v,0.f,1.f)*255));
        }
        Check(WriteValidationPng(dir/name,bytes,w,h),"Could not write graph evidence");
    };
    const auto sharp=render(lower());picture("sharp.png",sharp);
    OfferBracketMasks(state,*raw);
    Check(render(lower())==sharp,"Unconnected masks changed sharp graph output");
    if(depth->masks.empty())return;
    const auto selected=std::find_if(depth->masks.begin(),depth->masks.end(),[](const auto& m){return m.name=="Far depth";});
    Check(selected!=depth->masks.end(),"No far depth band");
    const auto index=std::size_t(selected-depth->masks.begin());
    auto& layer=state.background;const auto published=layer.maskOutputs.at(index);
    auto snap=lower();Check(ConfigureRawLayerThumbnail(snap,kRawBackgroundId,published.id,error),error);
    const auto mask=render(snap);Check(mask.size()==sharp.size(),"Mask registration extent mismatch");
    auto overlay=sharp;
    for(std::size_t i=0;i<mask.size();i+=4){
        const float a=.65f*mask[i];
        for(int c=0;c<3;++c)overlay[i+c]=sharp[i+c]*(1-a)+(c==1?.85f:.08f)*a;
    }
    picture("mask-overlay.png",overlay);
    const auto resultId=FindRawRole(layer,GraphModel::NodeRole::LayerResult)->id;
    const auto prev=*layer.graph.FindInputLink(resultId,"imageIn");
    const auto blurId=layer.graph.AddLayerNode(LayerType::GaussianBlur,0,{800,240})->id;
    layer.graph.FindNode(blurId)->layerParameters["amount"]=80.f;
    layer.graph.RemoveLink(prev.fromNodeId,prev.fromSocketId,resultId,"imageIn");
    Check(layer.graph.TryConnectSockets(prev.fromNodeId,prev.fromSocketId,blurId,"imageIn",&error),error);
    Check(layer.graph.TryConnectSockets(published.nodeId,"maskOut",blurId,"maskIn",&error),error);
    Check(layer.graph.TryConnectSockets(blurId,"imageOut",resultId,"imageIn",&error),error);
    const auto blurred=render(lower());picture("masked-blur.png",blurred);
    double inside=0,outside=0;
    for(std::size_t i=0;i<sharp.size();i+=4)for(int c=0;c<3;++c){
        const double change=std::abs(blurred[i+c]-sharp[i+c]);
        if(mask[i]>.5f)inside=std::max(inside,change);
        if(mask[i]==0)outside=std::max(outside,change);
    }
    std::cout<<"Gaussian Blur maximum change inside "<<inside<<", outside "<<outside<<std::endl;
    Check(inside>.00001 && outside<.0001,"Graph blur coverage differs from mask");
    // Use the same portable project transaction and original-capture import
    // as Save As. Close the store before resolving the result or its wiring.
    const auto projectPath=dir/"Masked bracket.stack";
    MultiFrameProjectCreation create;
    create.path=projectPath;create.storageKind=ProjectStorageKind::PortableFile;
    create.projectId=GenerateStableUuid();create.projectName="Masked bracket";
    std::ifstream requestFile(file.parent_path()/"request.json");
    const auto request=nlohmann::json::parse(requestFile);
    for (const auto& capture:request.at("sources"))
        create.sources.push_back(std::filesystem::u8path(capture.at("path").get<std::string>()));
    {
        auto project=CreateMultiFrameProject(create);Check(bool(project),project.message);
        auto& snapshot=project.snapshot;
        auto& set=snapshot.sourceSets.at(0);
        Check(set.settings.at("bracketing").at("groups").size()==groups,"Saved capture group count differs");
        snapshot.pipelineData["rawLayerStack"]=SerializeRawLayerStack(state);
        Check(SaveBracketingResult(project.store,snapshot,set.sourceSetId,result,{},error),error);
    }
    auto project=OpenProjectStore(projectPath);Check(bool(project),project.message);
    Raw::Bracketing::BracketingResult restored;
    Check(RestoreBracketingResult(project.store,project.snapshot,project.snapshot.sourceSets.at(0).sourceSetId,restored,error),error);
    RawLayerStackState reopened;
    Check(DeserializeRawLayerStack(project.snapshot.pipelineData.at("rawLayerStack"),reopened,error),error);
    state=std::move(reopened);source.rawDevelopment.embeddedRawData=restored.raw;
    Check(!OfferBracketMasks(state,*raw) && render(lower())==blurred,"Saved mask wiring changed on reopen");
    state.background.graph.RemoveNode(blurId);
    Check(state.background.graph.TryConnectSockets(prev.fromNodeId,prev.fromSocketId,resultId,"imageIn",&error),error);
    Check(render(lower())==sharp,"Removing masked blur did not restore exact sharp output");
    // A deleted node remains deleted after reopen. A reprocess with no matching
    // region retains the old node and endpoint and marks it unavailable.
    auto deleted=state;
    deleted.background.graph.RemoveNode(published.nodeId);
    deleted.background.maskOutputs.erase(std::remove_if(deleted.background.maskOutputs.begin(),deleted.background.maskOutputs.end(),
        [&](const auto& o){return o.nodeId==published.nodeId;}),deleted.background.maskOutputs.end());
    RawLayerStackState deletedReopened;Check(DeserializeRawLayerStack(SerializeRawLayerStack(deleted),deletedReopened,error),error);
    Check(!OfferBracketMasks(deletedReopened,*raw) && !deletedReopened.background.graph.FindNode(published.nodeId),
        "Deleted mask was recreated");
    auto missing=state;
    Check(OfferBracketMasks(missing,Raw::RawImageData{}) &&
        missing.background.graph.FindNode(published.nodeId)->rawCoverage.value("depthUnavailable",false),"Missing mask was redirected");
    auto marker=nlohmann::json{{"schema","stack.bracket.master.v1"},{"resultId",result.identity},
        {"orientation",raw->metadata.orientation},{"contentHash",raw->contentIdentityHash},{"mosaicHash",raw->normalizedMosaicContentHash},
        {"decoderVersion",raw->decoderIdentityVersion},{"cameraRgb",raw->reconstructedCameraRgb},
        {"metadata",Raw::Serialization::SerializeMetadata(raw->metadata)},{"depthMasks",Raw::DepthMaskManifest(depth->masks)}};
    const auto master=dir/(raw->reconstructedCameraRgb?"original.exr":"original.dng");
    if(raw->reconstructedCameraRgb)RgbMaster::Write(master,raw->metadata.visibleWidth,raw->metadata.visibleHeight,raw->linearFloatBuffer,marker,depth->masks);
    else DngMaster::Write(master,*raw,marker);
    const auto copy=dir/("copied"+master.extension().string());std::filesystem::copy_file(master,copy,std::filesystem::copy_options::overwrite_existing);
    Raw::RawImageData independent;Check(Raw::RawLoader::LoadFile(copy.string(),independent),independent.metadata.error);
    Check(independent.reconstructionDepth && independent.reconstructionDepth->masks.size()==depth->masks.size(),"Standalone mask catalog lost");
    const auto headerSource=ReadBracketMasterMaskSource(copy);
    Check(bool(headerSource) && headerSource->metadata.orientation==raw->metadata.orientation,"Gallery mask directory missing");
    RawLayerStackState fresh;Check(OfferBracketMasks(fresh,*headerSource),"Standalone nodes missing");
    source.rawDevelopment.recipe.source.sourcePath=copy.u8string();
    source.rawDevelopment.embeddedRawData.reset();
    RenderGraphSnapshot lazy;lazy.nodes.push_back(source);lazy.outputNodeId=1;lazy.outputSocketId="imageOut";
    Check(LowerRawLayerStack(lazy,fresh,1,++revision,error,0,120,30,nullptr,nullptr,headerSource.get()),error);
    Check(ConfigureRawLayerThumbnail(lazy,kRawBackgroundId,fresh.background.maskOutputs.at(index).id,error),error);
    Check(render(lazy)==mask,"Copied master mask lost registration on independent Gallery open");
    for(std::size_t i=0;i<depth->masks.size();++i) {
        const auto& restored=independent.reconstructionDepth->masks[i];
        Check(restored.id==depth->masks[i].id && *restored.Read()==*depth->masks[i].Read(),"Standalone mask coverage changed");
    }
    std::cout<<"Checked "<<depth->masks.size()<<" unconnected masks, saved wiring, pixel-identical sharp output and copied master. "
             <<"Maximum delta inside "<<inside<<", outside "<<outside<<"\n";
}
}
bool ValidateBracketMaskResult(int argc,char** argv) {
    if(argc!=3 || !glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(64,64,"Bracket mask validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);bool ok=false;
    try {
        Check(LoadGLFunctions(),"OpenGL unavailable");
        const std::filesystem::path input=argv[0];
        if (input.extension()==".bracket") Verify(input,std::stoul(argv[1]),argv[2]);
        else VerifyMaster(input,argv[2]);
        ok=true;
    }
    catch(const std::exception& e){std::cerr<<"Bracket mask validation failed: "<<e.what()<<'\n';}
    glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);glfwTerminate();return ok;
}
}
