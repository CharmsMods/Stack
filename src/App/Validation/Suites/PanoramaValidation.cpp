#include "Persistence/BracketingProject.h"
#include "Raw/Bracketing/Panorama/Panorama.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RawGpuPipeline.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Persistence/BracketingResultStore.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RenderPipeline.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>

namespace Stack::Validation {
void ValidatePanoramaSessions(GLFWwindow*,const std::filesystem::path&,const std::filesystem::path&,nlohmann::json&);
namespace {
using namespace Raw::Bracketing;
void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
nlohmann::json GraphAdjustment() {
    return {{"type","ColorGrade"},{"strength",35},{"shadows",{1.,1.,1.}},
        {"midtones",{1.02,1.,.98}},{"highlights",{1.,1.,1.}}};
}
RawRecipe::RawDevelopmentRecipe EditedRecipe(const Raw::RawImageData& raw) {
    auto recipe=RawRecipe::MakeDefaultRecipe(raw.metadata.sourcePath,"Panorama");
    recipe.preToneExposureEv=.3f;recipe.rgbDenoise.enabled=false;
    recipe.finishTone.layerJson["localBaselineEnabled"]=false;
    recipe.finishTone.layerJson["foundationAdaptiveAssist"]=false;
    return recipe;
}
bool Decode(const std::filesystem::path& path,Raw::RawImageData& raw,const std::function<bool()>& cancel) {
    if(Raw::RawLoader::IsRawPath(path.string()))return Raw::RawLoader::LoadFile(path.string(),raw,cancel);
    int w=0,h=0,c=0;auto* rgb=stbi_load(path.string().c_str(),&w,&h,&c,3);if(!rgb)return false;
    auto identity=RawEvidence::ComputeSourceIdentity(path,cancel);auto& m=raw.metadata;
    m.rawWidth=m.visibleWidth=w;m.rawHeight=m.visibleHeight=h;m.orientation=1;
    m.mosaiced=false;m.linearChannels=3;m.linearSampleFormat=Raw::RawSampleFormat::Float32;
    m.pixelLayout=Raw::RawPixelLayout::LinearRgb;m.blackLevel=0;m.whiteLevel=1;
    m.cameraMake="Published example";m.cameraModel="sRGB photographs";
    m.sourcePath=path.string();m.sourceContentSha256=identity.sha256;m.sourceByteSize=identity.byteSize;
    raw.reconstructedCameraRgb=true;raw.linearFloatBuffer.resize(std::size_t(w)*h*3);
    for(std::size_t p=0;p<raw.linearFloatBuffer.size();++p) {
        const float v=rgb[p]/255.f;raw.linearFloatBuffer[p]=v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);
    }
    stbi_image_free(rgb);return true;
}
void Write(const std::filesystem::path& path,const Raw::RawImageData& raw) {
    const int w=raw.metadata.visibleWidth,h=raw.metadata.visibleHeight;std::vector<unsigned char> pixels(std::size_t(w)*h*4);
    for(std::size_t p=0;p<pixels.size()/4;++p){for(int c=0;c<3;++c){const float v=std::max(0.f,raw.linearFloatBuffer[p*3+c]);
        const float encoded=v<=.0031308f?12.92f*v:1.055f*std::pow(v,1/2.4f)-.055f;
        pixels[p*4+c]=static_cast<unsigned char>(255*std::clamp(encoded,0.f,1.f));}
        pixels[p*4+3]=static_cast<unsigned char>(255*(raw.outputCoverage?(*raw.outputCoverage)[p]:1));}
    Check(stbi_write_png(path.string().c_str(),w,h,4,pixels.data(),w*4)!=0,"Could not write panorama inspection PNG.");
    int rw=0,rh=0,rc=0;auto* read=stbi_load(path.string().c_str(),&rw,&rh,&rc,4);
    Check(read&&rw==w&&rh==h&&rc==4,"Panorama PNG dimensions or alpha failed.");
    for(std::size_t p=0;p<pixels.size()/4;++p)if(read[p*4+3]!=pixels[p*4+3]){stbi_image_free(read);throw std::runtime_error("PNG alpha changed.");}
    stbi_image_free(read);
}
void SaveRoundTrip(const std::filesystem::path& directory,const ProcessingRequest& request,const BracketingResult& result,nlohmann::json& report) {
    using namespace Project;
    RawProjectSnapshot snapshot;snapshot.projectId=GenerateStableUuid();snapshot.projectName="Panorama validation";
    snapshot.sourceWidth=result.raw->metadata.visibleWidth;snapshot.sourceHeight=result.raw->metadata.visibleHeight;
    const auto path=directory/"panorama.stack";
    auto opened=CreateProjectStore(path,ProjectStorageKind::DirectoryBundle,snapshot);Check(bool(opened),opened.message);
    snapshot=opened.snapshot;auto transaction=opened.store->BeginTransaction(snapshot.persistedStorageRevision);
    MultiFrameSourceSet set;set.sourceSetId=GenerateStableUuid();set.name="Panorama";set.operationIntent=MultiFrameOperationIntent::RawBurstHdr;
    set.operationSchemaVersion=kHdrOperationSchemaVersion;set.settings=MakeDefaultHdrOperationSettings();
    set.settings["algorithmId"]="stack-bracketing";set.settings["algorithmVersion"]=RecipeVersion;
    set.settings["radiometricAnchorFrameId"]=request.recipe.originFrameId;
    set.referenceFrameId=request.recipe.originFrameId;set.settings["bracketing"]=Serialize(request.recipe);
    set.settings["sharedPostHdrRecipe"]=RawRecipe::SerializeRecipe(EditedRecipe(*result.raw));
    std::string error;
    for(const auto& source:request.sources){EmbeddedAssetRecord asset;
        Raw::RawMetadata metadata;Check(Raw::RawLoader::LoadMetadata(source.path.string(),metadata),"Could not load saved RAW source metadata.");
        Check(opened.store->StageAssetFile(transaction,source.path,MultiFrameInputFamily::Raw,SerializeRawCaptureCompatibilitySummary(BuildRawCaptureCompatibilitySummary(metadata)),asset,&error),error);
        asset.originalFilename=source.displayName;asset.displayName=source.displayName;
        set.frames.push_back({source.frameId,asset.assetId,true,source.displayName});snapshot.embeddedAssets.push_back(asset);}
    snapshot.sourceSets.push_back(set);snapshot.activeSourceSetId=set.sourceSetId;snapshot.activeFrameId=set.referenceFrameId;
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::MultiFrameHdrPayload payload;payload.sourceSetId=set.sourceSetId;payload.radiometricAnchorFrameId=set.referenceFrameId;
    const auto sourceNode=graph.AddMultiFrameHdrNode(payload,{0,0})->id;
    const auto gradeNode=graph.AddLayerNode(LayerType::ColorGrade,0,{200,0})->id;
    const auto outputNode=graph.AddOutputNode({400,0},true)->id;
    Check(graph.TryConnectSockets(sourceNode,"imageOut",gradeNode,"imageIn",&error),error);
    Check(graph.TryConnectSockets(gradeNode,"imageOut",outputNode,"imageIn",&error),error);
    snapshot.pipelineData=EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array({GraphAdjustment()}),graph);
    auto committed=opened.store->Commit(transaction,snapshot);Check(bool(committed),committed.message);snapshot.persistedStorageRevision=committed.committedStorageRevision;
    Check(SaveBracketingResult(opened.store,snapshot,set.sourceSetId,result,{},error),error);
    auto reopened=OpenProjectStore(path);Check(bool(reopened),reopened.message);BracketingResult restored;
    Check(RestoreBracketingResult(reopened.store,reopened.snapshot,set.sourceSetId,restored,error),error);
    Check(restored.panorama&&restored.raw&&restored.raw->outputCoverage,"Saved panorama contract missing.");
    Check(restored.raw->linearFloatBuffer==result.raw->linearFloatBuffer&&*restored.raw->outputCoverage==*result.raw->outputCoverage&&
        *restored.panorama->ownership==*result.panorama->ownership,"Panorama pixels, ownership, or alpha changed after reopening.");
    Check(Panorama::SerializeLayout(*restored.panorama)==Panorama::SerializeLayout(*result.panorama),"Panorama layout changed after reopening.");
    Check(reopened.snapshot.sourceSets.front().settings.at("bracketing")==Serialize(request.recipe),"Panorama settings changed.");
    Check(reopened.snapshot.sourceSets.front().settings.at("sharedPostHdrRecipe")==set.settings.at("sharedPostHdrRecipe")&&
        reopened.snapshot.pipelineData==snapshot.pipelineData,"RAW or graph edits changed after reopening.");
    report["saveReopenExact"]=true;report["savedProject"]=path.string();
}
void CheckEditing(const BracketingResult& result,nlohmann::json& report,const std::filesystem::path& output) {
    {
        Raw::RawGpuPipeline development; Raw::RawDevelopSettings settings;
        const auto texture=development.Render(*result.raw,settings);
        Check(texture!=0,development.GetLastError());
        const int w=development.GetOutputWidth(),h=development.GetOutputHeight();
        std::vector<float> values(std::size_t(w)*h*4);
        glBindTexture(GL_TEXTURE_2D,texture);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,values.data());
        double maximum=0;std::size_t changed=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto p=std::size_t(y)*w+x,q=std::size_t(h-1-y)*w+x;
            const double error=std::abs(values[p*4+3]-(*result.raw->outputCoverage)[q]);maximum=std::max(maximum,error);changed+=error>.002;}
        report["rawDecodeAlpha"]={{"maximumError",maximum},{"changedPixels",changed}};
    }
    RenderPipeline pipeline;pipeline.Initialize();pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);pipeline.SetRawDevelopmentViewportValidationEnabled(false);pipeline.SetPreviewMaxDimension(0);
    RenderGraphSnapshot graph;RenderGraphNode node;node.nodeId=1;node.kind=RenderGraphNodeKind::RawDevelopment;
    node.rawDevelopment.embeddedRawData=result.raw;node.rawDevelopment.recipe=EditedRecipe(*result.raw);
    graph.nodes.push_back(node);RenderGraphNode target;target.nodeId=2;target.kind=RenderGraphNodeKind::Output;graph.nodes.push_back(target);
    RenderGraphNode grade;grade.nodeId=3;grade.kind=RenderGraphNodeKind::Layer;grade.layerJson=GraphAdjustment();graph.nodes.push_back(grade);
    graph.links.push_back({1,"imageOut",3,"imageIn"});graph.links.push_back({3,"imageOut",2,"imageIn"});graph.outputNodeId=2;graph.outputSocketId="imageOut";
    for(int pass=0;pass<3;++pass) {
        graph.nodes[0].rawDevelopment.recipe.rgbDenoise.enabled=pass==1;
        graph.nodes[0].rawDevelopment.recipe.finishTone.layerJson["localBaselineEnabled"]=pass==2;
        graph.nodes[0].rawDevelopment.recipe.finishTone.layerJson["autoCalibratePending"]=pass==2;
        pipeline.ExecuteGraph(graph);const int w=pipeline.GetCanvasWidth(),h=pipeline.GetCanvasHeight();
        Check(w==result.raw->metadata.visibleWidth&&h==result.raw->metadata.visibleHeight&&pipeline.GetOutputTexture(),"Panorama RAW handoff changed dimensions.");
        std::vector<float> values(std::size_t(w)*h*4);glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,values.data());
        double directError=0,flippedError=0;std::size_t transparent=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto p=std::size_t(y)*w+x,q=std::size_t(h-1-y)*w+x;
            directError=std::max(directError,double(std::abs(values[p*4+3]-(*result.raw->outputCoverage)[p])));
            flippedError=std::max(flippedError,double(std::abs(values[p*4+3]-(*result.raw->outputCoverage)[q])));
            transparent+=values[p*4+3]<.01f;
        }
        report["editing"][pass]={{"maximumAlphaError",flippedError},{"transparentPixels",transparent}};
        int exportWidth=0,exportHeight=0;auto exported=pipeline.GetOutputPixels(exportWidth,exportHeight);
        Check(!exported.empty(),"Panorama export returned no pixels.");
        Check(stbi_write_png((output/("edited-"+std::to_string(pass)+".png")).string().c_str(),exportWidth,exportHeight,4,exported.data(),exportWidth*4)!=0,"Could not export edited panorama.");
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto p=std::size_t(y)*w+x,q=std::size_t(h-1-y)*w+x;
            Check(std::abs(values[p*4+3]-(*result.raw->outputCoverage)[q])<.002,"RAW or graph editing lost panorama coverage.");
            for(int c=0;c<4;++c)Check(std::isfinite(values[p*4+c]),"Non-finite edited panorama pixel.");}
    }
    report["rawGraphAndDenoiseCoverage"]=true;pipeline.Shutdown();
}
}
// Real photographs only. No generated geometry, private UI access, or source mutation.
bool ValidatePanorama(int argc,char** argv) {
    if(argc<3){std::cerr<<"Usage: --validate-panorama output-directory source1 source2 ...\n";return false;}
    GLFWwindow* window=nullptr;nlohmann::json report;const std::filesystem::path output=std::filesystem::absolute(argv[0]);
    std::filesystem::create_directories(output);bool ok=false;
    try {
        Check(glfwInit()!=0,"Could not initialize GLFW.");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        window=glfwCreateWindow(32,32,"Panorama validation",nullptr,nullptr);Check(window!=nullptr,"OpenGL 4.3 unavailable.");glfwMakeContextCurrent(window);Check(LoadGLFunctions(),"OpenGL loader failed.");
        if(argc==3&&std::string(argv[1])=="--sessions") {
            ValidatePanoramaSessions(window,argv[2],output,report);report["passed"]=true;
            std::ofstream(output/"validation.json")<<report.dump(2);glfwDestroyWindow(window);glfwTerminate();return true;
        }
        if(argc==3&&std::string(argv[1])=="--reopen") {
            auto opened=Project::OpenProjectStore(argv[2]);Check(bool(opened),opened.message);BracketingResult result;std::string error;
            Check(Project::RestoreBracketingResult(opened.store,opened.snapshot,opened.snapshot.activeSourceSetId,result,error),error);
            CheckEditing(result,report,output);report["passed"]=true;
            std::ofstream(output/"validation.json")<<report.dump(2);glfwDestroyWindow(window);glfwTerminate();return true;
        }
        ProcessingRequest request;request.cacheDirectory=output/"cache";request.decode=Decode;request.memoryBudgetBytes=8ull*1024*1024*1024;
        request.recipe.reconstruction=ReconstructionMode::Panorama;Group group;group.id="captures";
        for(int i=1;i<argc;++i){const auto path=std::filesystem::absolute(argv[i]);auto identity=RawEvidence::ComputeSourceIdentity(path);Check(identity.valid,identity.reason);
            const auto id="capture-"+std::to_string(i);request.sources.push_back({id,identity.sha256,identity.byteSize,path,path.filename().string()});group.frames.push_back({id});}
        request.recipe.groups.push_back(group);request.recipe.knots=EqualCurves(1);request.recipe.originFrameId=group.frames[group.frames.size()/2].id;
        request.executeOpenGlTask=[](Raw::OpenGlTask task,std::string& error){return task(error);};
        std::ofstream progress(output/"progress.log");
        int last=-1;request.reportProgress=[&](double fraction,const std::string& label){const int p=int(fraction*100);if(p!=last){std::cout<<p<<"% "<<label<<std::endl;progress<<p<<"% "<<label<<std::endl;last=p;}};
        const auto start=std::chrono::steady_clock::now();auto result=Process(request);
        Check(result.status==BracketingResult::Status::Completed,result.message);
        report["seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        report["layout"]=Panorama::SerializeLayout(*result.panorama);Write(output/"panorama.png",*result.raw);
        std::size_t covered=0;for(float a:*result.raw->outputCoverage)covered+=a>0;report["coverageFraction"]=double(covered)/result.raw->outputCoverage->size();
        if(Raw::RawLoader::IsRawPath(request.sources.front().path.string()))SaveRoundTrip(output,request,result,report);
        else report["saveReopen"]= "Covered by the separate real RAW run";
        CheckEditing(result,report,output);
        request.analysis=result.analysis;request.recipe.panorama.overlapDenoise=false;auto off=Process(request);Check(off.status==BracketingResult::Status::Completed,off.message);Write(output/"panorama-off.png",*off.raw);
        double difference=0;std::size_t averaged=0;for(std::size_t p=0;p<result.raw->outputCoverage->size();++p){const float support=(*result.raw->multiFrameMeasurementSidecars->effectiveSupport)[p];averaged+=support>1.01f;
            for(int c=0;c<3;++c)difference=std::max(difference,double(std::abs(result.raw->linearFloatBuffer[p*3+c]-off.raw->linearFloatBuffer[p*3+c])));}
        report["autoOffMaximumDifference"]=difference;report["averagedPixels"]=averaged;
        request.maximumOutputDimension=8;auto limited=Process(request);Check(limited.status==BracketingResult::Status::Failed&&limited.panorama&&limited.analysis&&!limited.raw,"Output limit did not retain the fitted layout.");
        report["oversizeMessage"]=limited.message;request.maximumOutputDimension=0;
        request.shouldCancel=[](){return true;};auto canceled=Process(request);Check(canceled.status==BracketingResult::Status::Canceled&&!canceled.raw,"Cancellation published an incomplete panorama.");
        report["cancellation"]=true;report["passed"]=true;ok=true;
    }catch(const std::exception& e){report["error"]=e.what();std::cerr<<e.what()<<std::endl;}
    std::ofstream(output/"validation.json")<<report.dump(2);if(window)glfwDestroyWindow(window);glfwTerminate();return ok;
}
}
