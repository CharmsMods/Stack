#include "TemporalGpu.h"
#include "TemporalGpuShaders.h"
#include "CaptureCorrespondence.h"
#include "PreparedRegion.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <limits>

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
struct Mapping {double x,y,xx,xy,yy,confidence;};
struct GpuObservation {double value,variance,headroom,support,fallback,exposure,measurement,uncertainty;std::uint32_t flags,padding;};
struct Parameters {std::array<double,24> values{};std::uint32_t failed=0,padding=0;};
static_assert(sizeof(Tap)==16&&sizeof(Mapping)==48&&sizeof(GpuObservation)==72&&sizeof(Parameters)==200);
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
    const ProcessingRequest& request,std::vector<Tap>& out,std::string& error){
    out.assign(std::size_t(width)*height,{0,1,2,0});
    const auto side=source.frame.tileRawPixels;
    const int x0=std::max(0,left),y0=std::max(0,top),x1=std::min(int(data.width),left+width),y1=std::min(int(data.height),top+height);
    if(x0>=x1||y0>=y1)return true;
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
        if(!request.preferGpuCombining||!execute){reason="CPU capture combining selected or compute context unavailable.";return;}
        std::string error;
        try {available=execute([&](std::string& e){
            if(!glDispatchCompute||!glGetBufferSubData||!glBindBufferBase||!glMemoryBarrier){e="OpenGL compute is unavailable.";return false;}
            Bindings restore;GLint limit=0;glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&limit);blockLimit=std::max(0,limit);
            const char* kernels[]={TemporalShaders::Prepare,TemporalShaders::Sample,TemporalShaders::Accumulate,TemporalShaders::Finish};
            for(unsigned i=0;i<programs.size();++i){const std::string shader=std::string(TemporalShaders::Common)+kernels[i];
                programs[i]=GLHelpers::CreateComputeProgram(shader.c_str());if(!programs[i]){e="Capture combining shader could not compile.";return false;}}
            return Check(e);
        },error);}catch(const std::exception& e){error=e.what();}
        if(!available)reason=error.empty()?"GPU capture combining is unavailable.":error;
    }
    ~Impl(){if(!execute)return;std::string ignored;try{execute([&](std::string&){for(auto p:programs)if(p)glDeleteProgram(p);return true;},ignored);}catch(...) {}}
    struct Tile {
        Impl& owner;const ProcessingRequest& request;std::array<GLuint,6> buffers{};std::uint64_t reserved=0;
        Tile(Impl& o,const ProcessingRequest& r):owner(o),request(r){}
        ~Tile(){if(buffers[0]){std::string ignored;try{owner.execute([&](std::string&){Bindings restore;
                glDeleteBuffers(GLsizei(buffers.size()),buffers.data());return true;},ignored);}catch(...) {}}
            if(reserved){std::lock_guard<std::mutex> lock(owner.mutex);owner.resident-=reserved;owner.wake.notify_all();}}
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
        void Dispatch(GLuint program,int left,int top,int width,int height,const PreparedDataset& data,
            int sx,int sy,int sw,int sh,const PreparedSource& source,bool aligned,bool bands,bool local){
            Bind();glUseProgram(program);
            glUniform4i(glGetUniformLocation(program,"target"),left,top,width,height);
            glUniform4i(glGetUniformLocation(program,"region"),sx,sy,sw,sh);
            glUniform2i(glGetUniformLocation(program,"sensor"),data.width,data.height);
            Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
            glUniform4i(glGetUniformLocation(program,"sites"),int(layout.SiteAt(0,0)),int(layout.SiteAt(1,0)),int(layout.SiteAt(0,1)),int(layout.SiteAt(1,1)));
            glUniform1i(glGetUniformLocation(program,"aligned"),aligned);glUniform1i(glGetUniformLocation(program,"reference"),&source==&data.sources[data.origin]);
            glUniform1i(glGetUniformLocation(program,"useBands"),bands);glUniform1i(glGetUniformLocation(program,"localGuide"),local);
            glUniform1i(glGetUniformLocation(program,"groupIndex"),0);glUniform1i(glGetUniformLocation(program,"groupCount"),1);
            glDispatchCompute((width*height+63)/64,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
        }
        bool Status(std::string& e){std::uint32_t failure=0;glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[5]);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,192,4,&failure);
            if(failure){e="A GPU capture sample exceeded its uploaded region.";return false;}return true;}
    };
    bool Combine(const ProcessingRequest& request,const PreparedDataset& data,const Mfd::PreparedRawTile& origin,
        bool detailFusion,std::vector<std::vector<Observation>>& groups,std::string& error){
        const auto fallback=[&](const std::string& why){std::lock_guard<std::mutex> lock(mutex);++cpuTiles;
            if(reason.empty())reason=why;return false;};
        if(!available||!request.preferGpuCombining)return fallback(reason);
        for(const auto& s:data.sources)if(s.enabled&&!ValidSource(s))return fallback("A source needs the scalar contract validator.");
        const int left=int(origin.originX),top=int(origin.originY),width=int(origin.extent.width),height=int(origin.extent.height);
        const auto count=std::size_t(width)*height,maxSource=std::size_t(width+16)*(height+16)*4;
        const auto required=count*(sizeof(Mapping)+32+sizeof(GpuObservation)+accumulatorBytes)+maxSource*sizeof(Tap)+sizeof(Parameters);
        if(required>budget||count*accumulatorBytes>blockLimit||maxSource*sizeof(Tap)>blockLimit)
            return fallback("Tile exceeds the bounded GPU workspace; full CPU calculation retained.");
        Tile tile(*this,request);
        {std::unique_lock<std::mutex> lock(mutex);while(resident+required>budget){
                if(Canceled(request)){error="Canceled";return false;}wake.wait_for(lock,std::chrono::milliseconds(20));}
            resident+=required;tile.reserved=required;peak=std::max(peak,resident);}
        std::string gpuError;
        if(!tile.Run([&](std::string&){glGenBuffers(GLsizei(tile.buffers.size()),tile.buffers.data());
            tile.Allocate(0,maxSource*sizeof(Tap));tile.Allocate(1,count*sizeof(Mapping));
            tile.Allocate(2,count*32);tile.Allocate(3,count*sizeof(GpuObservation));
            tile.Allocate(4,count*accumulatorBytes);tile.Allocate(5,sizeof(Parameters));return true;},gpuError)){
            if(Canceled(request)){error="Canceled";return false;}return fallback(gpuError);}
        const auto& reference=data.sources[data.origin];std::vector<Tap> referenceTaps,taps;
        if(!Region(reference,data,left-4,top-4,width+8,height+8,request,referenceTaps,error))return false;
        std::vector<Mapping> mapping(count);std::vector<GpuObservation> output(count);
        groups.assign(request.recipe.groups.size(),{});
        for(std::size_t g=0;g<groups.size();++g){
            const auto referenceParameters=SourceParameters(reference);
            if(!tile.Run([&](std::string& e){tile.Upload(0,referenceTaps.size()*sizeof(Tap),referenceTaps.data());
                tile.Upload(5,sizeof(Parameters),&referenceParameters);
                tile.Dispatch(programs[0],left,top,width,height,data,left-4,top-4,width+8,height+8,reference,false,false,
                    request.recipe.alignmentMode==AlignmentMode::AutomaticLocal);return tile.Status(e);},gpuError))break;
            const bool bands=detailFusion&&std::any_of(data.sources.begin(),data.sources.end(),
                [&](const auto& s){return s.enabled&&s.group==g&&s.detailPreference<.999;});
            for(const auto& source:data.sources){
                if(!source.enabled||source.group!=g)continue;
                if(Canceled(request)){error="Canceled";return false;}
                const bool aligned=source.alignmentDiagnostic.model!=CaptureAlignment::Model::Identity||
                    (&source!=&reference&&source.alignmentDiagnostic.localApplied);
                int sx=left,sy=top,sw=width,sh=height;
                if(aligned){
                    double minX=double(data.width),minY=double(data.height),maxX=0,maxY=0;
                    for(int y=0;y<height;++y){if(Canceled(request)){error="Canceled";return false;}
                        for(int x=0;x<width;++x){
                            const auto c=EvaluateCaptureCorrespondence(source,{double(left+x),double(top+y)});
                            mapping[std::size_t(y)*width+x]={c.sourceRaw.x,c.sourceRaw.y,c.covarianceRaw.xxRawPixelsSquared,
                                c.covarianceRaw.xyRawPixelsSquared,c.covarianceRaw.yyRawPixelsSquared,c.confidence};
                            if(std::isfinite(c.sourceRaw.x)&&std::isfinite(c.sourceRaw.y)&&
                                c.sourceRaw.x>=-4&&c.sourceRaw.y>=-4&&c.sourceRaw.x<data.width+4.&&c.sourceRaw.y<data.height+4.){
                                minX=std::min(minX,c.sourceRaw.x);minY=std::min(minY,c.sourceRaw.y);
                                maxX=std::max(maxX,c.sourceRaw.x);maxY=std::max(maxY,c.sourceRaw.y);}
                        }}
                    sx=std::max(0,int(std::floor(minX))-6);sy=std::max(0,int(std::floor(minY))-6);
                    const auto right=std::min(int(data.width),int(std::ceil(maxX))+7),bottom=std::min(int(data.height),int(std::ceil(maxY))+7);
                    sw=std::max(1,right-sx);sh=std::max(1,bottom-sy);
                }
                if(std::size_t(sw)*sh>maxSource)return fallback("Warp needs a larger source region than the GPU tile allowance.");
                if(!Region(source,data,sx,sy,sw,sh,request,taps,error))return false;
                const auto parameters=SourceParameters(source);
                if(!tile.Run([&](std::string&){tile.Upload(0,taps.size()*sizeof(Tap),taps.data());
                    if(aligned)tile.Upload(1,count*sizeof(Mapping),mapping.data());tile.Upload(5,192,&parameters);
                    tile.Dispatch(programs[1],left,top,width,height,data,sx,sy,sw,sh,source,aligned,bands,false);
                    tile.Dispatch(programs[2],left,top,width,height,data,sx,sy,sw,sh,source,aligned,bands,false);
                    return true;},gpuError))break;
            }
            if(!gpuError.empty())break;
            if(!tile.Run([&](std::string& e){tile.Dispatch(programs[3],left,top,width,height,data,0,0,1,1,reference,false,bands,false);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER,tile.buffers[3]);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,count*sizeof(GpuObservation),output.data());return tile.Status(e);},gpuError))break;
            auto& group=groups[g];group.reserve(count);for(const auto& v:output)group.push_back(Unpack(v));
        }
        if(Canceled(request)){error="Canceled";return false;}
        if(!gpuError.empty())return fallback(gpuError);
        {std::lock_guard<std::mutex> lock(mutex);++gpuTiles;}
        return true;
    }
    void Report(BracketingAnalysis& analysis)const{
        std::lock_guard<std::mutex> lock(mutex);
        analysis.diagnostics.push_back("Capture combining: "+std::to_string(gpuTiles)+" GPU tiles, "+std::to_string(cpuTiles)+
            " CPU tiles; double-precision resampling, uncertainty and temporal/detail fusion; GPU peak reservation "+std::to_string(peak/(1024*1024))+" MiB."+
            (reason.empty()?"":" "+reason));
    }
    Raw::OpenGlTaskExecutor execute;std::array<GLuint,4> programs{};bool available=false;
    std::uint64_t budget=0,blockLimit=0,resident=0,peak=0,gpuTiles=0,cpuTiles=0;
    mutable std::mutex mutex;std::condition_variable wake;std::string reason;
};
TemporalGpu::TemporalGpu(const ProcessingRequest& r):impl_(std::make_unique<Impl>(r)){}
TemporalGpu::~TemporalGpu()=default;
bool TemporalGpu::Available()const{return impl_->available;}
bool TemporalGpu::Combine(const ProcessingRequest& r,const PreparedDataset& d,const Mfd::PreparedRawTile& t,
    bool detail,std::vector<std::vector<Observation>>& groups,std::string& e){return impl_->Combine(r,d,t,detail,groups,e);}
void TemporalGpu::Report(BracketingAnalysis& a)const{impl_->Report(a);}
}
