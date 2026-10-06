#include "Raw/RawZoneAreaRasterizer.h"
#include <algorithm>

namespace Stack::RawRecipe {
namespace {
void Mix(std::size_t& h,std::size_t v) {h^=v+0x9e3779b9u+(h<<6u)+(h>>2u);}
void Flip(std::vector<float>& pixels,int width,int height) {
    for (int y=0;y<height/2;++y) std::swap_ranges(pixels.begin()+std::size_t(y)*width,
        pixels.begin()+std::size_t(y+1)*width,pixels.begin()+std::size_t(height-1-y)*width);
}
}
void ZoneAreaRasterizer::Clear() {
    m_Prefix={};m_Result={};m_Strokes.clear();m_Domain=0;m_AreaId.clear();m_Footprint.clear();m_FootprintStroke={};
}
std::size_t ZoneAreaRasterizer::Bytes() const {
    return (m_Prefix.mask.size()+m_Prefix.lower.size()+m_Prefix.upper.size()+
        m_Result.mask.size()+m_Result.lower.size()+m_Result.upper.size()+m_Footprint.size())*sizeof(float);
}
bool ZoneAreaRasterizer::Apply(State& state,const RawZoneArea& area,const RawZoneBrushStroke& stroke,
    int width,int height,const RawCropRotationRecipe& transform,const ImageGuide* guide,
    const std::function<bool()>& cancelled,bool retainFootprint) {
    if (cancelled && cancelled()) return false;
    ++m_StrokeEvaluations;
    RawZoneArea footprint;footprint.sourceAspect=area.sourceAspect;footprint.strokes={stroke};
    auto& geometry=footprint.strokes[0];
    geometry.followEdges=false;geometry.erase=false;
    const auto& old=m_FootprintStroke;
    const bool extends=retainFootprint && !m_Footprint.empty() && !old.path.empty() &&
        old.radius==stroke.radius && old.softness==stroke.softness && old.opacity==stroke.opacity &&
        old.path.size()<=stroke.path.size() && std::equal(old.path.begin(),old.path.end(),stroke.path.begin(),
            [](const auto& a,const auto& b){return a.u==b.u && a.v==b.v;});
    std::vector<float> coverage;
    if (extends && old.path.size()==stroke.path.size()) coverage=m_Footprint;
    else {
        if (extends) geometry.path.erase(geometry.path.begin(),geometry.path.begin()+old.path.size()-1);
        m_GeometrySegments+=geometry.path.size();
        coverage=RasterizeZoneArea(footprint,width,height,transform,cancelled);
        if (coverage.empty()) return false;
        if (extends) for (std::size_t i=0;i<coverage.size();++i) {
            if ((i&4095u)==0 && cancelled && cancelled()) return false;
            coverage[i]=std::max(coverage[i],m_Footprint[i]);
        }
        if (retainFootprint) {m_Footprint=coverage;m_FootprintStroke=stroke;}
    }
    if (stroke.followEdges && !stroke.path.empty()) {
        if (!state.lower.empty()) for (std::size_t i=0;i<coverage.size();++i)
            coverage[i]=std::min(coverage[i],stroke.erase ? 1-state.lower[i] : state.upper[i]);
        Flip(coverage,width,height);
        const auto seed=ZoneAreaDisplayPoint(stroke.path.front().u,stroke.path.front().v,transform,false);
        if (!FollowImageEdges(*guide,coverage,int(seed.u*width),int(seed.v*height),
                stroke.radius*std::min(width,height),stroke.edgeSensitivity,cancelled)) return false;
        Flip(coverage,width,height);state.assisted=true;
    }
    if (!stroke.followEdges && state.assisted && state.lower.empty()) {
        state.lower.assign(coverage.size(),0);state.upper.assign(coverage.size(),1);
    }
    for (std::size_t i=0;i<coverage.size();++i) {
        if ((i&4095u)==0 && cancelled && cancelled()) return false;
        const float alpha=coverage[i];
        float value=stroke.erase ? std::min(state.mask[i],1-alpha) : std::max(state.mask[i],alpha);
        if (stroke.followEdges && !state.lower.empty()) value=std::clamp(value,state.lower[i],state.upper[i]);
        else if (!stroke.followEdges && state.assisted && alpha>0) {
            if (stroke.erase) {state.upper[i]=std::min(state.upper[i],1-alpha);state.lower[i]=std::min(state.lower[i],state.upper[i]);}
            else {state.lower[i]=std::max(state.lower[i],alpha);state.upper[i]=std::max(state.upper[i],state.lower[i]);}
        }
        state.mask[i]=value;
    }
    return true;
}
const std::vector<float>* ZoneAreaRasterizer::Evaluate(const RawZoneArea& area,int width,int height,
    const RawCropRotationRecipe& transform,const ImageGuide* guide,const std::function<bool()>& cancelled) {
    if (cancelled && cancelled()) return nullptr;
    if (width<=0 || height<=0 || (ZoneAreaUsesGuidance(area) && (!guide || !guide->Valid() || guide->width!=width || guide->height!=height))) return nullptr;
    std::size_t domain=std::hash<float>{}(area.sourceAspect);
    Mix(domain,width);Mix(domain,height);Mix(domain,transform.rotationDegrees);
    Mix(domain,transform.flipHorizontally);Mix(domain,transform.flipVertically);
    if (guide) {Mix(domain,guide->revision);Mix(domain,reinterpret_cast<std::size_t>(guide));}
    std::vector<std::size_t> strokes;strokes.reserve(area.strokes.size());
    for (const auto& s:area.strokes) strokes.push_back(ZoneStrokeFingerprint(s));
    if (domain!=m_Domain || m_AreaId!=area.id) Clear();
    if (!m_Result.mask.empty() && strokes==m_Strokes) return &m_Result.mask;
    const bool appended=!m_Result.mask.empty() && strokes.size()>m_Strokes.size() &&
        std::equal(m_Strokes.begin(),m_Strokes.end(),strokes.begin());
    const std::size_t prefixCount=m_Strokes.empty() ? 0 : m_Strokes.size()-1;
    const bool samePrefix=!m_Prefix.mask.empty() && strokes.size()==m_Strokes.size() &&
        std::equal(m_Strokes.begin(),m_Strokes.begin()+prefixCount,strokes.begin());
    std::size_t start=0;
    if (appended) {m_Prefix=std::move(m_Result);start=m_Strokes.size();}
    else if (samePrefix) start=prefixCount;
    else {m_Prefix={};m_Prefix.mask.assign(std::size_t(width)*height,0);}
    // Work before the final stroke is immutable until an earlier edit occurs.
    for (std::size_t i=start;i+1<area.strokes.size();++i)
        if (!Apply(m_Prefix,area,area.strokes[i],width,height,transform,guide,cancelled)) {Clear();return nullptr;}
    m_Result=m_Prefix;
    m_Strokes=std::move(strokes);m_Domain=domain;m_AreaId=area.id;
    if (!area.strokes.empty() && !Apply(m_Result,area,area.strokes.back(),width,height,transform,guide,cancelled,true)) {
        // Superseding an in-flight render must not discard completed strokes.
        m_Result={};return nullptr;
    }
    return &m_Result.mask;
}
}
