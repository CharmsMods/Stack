#include "Panorama.h"
#include "../Processor.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
}
