#include "Raw/Bracketing/Depth/Geometry.h"
#include "Raw/Bracketing/Depth/GpuGeometry.h"
#include "Raw/Bracketing/Depth/GuideShaders.h"
#include "Raw/Bracketing/Depth/TrackingShaders.h"
#include "Raw/Bracketing/Depth/GeometryMathShader.h"
#include "Raw/Bracketing/Depth/FusionShaders.h"
#include "Raw/Bracketing/Depth/CompletionShader.h"
#include "Raw/Bracketing/InspectionImage.h"
#include "BracketingDiagnosticInputs.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
using namespace Raw::Bracketing;
void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void SaveRgb(const std::filesystem::path& path,const Preview& p) {
    std::vector<unsigned char> bytes(std::size_t(p.width)*p.height*3);
    for(std::size_t i=0;i<bytes.size()/3;++i)for(unsigned c=0;c<3;++c) {
        double value=0;for(unsigned k=0;k<3;++k)value+=p.metadata.cameraToSrgb[c*3+k]*p.resultRgb[i*3+k]*
            p.metadata.cameraWhiteBalance[k]/std::max(1e-6f,p.metadata.cameraWhiteBalance[1]);
        value=std::max(0.,value);value=value/(1+value);
        value=value<.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055;
        bytes[i*3+c]=static_cast<unsigned char>(std::clamp(value,0.,1.)*255+.5);
    }
    Check(stbi_write_png(path.string().c_str(),p.width,p.height,3,bytes.data(),p.width*3)!=0,"Could not save depth comparison.");
}
void SaveResult(const std::filesystem::path& path,ProcessingRequest request,const BracketingResult& result) {
    request.analysis=result.analysis;
    const auto image=RenderInspectionImage(request,result,InspectionImageKind::Result,{},0,1600);
    Check(bool(image),"Could not render a matched reconstruction comparison.");
    Preview preview;preview.metadata=result.preview.metadata;
    preview.width=image->width;preview.height=image->height;preview.resultRgb=image->rgb;
    SaveRgb(path,preview);
}
void SaveDepth(const std::filesystem::path& directory,const Raw::ReconstructionDepth& depth) {
    if(!depth.Available())return;
    std::vector<unsigned char> bytes(std::size_t(depth.width)*depth.height*3);
    for(std::size_t p=0;p<depth.inverseDepth.size();++p)for(unsigned c=0;c<3;++c)
        bytes[p*3+c]=depth.confidence[p]>=.3?static_cast<unsigned char>(depth.inverseDepth[p]*255):c==1?0:90;
    Check(stbi_write_png((directory/"depth.png").string().c_str(),depth.width,depth.height,3,bytes.data(),depth.width*3)!=0,"Depth map write failed");
}
void Focused() {
    const Raw::OpenGlTaskExecutor executor=[](Raw::OpenGlTask task,std::string& e){return task(e);};
    Depth::PairGeometry pair;pair.focal=500;pair.cx=64;pair.cy=48;pair.translation={.1,0,0};
    for(double distance:{1.,2.,8.}) {
        double inverse=0,parallax=0,error=0;
        Check(Depth::Triangulate(pair,{64,48},{64+50/distance,48},inverse,parallax,error),"Known camera translation did not triangulate");
        Check(std::abs(inverse-1/distance)<1e-10,"Relative depth ordering or scale is wrong");
    }
    double inverse=0,parallax=0,residual=0;
    Check(!Depth::Triangulate(pair,{64,48},{64,48},inverse,parallax,residual),"Zero parallax produced depth");
    std::vector<cv::Point2d> points,flat,layered;
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<9;++x) {
        points.emplace_back(40+x*40,30+y*32);
        flat.emplace_back(points.back().x+3,points.back().y-1);
        layered.emplace_back(points.back().x+50./(2+3*((x+2*y)%3)),points.back().y);
    }
    std::string geometryReason;pair.cx=200;pair.cy=140;
    Check(!Depth::SolveGeometry(points,points,pair,geometryReason),"Stationary matches manufactured depth");
    Check(!Depth::SolveGeometry(points,flat,pair,geometryReason),"Planar matches manufactured depth");
    pair.focal=0;
    const bool layeredResolved=Depth::SolveGeometry(points,layered,pair,geometryReason);
    Check(layeredResolved,"Image-only depth planes were rejected: "+geometryReason);
    std::vector<cv::Point2d> refocused;
    const double angle=.03;
    const cv::Matx33d rotation(std::cos(angle),0,std::sin(angle),0,1,0,-std::sin(angle),0,std::cos(angle));
    for(unsigned i=0;i<points.size();++i) {
        const double distance=2+3*((i%9+2*(i/9))%3);
        const cv::Vec3d point((points[i].x-200)/500*distance,(points[i].y-140)/500*distance,distance);
        const auto moved=rotation*point+cv::Vec3d(.1,.02,.01);
        refocused.emplace_back(515*moved[0]/moved[2]+200,515*moved[1]/moved[2]+140);
    }
    pair.focal=0;pair.sourceFocal=0;
    const bool scaleResolved=Depth::SolveGeometry(points,refocused,pair,geometryReason);
    Check(scaleResolved,"Image-only geometry with camera scale change failed: "+geometryReason);
    {
        ProcessingRequest gpuRequest;gpuRequest.executeOpenGlTask=executor;Depth::GpuRuntime gpu(gpuRequest);
        std::string error;
        Check(gpu.Run([&](std::string& e){
            const auto evidence=std::string("#version 430 core\n")+Depth::Shaders::GeometryMath+Depth::Shaders::DepthEvidence;
            return gpu.Program(Depth::Shaders::Guide,e)&&gpu.Program(Depth::Shaders::Pyramid,e)&&
                gpu.Program(Depth::Shaders::Track,e)&&gpu.Program(evidence.c_str(),e)&&gpu.Program(Depth::Shaders::Fuse,e)&&
                gpu.Program(Depth::Shaders::Complete,e);
        },error),error);
        Depth::GpuGuide guide;guide.width=401;guide.height=281;guide.stride=1;
        Depth::GpuTracks tracks;tracks.columns=unsigned(points.size());tracks.rows=1;
        std::vector<std::array<float,8>> packed(points.size());
        for(const auto* target:{&flat,&layered,&refocused}) {
            for(unsigned i=0;i<points.size();++i)packed[i]={float(points[i].x),float(points[i].y),float((*target)[i].x),float((*target)[i].y),1,1,0,1};
            Check(gpu.Run([&](std::string&){tracks.buffer=gpu.Buffer(packed.size()*sizeof(packed[0]),packed.data());return true;},error),error);
            Depth::GpuPose pose;Check(Depth::FitGeometry(gpu,guide,tracks,pose,error),error);
            std::cout<<"GPU geometry fixture: "<<pose.accepted<<" matches "<<pose.counts[3]<<" inliers "<<pose.counts[0]<<" planar "<<pose.counts[2]<<" ambiguity "<<pose.status[2]<<'\n'<<std::flush;
            Check(pose.accepted==(target!=&flat),"GPU camera geometry disagrees with the known stationary/depth-plane fixture.");
            if(pose.accepted){
                std::vector<std::array<float,2>> evidence(points.size());
                Check(gpu.Run([&](std::string& e){
                    const auto code=std::string("#version 430 core\n")+Depth::Shaders::GeometryMath+Depth::Shaders::DepthEvidence;
                    const auto program=gpu.Program(code.c_str(),e);if(!program)return false;
                    const auto buffer=gpu.Buffer(evidence.size()*sizeof(evidence[0]));gpu.Use(program);
                    gpu.Bind(0,tracks.buffer);gpu.Bind(1,pose.buffer);gpu.Bind(2,buffer);gpu.I("count",unsigned(points.size()));gpu.I("view",0);
                    gpu.F2("size",float(guide.width),float(guide.height));gpu.Dispatch((unsigned(points.size())+63)/64);
                    gpu.Read(buffer,evidence.size()*sizeof(evidence[0]),evidence.data());return true;
                },error),error);
                Check(evidence[0][1]>.3&&evidence[2][1]>.3&&evidence[0][0]>2*evidence[2][0],"GPU triangulation lost known depth ordering.");
            }
        }
    }
    double nearInverse=0,farInverse=0;
    Check(Depth::Triangulate(pair,{points[0].x,points[0].y},{refocused[0].x,refocused[0].y},nearInverse,parallax,residual)&&
        Depth::Triangulate(pair,{points[2].x,points[2].y},{refocused[2].x,refocused[2].y},farInverse,parallax,residual)&&
        nearInverse>farInverse*2,"Camera scale change reversed or collapsed known depth ordering");
    Raw::ReconstructionDepth map;map.width=map.sensorWidth=64;map.height=map.sensorHeight=48;map.acceptedPairs=1;
    map.inverseDepth.resize(64*48);map.confidence.assign(64*48,1);
    std::vector<float> rgb(64*48*3);
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x) {
        const auto p=y*64+x;map.inverseDepth[p]=x<32?1:0;
        for(unsigned c=0;c<3;++c)rgb[p*3+c]=x<32?.2f:((x+y)%2?1.5f:-.1f);
    }
    {
        ProcessingRequest request;request.executeOpenGlTask=executor;Depth::GpuRuntime gpu(request);std::string error;
        std::vector<std::array<float,2>> measured(64*48),completed(measured.size());std::vector<float> guide(measured.size(),.2f);
        for(unsigned i=0;i<measured.size();++i)measured[i]={map.inverseDepth[i],1};
        measured[24*64+16][1]=0;measured[24*64+31][1]=0;
        Check(gpu.Run([&](std::string& e){const auto program=gpu.Program(Depth::Shaders::Complete,e);if(!program)return false;
            const auto input=gpu.Buffer(measured.size()*sizeof(measured[0]),measured.data()),output=gpu.Buffer(completed.size()*sizeof(completed[0]));
            const auto signal=gpu.Buffer(guide.size()*sizeof(float),guide.data()),stats=gpu.Buffer(40*sizeof(float));
            gpu.Use(program);gpu.Bind(0,input);gpu.Bind(1,output);gpu.Bind(2,signal);gpu.Bind(3,stats);
            gpu.I2("size",64,48);gpu.I2("guideSize",64,48);gpu.I("mode",0);gpu.Dispatch(4,3);
            gpu.Read(output,completed.size()*sizeof(completed[0]),completed.data());return true;
        },error),error);
        Check(completed[24*64+16][1]>=.3&&completed[24*64+16][0]>.99,"GPU completion lost a supported constant surface.");
        Check(completed[24*64+31][1]<.3,"GPU completion invented depth between distinct surfaces.");
        Check(completed[24*64+15]==measured[24*64+15],"GPU completion altered a measured seed.");
    }
    DepthSettings settings;settings.enabled=true;settings.focus=1;settings.blurRadius=8;
    std::vector<float> blurred;std::string error;
    Check(Depth::RenderDefocusGpu(rgb,64,48,map,settings,0,0,1,1,blurred,executor,{},error),error);
    Check(std::abs(blurred[(24*64+31)*3]-.2)<1e-6,"Background blur leaked into a focused foreground edge");
    Check(std::abs(blurred[(24*64+48)*3]-rgb[(24*64+48)*3])>.1,"Out-of-focus texture did not blur");
    std::vector<float> cpuBlur;
    Check(Depth::RenderDefocus(rgb,64,48,map,settings,0,0,1,1,cpuBlur,{},error),error);
    for(unsigned i=0;i<blurred.size();++i)Check(std::abs(blurred[i]-cpuBlur[i])<2e-5,"GPU layered defocus disagrees with the CPU reference");
    Check(Raw::SampleReconstructionDepth(map,31.5,24).confidence==0,"Depth interpolation invented an occlusion surface");
    map.confidence.assign(64*48,0);
    Check(Depth::RenderDefocusGpu(rgb,64,48,map,settings,0,0,1,1,blurred,executor,{},error)&&rgb==blurred,"Unknown depth changed the image");
    map.confidence.assign(64*48,1);settings.blurRadius=0;
    Check(Depth::RenderDefocusGpu(rgb,64,48,map,settings,0,0,1,1,blurred,executor,{},error)&&rgb==blurred,"Zero blur changed the image");
    settings.blurRadius=8;rgb.assign(rgb.size(),2.5f);
    Check(Depth::RenderDefocusGpu(rgb,64,48,map,settings,0,0,1,1,blurred,executor,{},error),error);
    for(float v:blurred)Check(std::abs(v-2.5f)<1e-5,"Defocus changed constant HDR brightness");
    Check(!Depth::RenderDefocusGpu(rgb,64,48,map,settings,0,0,1,1,blurred,executor,[]{return true;},error),"Defocus ignored cancellation");
    ProcessingRequest request;request.recipe.originFrameId="a";request.recipe.groups={{"g","Group",true,{{"a"}}}};
    request.recipe.knots=EqualCurves(1);request.recipe.depth=settings;
    auto identity=AnalysisIdentity(request);request.recipe.depth.focus=.2;request.recipe.depth.blurRadius=20;
    Check(AnalysisIdentity(request)==identity,"Focus changes invalidated geometry");
    BracketingRecipe loaded;Check(Deserialize(Serialize(request.recipe),loaded,error)&&loaded.depth.focus==.2,error);
    auto legacy=Serialize(request.recipe);legacy["version"]=6;legacy.erase("depth");
    Check(Deserialize(legacy,loaded,error)&&!loaded.depth.enabled,"Old recipes enabled depth by default");
    std::cout<<"Depth checks: image-only geometry, camera scale change, triangulation, stationary/planar rejection, focus edge, unknown fallback, HDR range, cancellation and recipe identity passed.\n"<<std::flush;
}
}
bool ValidateBracketingDepth(int argc,char** argv) {
    GLFWwindow* window=nullptr;bool initialized=false;
    try {
        if(argc<1)return true;
        const std::filesystem::path root=argv[0];std::filesystem::create_directories(root);
        const bool geometryOnly=argc>1&&std::string(argv[1])=="--geometry-only";
        const int firstSet=geometryOnly?2:1;
        initialized=glfwInit();Check(initialized,"Depth validation could not initialize OpenGL.");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        window=glfwCreateWindow(64,64,"Depth validation",nullptr,nullptr);Check(window!=nullptr,"Depth validation needs an OpenGL context.");
        glfwMakeContextCurrent(window);Check(LoadGLFunctions(),"Depth validation could not load OpenGL.");
        Focused();
        for(int set=firstSet;set<argc;++set) {
            ProcessingRequest request;std::string error;const std::filesystem::path input=argv[set];
            const auto directory=root/input.filename();std::filesystem::create_directories(directory);
            Check(LoadBracketingDiagnosticFolder(input,request,error),error);
            const auto memory=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
            request.memoryBudgetBytes=memory.budgetBytes;request.workerCount=std::min(14u,ResolveBracketingWorkerCount(memory.budgetBytes));
            request.cacheDirectory=directory/"prepared";request.recipe.depth.enabled=true;
            Check(Validate(request.recipe,error),error);
            request.executeOpenGlTask=[](Raw::OpenGlTask task,std::string& e){return task(e);};
            request.reportProgress=[&](double p,const std::string& stage){std::cout<<input.filename().string()<<" "<<int(p*100)<<"% "<<stage<<'\n'<<std::flush;};
            const auto start=std::chrono::steady_clock::now();
            if(geometryOnly) {
                // Recheck a geometry fix against the same captures without
                // repeating the much more expensive, unchanged temporal merge.
                PreparedDataset prepared;std::vector<std::string> diagnostics;
                Check(PrepareSources(request,prepared,diagnostics,error)&&Depth::Estimate(request,prepared,diagnostics,error),error);
                const auto depth=prepared.depth;Check(bool(depth),"Depth request returned no status");
                const nlohmann::json report={{"geometryOnly",true},{"sourceCount",request.sources.size()},
                    {"geometryFromImages",true},{"depthAvailable",depth->Available()},{"acceptedPairs",depth->acceptedPairs},
                    {"measuredFraction",depth->measuredFraction},{"message",depth->message},
                    {"timings",prepared.stageSeconds},{"diagnostics",diagnostics},
                    {"totalSeconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()}};
                std::ofstream(directory/"geometry-check.json")<<report.dump(2);
                SaveDepth(directory,*depth);
                std::cout<<report.dump(2)<<'\n'<<std::flush;continue;
            }
            auto result=Process(request);Check(result.status==BracketingResult::Status::Completed,result.message);
            SaveResult(directory/"sharp.png",request,result);
            const auto depth=result.raw->reconstructionDepth;Check(bool(depth),"Depth request returned no status");
            nlohmann::json report={{"sourceCount",request.sources.size()},{"geometryFromImages",true},{"depthAvailable",depth->Available()},
                {"acceptedPairs",depth->acceptedPairs},{"measuredFraction",depth->measuredFraction},{"message",depth->message},
                {"timings",result.analysis->stageSeconds},{"diagnostics",result.analysis->diagnostics}};
            if(depth->Available()) {
                SaveDepth(directory,*depth);
                for(const auto focus:{.8,.2}) {
                    auto focused=result;focused.raw=std::make_shared<Raw::RawImageData>(*result.raw);
                    request.recipe.depth.focus=focus;request.recipe.depth.blurRadius=32;
                    Check(Depth::Finish(request,focused,error),error);
                    SaveResult(directory/(focus>.5?"near-focus.png":"far-focus.png"),request,focused);
                    report["defocusApplied"]=focused.raw->reconstructedCameraRgb;
                }
            }
            report["totalSeconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::ofstream(directory/"report.json")<<report.dump(2);std::cout<<report.dump(2)<<'\n'<<std::flush;
        }
        if(window)glfwDestroyWindow(window);if(initialized)glfwTerminate();return true;
    }catch(const std::exception& e) {
        std::cerr<<"Depth validation failed: "<<e.what()<<'\n';
        if(window)glfwDestroyWindow(window);if(initialized)glfwTerminate();return false;
    }
}
}
