#include "Internal.h"
#include "NodeMath/ContractTypes.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GpuMemoryBudget.h"
#include <cmath>

namespace Raw::Bracketing::Panorama {
std::string PreparationIdentity(const ProcessingRequest& request) {
    auto recipe=request.recipe;recipe.panorama={};recipe.knots=EqualCurves(recipe.groups.size());recipe.automatic=true;
    for(auto& group:recipe.groups){group.name.clear();group.colorSlot=-1;for(auto& f:group.frames){f.manualExposure=false;f.relativeEv=0;}}
    auto j=Serialize(recipe);j["panoramaRevision"]=Revision;
    for(const auto& s:request.sources)j["sources"].push_back({s.frameId,s.sha256,s.bytes});
    if(request.sources.empty()&&request.analysis) {
        const auto stored=nlohmann::json::parse(request.analysis->identity,nullptr,false);
        if(stored.is_object()&&stored.contains("sources"))j["sources"]=stored["sources"];
    }
    return j.dump();
}
BracketingResult Process(const ProcessingRequest& request) {
    BracketingResult result;
    try {
        CheckCanceled(request);std::string error;
        if(request.version!=1||!Validate(request.recipe,error))throw std::runtime_error(error.empty()?"Unsupported panorama request.":error);
        const auto identity=PreparationIdentity(request);
        const bool reuse=request.analysis&&request.analysis->panorama&&request.analysis->identity==identity;
        auto data=reuse?std::make_shared<Prepared>(*request.analysis->panorama):Prepare(request);
        // Reused files remain owned by the original immutable analysis.
        if(reuse){data->removeOnRelease=false;data->cacheOwner=request.analysis->panorama->cacheOwner?request.analysis->panorama->cacheOwner:request.analysis->panorama;}
        if(!reuse&&!request.preparationOnly)SolveLayout(request,*data);
        auto analysis=std::make_shared<BracketingAnalysis>();analysis->identity=identity;
        analysis->panorama=data;
        for(const auto& source:data->captures) {
            auto original=std::make_shared<CapturePreview>();original->width=unsigned(source.proxy.cols);original->height=unsigned(source.proxy.rows);
            original->rgb.assign(source.proxy.ptr<float>(),source.proxy.ptr<float>()+source.proxy.total()*3);
            original->clipped.assign(source.clipping.ptr<unsigned char>(),source.clipping.ptr<unsigned char>()+source.clipping.total());
            analysis->originals[source.source.frameId]=std::move(original);
        }
        result.analysis=analysis;
        if(request.preparationOnly){result.status=BracketingResult::Status::Completed;result.message="Panorama captures ready for inspection.";return result;}
        // An inspection-only preparation has no fitted geometry yet.
        if(data->layout.cameras.empty())SolveLayout(request,*data);
        ChooseCanvas(request,data->layout);
        result.panorama=std::make_shared<Layout>(data->layout);
        unsigned maxDimension=request.maximumOutputDimension;
        RawGpuMemoryBudgetDecision graphicsBudget;
        if(request.executeOpenGlTask) {
            std::string deviceError;
            if(!request.executeOpenGlTask([&](std::string&){
                GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);
                if(limit>0)maxDimension=maxDimension?std::min(maxDimension,unsigned(limit)):unsigned(limit);
                graphicsBudget=ResolveRawGpuMemoryBudget(Stack::Renderer::QueryGpuMemoryBudget());
                return limit>0;},deviceError))
                throw std::runtime_error("Could not query the graphics limit for the panorama: "+deviceError);
        }
        const auto& l=data->layout;const auto pixels=std::uint64_t(l.width)*l.height;
        const std::uint64_t reserve=256ull*1024*1024;
        const double available=request.memoryBudgetBytes>reserve?double(request.memoryBudgetBytes-reserve):0;
        double allowed=std::min(1.,std::sqrt(available/std::max(1.,double(pixels)*160)));
        if(request.executeOpenGlTask&&!graphicsBudget.usedFallback)
            allowed=std::min(allowed,std::sqrt(double(graphicsBudget.workingBudgetBytes)/std::max(1.,double(pixels)*96)));
        if(maxDimension)allowed=std::min(allowed,double(maxDimension)/std::max(l.width,l.height));
        if(allowed<1) {
            const int percentage=int(std::floor(request.recipe.panorama.outputScale*allowed*100));
            const std::string choice=percentage<1?
                " Even the minimum 1% output exceeds the available memory or graphics limit. Use a smaller capture selection.":
                " Choose an output size of "+std::to_string(percentage)+"% or less for the available memory and graphics limits.";
            throw std::runtime_error("Panorama is "+std::to_string(l.width)+" x "+std::to_string(l.height)+"."+choice+" The layout has been retained.");
        }
        if(data->captures.size()>65534)throw std::runtime_error("Too many panorama captures.");
        auto seams=MatchBrightnessAndSeams(request,*data);result.panorama=std::make_shared<Layout>(data->layout);
        result.identity=Stack::NodeMath::Sha256ContentIdentity(identity+Identity(request.recipe));
        result.raw=std::make_shared<RawImageData>();auto& raw=*result.raw;
        raw.metadata=data->captures[data->reference].metadata;auto& m=raw.metadata;
        m.rawWidth=m.visibleWidth=int(l.width);m.rawHeight=m.visibleHeight=int(l.height);m.orientation=1;m.leftMargin=m.topMargin=0;
        m.mosaiced=false;m.cfaPattern=CfaPattern::Unknown;m.pixelLayout=RawPixelLayout::LinearRgb;m.linearChannels=3;m.linearSampleFormat=RawSampleFormat::Float32;
        m.dngActiveArea={0,0,int(l.height),int(l.width)};m.dngGainMaps.clear();m.dngGainMapCount=0;
        m.dngLinearizationTable.clear();m.dngBlackLevelValues.clear();m.dngBlackLevelDeltaH.clear();m.dngBlackLevelDeltaV.clear();
        m.blackLevel=0;m.whiteLevel=1;m.perChannelBlack={0,0,0,0};m.sourceByteSize=pixels*12;
        raw.reconstructedCameraRgb=true;raw.contentIdentity=result.identity;raw.contentIdentityHash=14695981039346656037ull;
        for(unsigned char c:result.identity){raw.contentIdentityHash^=c;raw.contentIdentityHash*=1099511628211ull;}
        Composite(request,*data,seams,result);MakePreview(result);
        result.preview.originals=analysis->originals;
        result.status=BracketingResult::Status::Completed;result.message="Panorama ready.";
        Progress(request,ProcessingStage::Completed,1,1,1);
    }catch(const std::exception& e) {
        result.message=e.what();result.status=request.shouldCancel&&request.shouldCancel()?BracketingResult::Status::Canceled:BracketingResult::Status::Failed;
        result.raw.reset();
    }
    return result;
}
nlohmann::json SerializeLayout(const Layout& layout) {
    nlohmann::json j={{"version",1},{"projection",int(layout.projection)},{"width",layout.width},{"height",layout.height},
        {"scale",layout.scale},{"offsetX",layout.offsetX},{"offsetY",layout.offsetY},{"cameras",nlohmann::json::array()}};
    for(const auto& c:layout.cameras)j["cameras"].push_back({{"id",c.frameId},{"name",c.name},{"width",c.width},{"height",c.height},
        {"focal",c.focal},{"cx",c.cx},{"cy",c.cy},{"k1",c.k1},{"k2",c.k2},{"gain",c.gain},{"rotation",c.rotation}});
    return j;
}
std::shared_ptr<const Layout> DeserializeLayout(const nlohmann::json& j) {
    auto l=std::make_shared<Layout>();
    if(j.at("version")!=1)throw std::runtime_error("Unsupported panorama layout version.");
    l->projection=static_cast<PanoramaProjection>(j.at("projection").get<int>());l->width=j.at("width");l->height=j.at("height");
    l->scale=j.at("scale");l->offsetX=j.at("offsetX");l->offsetY=j.at("offsetY");
    if((l->projection!=PanoramaProjection::Perspective&&l->projection!=PanoramaProjection::Spherical)||
       !l->width||!l->height||!std::isfinite(l->scale)||l->scale<=0||!std::isfinite(l->offsetX)||!std::isfinite(l->offsetY))
        throw std::runtime_error("Invalid panorama canvas.");
    for(const auto& v:j.at("cameras")) {
        Camera c;c.frameId=v.at("id");c.name=v.at("name");c.width=v.at("width");c.height=v.at("height");
        c.focal=v.at("focal");c.cx=v.at("cx");c.cy=v.at("cy");c.k1=v.at("k1");c.k2=v.at("k2");c.gain=v.at("gain");c.rotation=v.at("rotation").get<std::array<double,9>>();
        if(!c.width||!c.height||!std::isfinite(c.focal)||c.focal<=0||!std::isfinite(c.gain)||c.gain<=0||
           !std::isfinite(c.cx)||!std::isfinite(c.cy)||!std::isfinite(c.k1)||!std::isfinite(c.k2)||
           !std::all_of(c.rotation.begin(),c.rotation.end(),[](double d){return std::isfinite(d);}))throw std::runtime_error("Invalid panorama camera.");
        l->cameras.push_back(std::move(c));
    }
    if(l->cameras.size()<2||l->cameras.size()>65534)throw std::runtime_error("Invalid panorama capture count.");
    return l;
}
void MakePreview(BracketingResult& result) {
    const auto& raw=*result.raw;auto& preview=result.preview;
    const auto w=unsigned(raw.metadata.visibleWidth),h=unsigned(raw.metadata.visibleHeight);
    const double scale=std::min(1.,1024./std::max(w,h));preview.width=std::max(1u,unsigned(w*scale));preview.height=std::max(1u,unsigned(h*scale));
    preview.metadata=raw.metadata;preview.sensorStepX=double(w)/preview.width;preview.sensorStepY=double(h)/preview.height;
    const auto count=std::size_t(preview.width)*preview.height;preview.resultRgb.assign(count*3,0);preview.coverage.assign(count,0);
    for(unsigned y=0;y<preview.height;++y)for(unsigned x=0;x<preview.width;++x) {
        const auto p=std::size_t(y)*preview.width+x;double weight=0;std::array<double,3> color{};
        const unsigned left=std::uint64_t(x)*w/preview.width,right=std::uint64_t(x+1)*w/preview.width;
        const unsigned top=std::uint64_t(y)*h/preview.height,bottom=std::uint64_t(y+1)*h/preview.height;
        for(unsigned yy=top;yy<bottom;++yy)for(unsigned xx=left;xx<right;++xx) {
            const auto q=std::size_t(yy)*w+xx;const float a=raw.outputCoverage?(*raw.outputCoverage)[q]:1;weight+=a;
            for(int c=0;c<3;++c)color[c]+=raw.linearFloatBuffer[q*3+c]*a;
        }
        preview.coverage[p]=float(weight/((right-left)*(bottom-top)));
        if(weight>0)for(int c=0;c<3;++c)preview.resultRgb[p*3+c]=float(color[c]/weight);
    }
}
Preview NativeDetail(const BracketingResult& result,unsigned x,unsigned y,unsigned size) {
    Preview p;if(!result.raw||!result.panorama)return p;const auto& raw=*result.raw;const auto w=unsigned(raw.metadata.visibleWidth),h=unsigned(raw.metadata.visibleHeight);
    p.width=std::min(size,w);p.height=std::min(size,h);const auto left=std::min(x>p.width/2?x-p.width/2:0,w-p.width),top=std::min(y>p.height/2?y-p.height/2:0,h-p.height);
    p.sensorOriginX=left;p.sensorOriginY=top;p.metadata=raw.metadata;p.resultRgb.resize(std::size_t(p.width)*p.height*3);p.coverage.resize(std::size_t(p.width)*p.height);
    for(unsigned yy=0;yy<p.height;++yy)for(unsigned xx=0;xx<p.width;++xx) {
        const auto q=std::size_t(yy+top)*w+xx+left,o=std::size_t(yy)*p.width+xx;
        for(int c=0;c<3;++c)p.resultRgb[o*3+c]=raw.linearFloatBuffer[q*3+c];p.coverage[o]=raw.outputCoverage?(*raw.outputCoverage)[q]:1;
    }
    return p;
}
}
