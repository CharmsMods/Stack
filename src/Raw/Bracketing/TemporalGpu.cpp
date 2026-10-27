#include "TemporalGpu.h"
#include "TemporalGpuShaders.h"
#include "CaptureCorrespondence.h"
#include "GuideCovariance.h"
#include "PreparedRegion.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <limits>
#include <utility>

#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_MAX_SHADER_STORAGE_BLOCK_SIZE
#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE 0x90DE
#endif
namespace Raw::Bracketing {
namespace {
struct Tap {float value,gain,flags,padding=0;};
struct Mapping {double x,y,xx,xy,yy,confidence;double guideXX=-1,guideXY=0,guideYY=0;};
struct GpuObservation {double value,variance,headroom,support,fallback,exposure,measurement,uncertainty;std::uint32_t flags,padding;};
struct Parameters {std::array<double,24> values{};std::uint32_t failed=0,padding=0;};
static_assert(sizeof(Tap)==16&&sizeof(Mapping)==72&&sizeof(GpuObservation)==72&&sizeof(Parameters)==200);
constexpr std::size_t accumulatorBytes=176;
struct Bindings {
    GLint program=0,buffer=0;std::array<GLint,6> indexed{};
    Bindings(){glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&buffer);
        for(unsigned i=0;i<indexed.size();++i)glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,i,&indexed[i]);}
    ~Bindings(){for(unsigned i=0;i<indexed.size();++i)glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,indexed[i]);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);glUseProgram(program);}
};
bool Check(std::string& error){auto code=glGetError();if(code==GL_NO_ERROR)return true;
    error="Capture combining GPU operation failed (OpenGL "+std::to_string(code)+").";return false;}
bool Canceled(const ProcessingRequest& r){return r.shouldCancel&&r.shouldCancel();}
Parameters SourceParameters(const PreparedSource& s){
    Parameters p;
    for(unsigned c=0;c<4;++c){const auto& n=s.noise.sites[c];
        p.values[c*4]=n.shotScale;p.values[c*4+1]=n.offsetVariance;
        p.values[c*4+2]=n.residualModelTau0;p.values[c*4+3]=n.residualModelTau1;
        p.values[20+c]=s.frame.calibration.usableSpanByCfaSite[c];}
    p.values[16]=s.scale;p.values[17]=s.scaleVariance;p.values[18]=s.detailPreference;return p;
}
bool ValidSource(const PreparedSource& s){
    if(s.noise.quality==Mfd::NoiseModelQuality::Unavailable||s.noise.preparedFrameCacheKey!=s.frame.cacheKey||
        !std::isfinite(s.scale)||s.scale<=0||!std::isfinite(s.scaleVariance)||s.scaleVariance<0)return false;
    for(unsigned c=0;c<4;++c)if(!Mfd::ValidateSiteNoiseProfile(s.noise.sites[c])||!s.noise.sites[c].quantizationIncluded||
        !std::isfinite(s.frame.calibration.usableSpanByCfaSite[c])||s.frame.calibration.usableSpanByCfaSite[c]<=0)return false;
    return true;
}
// Copy rows directly from immutable prepared tiles. Source samples are neither
// filtered nor converted; the only padding is outside the physical sensor.
bool Region(const PreparedSource& source,const PreparedDataset& data,int left,int top,int width,int height,
    const ProcessingRequest& request,std::vector<Tap>& out,std::string& error,
    const Mfd::PreparedRawTile* resident=nullptr){
    out.assign(std::size_t(width)*height,{0,1,2,0});
    const auto side=source.frame.tileRawPixels;
    if(!side){error="A prepared RAW source has no tile extent.";return false;}
    const int x0=std::max(0,left),y0=std::max(0,top),x1=std::min(int(data.width),left+width),y1=std::min(int(data.height),top+height);
    if(x0>=x1||y0>=y1)return true;
    // Whole-region borrowing leaves the ordinary source-tile copy kernel
    // unchanged. Partial guide halos still require their backing tiles.
    if(resident&&resident->originX<=unsigned(x0)&&resident->originY<=unsigned(y0)&&
        unsigned(x1)-resident->originX<=resident->extent.width&&unsigned(y1)-resident->originY<=resident->extent.height) {
        if(Canceled(request)){error="Canceled";return false;}
        const auto& t=*resident;
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
            const auto q=std::size_t(y-t.originY)*t.extent.width+x-t.originX;
            out[std::size_t(y-top)*width+x-left]={t.normalizedMosaic[q],t.comparisonGain[q],float(t.sampleFlags[q]),0};
        }
        return true;
    }
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    for(unsigned ty=unsigned(y0)/side;ty<=unsigned(y1-1)/side;++ty)for(unsigned tx=unsigned(x0)/side;tx<=unsigned(x1-1)/side;++tx){
        if(Canceled(request)){error="Canceled";return false;}
        Mfd::PreparedRawTile t;if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,t,&error)!=Mfd::TileCacheReadStatus::Hit)return false;
        for(int y=std::max(y0,int(t.originY));y<std::min(y1,int(t.originY+t.extent.height));++y)
            for(int x=std::max(x0,int(t.originX));x<std::min(x1,int(t.originX+t.extent.width));++x){
                const auto q=std::size_t(y-t.originY)*t.extent.width+x-t.originX;
                out[std::size_t(y-top)*width+x-left]={t.normalizedMosaic[q],t.comparisonGain[q],float(t.sampleFlags[q]),0};
            }
    }
    return true;
}
bool ValidOutput(const GpuObservation& p){
    // Unsupported/clipped fallbacks retain a negative measurement sentinel.
    // FALLBACK_REJECTED (32) is valid transient provenance, stripped by Unpack.
    // Finite inputs alone do not guarantee finite GPU arithmetic or readback.
    return std::isfinite(p.value)&&std::isfinite(p.variance)&&p.variance>=0&&
        std::isfinite(p.support)&&p.support>=0&&std::isfinite(p.measurement)&&
        (p.support<=0||p.measurement>=0)&&std::isfinite(p.uncertainty)&&p.uncertainty>=0&&
        std::isfinite(p.headroom)&&p.headroom>=0&&p.headroom<=1&&
        std::isfinite(p.fallback)&&std::isfinite(p.exposure)&&p.exposure>=0&&(p.flags&~63u)==0;
}
Observation Unpack(const GpuObservation& p){
    Observation o;o.value=p.value;o.variance=p.variance;o.headroom=p.headroom;o.support=p.support;
    o.fallback=p.fallback;o.exposure=p.exposure;o.measurementVariance=p.measurement;o.uncertaintyVariance=p.uncertainty;
    o.finite=(p.flags&1)!=0;o.clipped=(p.flags&2)!=0;o.localRejected=(p.flags&4)!=0;
    o.fixedReference=(p.flags&8)!=0;o.fallbackClipped=(p.flags&16)!=0;return o;
}
}
class TemporalGpu::Impl {
public:
    explicit Impl(const ProcessingRequest& request):execute(request.executeOpenGlTask){
        budget=std::min<std::uint64_t>(512ull*1024*1024,request.memoryBudgetBytes/4);
        if(!request.preferGpuCombining){reason="GPU capture combining is required but disabled by the processing request.";return;}
        if(!execute){reason="GPU capture combining is required, but the compute context is unavailable.";return;}
        std::string error;
        try {available=execute([&](std::string& e){
            if(!glDispatchCompute||!glGetBufferSubData||!glBindBufferBase||!glMemoryBarrier){e="OpenGL compute is unavailable.";return false;}
            Bindings restore;GLint limit=0;glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&limit);blockLimit=std::max(0,limit);
            const char* kernels[]={TemporalShaders::Prepare,TemporalShaders::Sample.c_str(),TemporalShaders::Accumulate.c_str(),
                TemporalShaders::Finish,TemporalShaders::SampleAccumulate.c_str()};
            const char* uniforms[]={"target","region","sensor","sites","aligned","reference","useBands","localGuide","groupIndex","groupCount","initializeGuide"};
            for(unsigned i=0;i<programs.size();++i){const std::string shader=std::string(TemporalShaders::Common)+kernels[i];
                programs[i]=GLHelpers::CreateComputeProgram(shader.c_str());if(!programs[i]){e="Capture combining shader could not compile.";return false;}
                for(unsigned j=0;j<uniformLocations[i].size();++j)uniformLocations[i][j]=glGetUniformLocation(programs[i],uniforms[j]);}
            return Check(e);
        },error);}catch(const std::exception& e){error=e.what();}
        catch(...){error="GPU capture combining initialization failed with an unknown error.";}
        if(!available)reason=error.empty()?"GPU capture combining is unavailable.":error;
    }
    struct Workspace {
        std::array<GLuint,6> buffers{};
        std::array<std::size_t,6> sizes{};
        std::uint64_t reserved=0;
    };
    bool Retire(Workspace work,std::string& error){
        if(!work.reserved)return true;
        bool ok=true;
        if(std::any_of(work.buffers.begin(),work.buffers.end(),[](GLuint b){return b!=0;})){
            try{ok=execute([&](std::string& e){Bindings restore;
                glDeleteBuffers(GLsizei(work.buffers.size()),work.buffers.data());return Check(e);},error);}
            catch(const std::exception& e){ok=false;error=e.what();}
            catch(...){ok=false;error="Unknown GPU workspace cleanup error.";}
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(ok)resident-=work.reserved;
            else {
                // Unknown deletion outcome must not create fictitious capacity.
                if(poolError.empty())poolError="GPU workspace cleanup failed: "+
                    (error.empty()?std::string("context executor refused cleanup."):error);
                error=poolError;
            }
        }
        wake.notify_all();return ok;
    }
    bool Release(Workspace& work,bool reusable,std::string& error){
        Workspace retiring=std::exchange(work,Workspace{});
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(reusable&&operations>0&&poolError.empty()&&!idle.reserved){
                idle=std::move(retiring);wake.notify_all();return true;
            }
        }
        return Retire(std::move(retiring),error);
    }
    bool Acquire(Workspace& work,const std::array<std::size_t,6>& sizes,
        const ProcessingRequest& request,std::uint64_t limit,std::string& error){
        std::uint64_t required=0;for(auto bytes:sizes)required+=bytes;
        for(;;){
            if(Canceled(request)){error="Canceled";return false;}
            Workspace retiring;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if(!poolError.empty()){error=poolError;return false;}
                if(operations>0&&idle.reserved&&idle.sizes==sizes&&resident<=limit){
                    work=std::exchange(idle,Workspace{});++reuses;return true;
                }
                // A different-sized idle lease must never pin admission capacity.
                if(idle.reserved)retiring=std::exchange(idle,Workspace{});
                else if(resident+required<=limit){
                    work.sizes=sizes;work.reserved=required;
                    resident+=required;peak=std::max(peak,resident);return true;
                }else {wake.wait_for(lock,std::chrono::milliseconds(20));continue;}
            }
            if(!Retire(std::move(retiring),error))return false;
        }
    }
    void BeginOperation(){std::lock_guard<std::mutex> lock(mutex);++operations;}
    bool EndOperation(std::string& error){
        Workspace retiring;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(--operations==0)retiring=std::exchange(idle,Workspace{});
        }
        const bool retired=Retire(std::move(retiring),error);
        std::lock_guard<std::mutex> lock(mutex);
        if(!poolError.empty()){error=poolError;return false;}
        return retired;
    }
    ~Impl(){
        std::string ignored;
        try{Retire(std::exchange(idle,Workspace{}),ignored);}catch(...){}
        if(!execute)return;
        try{execute([&](std::string&){for(auto p:programs)if(p)glDeleteProgram(p);return true;},ignored);}catch(...){}
    }
    struct Tile : Workspace {
        Impl& owner;const ProcessingRequest& request;
        Tile(Impl& o,const ProcessingRequest& r):owner(o),request(r){}
        ~Tile(){std::string ignored;try{owner.Release(*this,false,ignored);}catch(...) {}}
        bool Finish(std::string& error){return owner.Release(*this,true,error);}
        bool Run(const Raw::OpenGlTask& task,std::string& error){
            if(Canceled(request)){error="Canceled";return false;}
            const bool ok=owner.execute([&](std::string& e){Bindings restore;return task(e)&&Check(e);},error);
            if(!ok&&error.empty())error="GPU capture combining did not complete.";
            return ok;
        }
        void Allocate(unsigned slot,std::size_t bytes){glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[slot]);
            glBufferData(GL_SHADER_STORAGE_BUFFER,GLsizeiptr(bytes),nullptr,GL_DYNAMIC_DRAW);}
        void Upload(unsigned slot,std::size_t bytes,const void* data){glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[slot]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,GLsizeiptr(bytes),data);}
        void Bind(){for(unsigned i=0;i<buffers.size();++i)glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,buffers[i]);}
        void Dispatch(unsigned programIndex,int left,int top,int width,int height,const PreparedDataset& data,
            int sx,int sy,int sw,int sh,const PreparedSource& source,bool aligned,bool bands,bool local,bool initializeGuide=true){
            const auto program=owner.programs[programIndex];const auto& locations=owner.uniformLocations[programIndex];
            Bind();glUseProgram(program);
            glUniform4i(locations[0],left,top,width,height);
            glUniform4i(locations[1],sx,sy,sw,sh);
            glUniform2i(locations[2],data.width,data.height);
            Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
            glUniform4i(locations[3],int(layout.SiteAt(0,0)),int(layout.SiteAt(1,0)),int(layout.SiteAt(0,1)),int(layout.SiteAt(1,1)));
            glUniform1i(locations[4],aligned);glUniform1i(locations[5],&source==&data.sources[data.origin]);
            glUniform1i(locations[6],bands);glUniform1i(locations[7],local);
            glUniform1i(locations[8],0);glUniform1i(locations[9],1);glUniform1i(locations[10],initializeGuide);
            glDispatchCompute((width*height+63)/64,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
        }
        bool Status(std::string& e){std::uint32_t failure=0;glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[5]);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,192,4,&failure);
            if(failure){e="A GPU capture sample exceeded its uploaded region.";return false;}return true;}
    };
    bool Combine(const ProcessingRequest& request,const PreparedDataset& data,const Mfd::PreparedRawTile& origin,
        bool detailFusion,std::vector<std::vector<Observation>>& groups,std::string& error){
        const auto fail=[&](const std::string& why){
            error=why.empty()?"GPU capture combining failed.":why;return false;};
        if(Canceled(request))return fail("Canceled");
        if(!available)return fail(reason);
        if(!request.preferGpuCombining)return fail("GPU capture combining is required but disabled by the processing request.");
        for(const auto& s:data.sources)if(s.enabled&&!ValidSource(s))
            return fail("GPU capture combining cannot use a source with invalid or unsupported noise, exposure, or calibration data.");
        if(!origin.extent.width||!origin.extent.height||origin.originX>data.width||origin.originY>data.height||
            origin.extent.width>data.width-origin.originX||origin.extent.height>data.height-origin.originY||
            data.width>unsigned(std::numeric_limits<int>::max()-16)||data.height>unsigned(std::numeric_limits<int>::max()-16))
            return fail("GPU capture combining received an invalid reference region.");
        const int left=int(origin.originX),top=int(origin.originY),width=int(origin.extent.width),height=int(origin.extent.height);
        const auto originCount=std::size_t(width)*height;
        if(origin.normalizedMosaic.size()!=originCount||origin.comparisonGain.size()!=originCount||origin.sampleFlags.size()!=originCount)
            return fail("GPU capture combining received incomplete reference samples.");
        const auto count=std::size_t(width)*height,maxSource=std::size_t(width+16)*(height+16)*4;
        const auto required=count*(sizeof(Mapping)+32+sizeof(GpuObservation)+accumulatorBytes)+maxSource*sizeof(Tap)+sizeof(Parameters);
        const auto admissionLimit=std::min(budget,request.memoryBudgetBytes/4);
        if(required>admissionLimit||count*accumulatorBytes>blockLimit||maxSource*sizeof(Tap)>blockLimit)
            return fail("GPU capture combining tile exceeds the available workspace or device buffer limit.");
        Tile tile(*this,request);
        const std::array<std::size_t,6> sizes={maxSource*sizeof(Tap),count*sizeof(Mapping),
            count*32,count*sizeof(GpuObservation),count*accumulatorBytes,sizeof(Parameters)};
        if(!Acquire(tile,sizes,request,admissionLimit,error))return false;
        std::string gpuError;
        if(!tile.buffers[0]){
            if(!tile.Run([&](std::string&){glGenBuffers(GLsizei(tile.buffers.size()),tile.buffers.data());
                for(unsigned slot=0;slot<sizes.size();++slot)tile.Allocate(slot,sizes[slot]);
                return true;},gpuError)){
                if(Canceled(request)){error="Canceled";return false;}return fail(gpuError);}
            std::lock_guard<std::mutex> lock(mutex);++allocations;
        }
        const auto& reference=data.sources[data.origin];std::vector<Tap> referenceTaps,taps;
        // Non-local Prepare samples only target-bounded pilot taps. Only
        // its local scene-gradient stencil needs the four-pixel halo.
        const bool localGuide=request.recipe.alignmentMode==AlignmentMode::AutomaticLocal;
        const int guideHalo=localGuide?4:0;
        // Keep ordinary local alignment's original staging/read order. Reuse
        // benefits non-local guides and expanded detail regions without
        // changing that hot path's allocation behavior.
        const bool reuseReference=!localGuide||detailFusion;
        if(!Region(reference,data,left-guideHalo,top-guideHalo,width+2*guideHalo,height+2*guideHalo,
            request,referenceTaps,error,reuseReference?&origin:nullptr))return false;
        std::vector<Mapping> mapping(localGuide?count:0);std::vector<GpuObservation> output(count);
        // Publish only after every group and final cancellation check succeed.
        std::vector<std::vector<Observation>> pendingGroups(request.recipe.groups.size());
        const auto referenceParameters=SourceParameters(reference);
        for(std::size_t g=0;g<pendingGroups.size();++g){
            // The reference pilot and scene gradient are identical for every
            // exposure group. Later prepares only clear the group's sums.
            if(!tile.Run([&](std::string& e){if(g==0)tile.Upload(0,referenceTaps.size()*sizeof(Tap),referenceTaps.data());
                tile.Upload(5,sizeof(Parameters),&referenceParameters);
                tile.Dispatch(0,left,top,width,height,data,left-guideHalo,top-guideHalo,width+2*guideHalo,height+2*guideHalo,
                    reference,false,false,localGuide,g==0);
                // Later groups only clear sums; Prepare cannot sample or set
                // failed when initializeGuide is false. Avoid a blocking flag
                // readback here. The final Status still checks every Sample.
                return g>0||tile.Status(e);},gpuError))break;
            // The guide now resides in its SSBO and later groups reuse it.
            // Release the CPU-only staging copy before loading source regions.
            if(g==0)std::vector<Tap>().swap(referenceTaps);
            const bool bands=detailFusion&&std::any_of(data.sources.begin(),data.sources.end(),
                [&](const auto& s){return s.enabled&&s.group==g&&s.detailPreference<.999;});
            for(const auto& source:data.sources){
                if(!source.enabled||source.group!=g)continue;
                if(Canceled(request)){error="Canceled";return false;}
                const bool aligned=source.alignmentDiagnostic.model!=CaptureAlignment::Model::Identity||
                    (&source!=&reference&&source.alignmentDiagnostic.localApplied);
                int sx=left,sy=top,sw=width,sh=height;
                if(aligned){
                    if(mapping.empty())mapping.resize(count);
                    double minX=double(data.width),minY=double(data.height),maxX=0,maxY=0;
                    for(int y=0;y<height;++y){if(Canceled(request)){error="Canceled";return false;}
                        for(int x=0;x<width;++x){
                            Mfd::RawMappingJacobian jacobian;
                            const bool guideEnabled=request.recipe.alignmentMode==AlignmentMode::AutomaticLocal;
                            const auto c=EvaluateCaptureCorrespondence(source,{double(left+x),double(top+y)},false,nullptr,
                                guideEnabled?&jacobian:nullptr);
                            mapping[std::size_t(y)*width+x]={c.sourceRaw.x,c.sourceRaw.y,c.covarianceRaw.xxRawPixelsSquared,
                                c.covarianceRaw.xyRawPixelsSquared,c.covarianceRaw.yyRawPixelsSquared,c.confidence};
                            Mfd::SymmetricRawCovariance guideCovariance;
                            if(guideEnabled&&TryGuideCovariance(c.covarianceRaw,jacobian,guideCovariance)) {
                                auto& m=mapping[std::size_t(y)*width+x];
                                m.guideXX=guideCovariance.xxRawPixelsSquared;
                                m.guideXY=guideCovariance.xyRawPixelsSquared;
                                m.guideYY=guideCovariance.yyRawPixelsSquared;
                            }
                            if(std::isfinite(c.sourceRaw.x)&&std::isfinite(c.sourceRaw.y)&&
                                c.sourceRaw.x>=-4&&c.sourceRaw.y>=-4&&c.sourceRaw.x<data.width+4.&&c.sourceRaw.y<data.height+4.){
                                minX=std::min(minX,c.sourceRaw.x);minY=std::min(minY,c.sourceRaw.y);
                                maxX=std::max(maxX,c.sourceRaw.x);maxY=std::max(maxY,c.sourceRaw.y);}
                        }}
                    sx=std::max(0,int(std::floor(minX))-6);sy=std::max(0,int(std::floor(minY))-6);
                    const auto right=std::min(int(data.width),int(std::ceil(maxX))+7),bottom=std::min(int(data.height),int(std::ceil(maxY))+7);
                    sw=std::max(1,right-sx);sh=std::max(1,bottom-sy);
                }
                if(std::size_t(sw)*sh>maxSource)return fail("GPU capture combining warp exceeds the source-region workspace allowance.");
                if(!Region(source,data,sx,sy,sw,sh,request,taps,error,reuseReference&&(&source==&reference)?&origin:nullptr))return false;
                const auto parameters=SourceParameters(source);
                if(!tile.Run([&](std::string&){tile.Upload(0,taps.size()*sizeof(Tap),taps.data());
                    if(aligned)tile.Upload(1,count*sizeof(Mapping),mapping.data());tile.Upload(5,192,&parameters);
                    if(bands){
                        tile.Dispatch(1,left,top,width,height,data,sx,sy,sw,sh,source,aligned,true,false);
                        tile.Dispatch(2,left,top,width,height,data,sx,sy,sw,sh,source,aligned,true,false);
                    }else tile.Dispatch(4,left,top,width,height,data,sx,sy,sw,sh,source,aligned,false,false);
                    return true;},gpuError))break;
            }
            if(!gpuError.empty())break;
            if(!tile.Run([&](std::string& e){tile.Dispatch(3,left,top,width,height,data,0,0,1,1,reference,false,bands,false);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER,tile.buffers[3]);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,count*sizeof(GpuObservation),output.data());return tile.Status(e);},gpuError))break;
            auto& group=pendingGroups[g];group.resize(count);
            for(std::size_t i=0;i<count;++i){
                if(!ValidOutput(output[i]))return fail("GPU capture combining produced an invalid or non-finite observation in group "+
                    std::to_string(g+1)+" at ("+std::to_string(left+i%width)+", "+std::to_string(top+i/width)+").");
                group[i]=Unpack(output[i]);
            }
        }
        if(Canceled(request)){error="Canceled";return false;}
        if(!gpuError.empty())return fail(gpuError);
        if(!tile.Finish(error))return false;
        if(Canceled(request)){error="Canceled";return false;}
        {std::lock_guard<std::mutex> lock(mutex);++gpuTiles;}
        groups.swap(pendingGroups);
        return true;
    }
    void Report(BracketingAnalysis& analysis)const{
        std::lock_guard<std::mutex> lock(mutex);
        analysis.diagnostics.push_back("Capture combining: "+std::to_string(gpuTiles)+" GPU tiles; GPU-only double-precision resampling, uncertainty and temporal/detail fusion; GPU peak reservation "+std::to_string(peak/(1024*1024))+" MiB; current reservation "+std::to_string(resident/(1024*1024))+
            " MiB; workspace allocations "+std::to_string(allocations)+", reuses "+std::to_string(reuses)+"."+
            (reason.empty()?"":" "+reason));
    }
    Raw::OpenGlTaskExecutor execute;std::array<GLuint,5> programs{};
    std::array<std::array<GLint,11>,5> uniformLocations{};bool available=false;
    std::uint64_t budget=0,blockLimit=0,resident=0,peak=0,gpuTiles=0;
    Workspace idle;std::size_t operations=0;
    std::uint64_t allocations=0,reuses=0;
    mutable std::mutex mutex;std::condition_variable wake;std::string reason,poolError;
};
TemporalGpu::TemporalGpu(const ProcessingRequest& r):impl_(std::make_unique<Impl>(r)){}
TemporalGpu::~TemporalGpu()=default;
TemporalGpu::Operation::Operation(TemporalGpu& owner):owner_(&owner){owner_->impl_->BeginOperation();}
TemporalGpu::Operation::~Operation(){std::string ignored;try{Finish(ignored);}catch(...) {}}
bool TemporalGpu::Operation::Finish(std::string& error){
    if(!owner_)return true;
    auto* owner=std::exchange(owner_,nullptr);return owner->impl_->EndOperation(error);
}
TemporalGpu::WorkspaceStats TemporalGpu::Workspaces()const{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return {impl_->resident,impl_->peak,impl_->allocations,impl_->reuses,impl_->operations,impl_->idle.reserved!=0};
}
bool TemporalGpu::Available()const{std::string error;return Ready(error);}
bool TemporalGpu::Ready(std::string& error)const{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!impl_->poolError.empty()){error=impl_->poolError;return false;}
    if(impl_->available)return true;
    error=impl_->reason.empty()?"GPU capture combining is unavailable.":impl_->reason;
    return false;
}
bool TemporalGpu::Combine(const ProcessingRequest& r,const PreparedDataset& d,const Mfd::PreparedRawTile& t,
    bool detail,std::vector<std::vector<Observation>>& groups,std::string& e){
    try{
        if(impl_->Combine(r,d,t,detail,groups,e))return true;
        if(e.empty())e="GPU capture combining failed while reading prepared source data.";
    }
    catch(const std::exception& failure){e="GPU capture combining failed: "+std::string(failure.what());}
    catch(...){e="GPU capture combining failed with an unknown error.";}
    return false;
}
void TemporalGpu::Report(BracketingAnalysis& a)const{impl_->Report(a);}
}
