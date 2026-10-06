#include "Recipe.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

namespace Raw::Bracketing {
const char* ReconstructionModeName(ReconstructionMode mode) {
    switch(mode) {
        case ReconstructionMode::Standard:return "standard";
        case ReconstructionMode::SuperResolution1x:return "super-resolution-1x";
        case ReconstructionMode::SuperResolution2x:return "super-resolution-2x";
        case ReconstructionMode::Panorama:return "panorama";
    }
    return "invalid";
}
bool ParseReconstructionMode(const std::string& text,ReconstructionMode& mode) {
    for(auto value:{ReconstructionMode::Standard,
            ReconstructionMode::SuperResolution1x,ReconstructionMode::SuperResolution2x,ReconstructionMode::Panorama})
        if(text==ReconstructionModeName(value)){mode=value;return true;}
    return false;
}
const char* AlignmentModeName(AlignmentMode mode) {
    switch (mode) {
        case AlignmentMode::AutomaticLocal: return "automatic-local";
        case AlignmentMode::AutomaticGlobal: return "automatic-global";
        case AlignmentMode::TranslationOnly: return "translation-only";
        case AlignmentMode::FixedCoordinates: return "fixed-coordinates";
    }
    return "invalid";
}
bool ParseAlignmentMode(const std::string& value, AlignmentMode& mode) {
    if (value == "automatic-local") mode = AlignmentMode::AutomaticLocal;
    else if (value == "automatic-global") mode = AlignmentMode::AutomaticGlobal;
    else if (value == "translation-only") mode = AlignmentMode::TranslationOnly;
    else if (value == "fixed-coordinates") mode = AlignmentMode::FixedCoordinates;
    else return false;
    return true;
}
void Redistribute(std::vector<double>& values, std::size_t selected, double value,
    const std::vector<double>& automatic) {
    if (selected >= values.size()) return;
    if (values.size() == 1) { values[0] = 1; return; }
    value = std::clamp(std::isfinite(value) ? value : 0.0, 0.0, 1.0);
    double sum = 0;
    for (std::size_t i = 0; i < values.size(); ++i)
        if (i != selected) sum += values[i] = std::max(0.0, values[i]);
    if (sum < 1e-12) {
        for (std::size_t i = 0; i < values.size(); ++i) if (i != selected) {
            values[i] = automatic.size() == values.size() ? std::max(0.0, automatic[i]) : 1;
            sum += values[i];
        }
    }
    if (sum < 1e-12) {
        sum = static_cast<double>(values.size() - 1);
        for (auto& v : values) v = 1;
    }
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = i == selected ? value : values[i] * (1 - value) / sum;
}
std::vector<Knot> EqualCurves(std::size_t count, double lo, double hi) {
    if (!count) return {};
    std::vector<double> shares(count, 1.0 / count);
    return {{lo, lo, lo + (hi-lo)/3, shares, shares, shares},
            {hi, hi - (hi-lo)/3, hi, shares, shares, shares}};
}
namespace {
double Bezier(double a, double b, double c, double d, double t) {
    const double s = 1-t;
    return s*s*s*a + 3*s*s*t*b + 3*s*t*t*c + t*t*t*d;
}
double Parameter(const Knot& a, const Knot& b, double ev) {
    double lo=0, hi=1;
    for (int i=0; i<40; ++i) {
        const double t=(lo+hi)*.5;
        if (Bezier(a.ev,a.outgoing,b.incoming,b.ev,t)<ev) lo=t; else hi=t;
    }
    return (lo+hi)*.5;
}
bool Simplex(const std::vector<double>& values, std::size_t count) {
    if (values.size()!=count) return false;
    double sum=0;
    for (double v : values) { if (!std::isfinite(v)||v<0||v>1) return false; sum+=v; }
    return std::abs(sum-1)<1e-7;
}
}
std::vector<double> Evaluate(const std::vector<Knot>& knots, double ev) {
    if (knots.empty()) return {};
    if (ev<=knots.front().ev) return knots.front().share;
    if (ev>=knots.back().ev) return knots.back().share;
    const auto it=std::upper_bound(knots.begin(),knots.end(),ev,
        [](double x,const Knot& k){return x<k.ev;});
    const auto& a=*(it-1); const auto& b=*it;
    const double t=Parameter(a,b,ev);
    auto result=a.share;
    for (std::size_t i=0;i<result.size();++i)
        result[i]=Bezier(a.share[i],a.right[i],b.left[i],b.share[i],t);
    return result;
}
void InsertKnot(std::vector<Knot>& knots, double ev) {
    if (knots.size()<2||ev<=knots.front().ev||ev>=knots.back().ev) return;
    auto it=std::upper_bound(knots.begin(),knots.end(),ev,
        [](double x,const Knot& k){return x<k.ev;});
    auto& a=*(it-1); auto& b=*it;
    if (ev-a.ev<.01||b.ev-ev<.01) return;
    const double t=Parameter(a,b,ev);
    const auto lerp=[t](double x,double y){return x+(y-x)*t;};
    Knot k; k.share.resize(a.share.size()); k.left=k.right=k.share;
    const double ab=lerp(a.ev,a.outgoing),bc=lerp(a.outgoing,b.incoming),cd=lerp(b.incoming,b.ev);
    k.incoming=lerp(ab,bc); k.outgoing=lerp(bc,cd); k.ev=lerp(k.incoming,k.outgoing);
    a.outgoing=ab; b.incoming=cd;
    for (std::size_t i=0;i<k.share.size();++i) {
        const double p=lerp(a.share[i],a.right[i]),q=lerp(a.right[i],b.left[i]),r=lerp(b.left[i],b.share[i]);
        k.left[i]=lerp(p,q); k.right[i]=lerp(q,r); k.share[i]=lerp(k.left[i],k.right[i]);
        a.right[i]=p; b.left[i]=r;
    }
    knots.insert(it,std::move(k));
}
bool Validate(const BracketingRecipe& r, std::string& error, bool allowInactiveDraft) {
    const auto fail=[&](const char* e){error=e;return false;};
    for (const auto& entry : r.orientationOverrides)
        if (entry.first < 0 || entry.first > 8 || entry.second < 0 || entry.second > 8)
            return fail("Invalid bracket orientation correction.");
    if (r.version!=RecipeVersion) return fail("Unsupported bracketing recipe version.");
    if(std::string(ReconstructionModeName(r.reconstruction))=="invalid") return fail("Invalid reconstruction mode.");
    if(int(r.panorama.projection)<0||int(r.panorama.projection)>2||
       !std::isfinite(r.panorama.outputScale)||r.panorama.outputScale<.01||r.panorama.outputScale>1)
        return fail("Panorama output size must be between 1% and 100% with a supported projection.");
    if(std::string(AlignmentModeName(r.alignmentMode))=="invalid") return fail("Invalid bracketing alignment mode.");
    if (r.groups.empty()||r.groups.size()>64) return fail("Use between one and 64 exposure groups.");
    std::unordered_set<std::string> frames,groups;
    bool origin=false,enabled=false;
    for (const auto& g:r.groups) {
        if(g.id.empty()||!groups.insert(g.id).second||g.frames.empty()) return fail("Groups need unique identities and at least one capture.");
        for(const auto& f:g.frames) {
            if(f.id.empty()||!frames.insert(f.id).second) return fail("A capture can belong to only one group.");
            if(!std::isfinite(f.relativeEv)||std::abs(f.relativeEv)>32) return fail("Relative exposure must be finite and within 32 EV.");
            origin |= f.id==r.originFrameId;
            enabled |= g.enabled&&f.enabled;
        }
    }
    if(!origin) return fail("The exposure origin is missing.");
    if(!enabled&&!allowInactiveDraft) return fail("Enable at least one capture.");
    if(r.knots.size()<2||r.knots.size()>128) return fail("Use between two and 128 curve points.");
    for(std::size_t i=0;i<r.knots.size();++i) {
        const auto& k=r.knots[i];
        if(!std::isfinite(k.ev)||!std::isfinite(k.incoming)||!std::isfinite(k.outgoing)||
           k.incoming>k.ev||k.outgoing<k.ev||!Simplex(k.share,r.groups.size())||
           !Simplex(k.left,r.groups.size())||!Simplex(k.right,r.groups.size()))
            return fail("Contribution curves must be finite, nonnegative, and total 100%.");
        if(i&& (k.ev-r.knots[i-1].ev<.001||r.knots[i-1].outgoing>k.incoming))
            return fail("Curve points and handles must remain ordered.");
    }
    error.clear();return true;
}
std::vector<int> ColorSlots(const BracketingRecipe& recipe) {
    std::vector<int> slots(recipe.groups.size(),-1);bool used[64]{};
    for(std::size_t i=0;i<slots.size();++i) {
        const int slot=recipe.groups[i].colorSlot;
        if(slot>=0&&slot<64&&!used[slot]) {slots[i]=slot;used[slot]=true;}
    }
    for(auto& slot:slots) if(slot<0) for(int c=0;c<64;++c) if(!used[c]) {slot=c;used[c]=true;break;}
    return slots;
}
nlohmann::json Serialize(const BracketingRecipe& r) {
    nlohmann::json j={{"version",RecipeVersion},{"origin",r.originFrameId},{"automatic",r.automatic},
        {"alignmentMode",AlignmentModeName(r.alignmentMode)},
        {"reconstruction",ReconstructionModeName(r.reconstruction)},
        {"groups",nlohmann::json::array()},{"knots",nlohmann::json::array()}};
    j["panorama"]={{"projection",int(r.panorama.projection)},{"outputScale",r.panorama.outputScale},{"overlapDenoise",r.panorama.overlapDenoise}};
    j["orientationOverrides"] = nlohmann::json::object();
    for (const auto& entry : r.orientationOverrides)
        j["orientationOverrides"][std::to_string(entry.first)] = entry.second;
    const auto slots=ColorSlots(r);std::size_t colorIndex=0;
    for(const auto& g:r.groups) {
        nlohmann::json v={{"id",g.id},{"name",g.name},{"enabled",g.enabled},{"colorSlot",slots[colorIndex++]},{"frames",nlohmann::json::array()}};
        for(const auto& f:g.frames) v["frames"].push_back({{"id",f.id},{"enabled",f.enabled},{"manualExposure",f.manualExposure},{"relativeEv",f.relativeEv}});
        j["groups"].push_back(std::move(v));
    }
    for(const auto& k:r.knots) j["knots"].push_back({{"ev",k.ev},{"incoming",k.incoming},{"outgoing",k.outgoing},
        {"share",k.share},{"left",k.left},{"right",k.right}});
    return j;
}
bool Deserialize(const nlohmann::json& j,BracketingRecipe& output,std::string& error,bool allowInactiveDraft) {
    try {
        BracketingRecipe r; const int storedVersion=j.at("version").get<int>();
        if(storedVersion<1||storedVersion>RecipeVersion) {error="Unsupported bracketing recipe version.";return false;}
        r.version=RecipeVersion;r.originFrameId=j.at("origin").get<std::string>();
        r.automatic=j.value("automatic",true);
        if(j.contains("panorama")) {
            const auto& p=j.at("panorama");
            r.panorama.projection=static_cast<PanoramaProjection>(p.value("projection",0));
            r.panorama.outputScale=p.value("outputScale",1.0);
            r.panorama.overlapDenoise=p.value("overlapDenoise",true);
        }
        if (j.contains("orientationOverrides")) {
            const auto& overrides = j.at("orientationOverrides");
            if (!overrides.is_object()) { error="Invalid orientation corrections."; return false; }
            for (auto entry=overrides.begin(); entry!=overrides.end(); ++entry) {
                if (entry.key().size()!=1 || entry.key()[0]<'0' || entry.key()[0]>'8' || !entry.value().is_number_integer() ||
                    entry.value() < 0 || entry.value() > 8) {
                    error="Invalid orientation correction."; return false;
                }
                r.orientationOverrides[entry.key()[0]-'0'] = entry.value().get<int>();
            }
        }
        if(storedVersion>=4&&!ParseReconstructionMode(j.value("reconstruction",std::string("standard")),r.reconstruction)) {
            error="Invalid reconstruction mode.";return false;
        }
        if(storedVersion==1) r.alignmentMode=AlignmentMode::FixedCoordinates;
        else if(!ParseAlignmentMode(j.value("alignmentMode",std::string()),r.alignmentMode)) {error="Invalid bracketing alignment mode.";return false;}
        for(const auto& v:j.at("groups")) {
            Group g;g.id=v.at("id");g.name=v.at("name");g.enabled=v.value("enabled",true);g.colorSlot=v.value("colorSlot",-1);
            for(const auto& f:v.at("frames")) g.frames.push_back({f.at("id"),f.value("enabled",true),f.value("manualExposure",false),f.value("relativeEv",0.0)});
            r.groups.push_back(std::move(g));
        }
        for(const auto& v:j.at("knots")) r.knots.push_back({v.at("ev"),v.at("incoming"),v.at("outgoing"),
            v.at("share").get<std::vector<double>>(),v.at("left").get<std::vector<double>>(),v.at("right").get<std::vector<double>>()});
        const auto slots=ColorSlots(r);for(std::size_t i=0;i<r.groups.size();++i)r.groups[i].colorSlot=slots[i];
        if(!Validate(r,error,allowInactiveDraft)) return false;output=std::move(r);return true;
    } catch(const std::exception& e) {error=std::string("Invalid bracketing recipe: ")+e.what();return false;}
}
int ResolveOrientation(const BracketingRecipe& recipe, int original) {
    const auto it = recipe.orientationOverrides.find(original);
    const int value = it == recipe.orientationOverrides.end() ? original : it->second;
    return value == 0 ? 1 : value;
}
bool UsesStoredFrame(const nlohmann::json& recipe, const std::string& frameId) {
    if (!recipe.is_object()) return true;
    const auto origin = recipe.find("origin");
    if (origin == recipe.end() || !origin->is_string()) return true;
    if (*origin == frameId) return true;
    const auto groups = recipe.find("groups");
    if (groups == recipe.end() || !groups->is_array()) return true;
    for (const auto& group : *groups) {
        if (!group.is_object()) return true;
        const auto enabled = group.find("enabled");
        if (enabled != group.end() && !enabled->is_boolean()) return true;
        if (enabled != group.end() && !enabled->get<bool>()) continue;
        const auto frames = group.find("frames");
        if (frames == group.end() || !frames->is_array()) return true;
        for (const auto& frame : *frames) {
            if (!frame.is_object()) return true;
            const auto id = frame.find("id"), active = frame.find("enabled");
            if (id == frame.end() || !id->is_string()) return true;
            if (active != frame.end() && !active->is_boolean()) return true;
            if (*id == frameId && (active == frame.end() || active->get<bool>())) return true;
        }
    }
    return false;
}
int ResolveStoredOrientation(const nlohmann::json& recipe, int original) {
    const auto overrides = recipe.find("orientationOverrides");
    if (overrides != recipe.end() && overrides->is_object()) {
        const auto value = overrides->find(std::to_string(original));
        if (value != overrides->end() && value->is_number_integer() && *value >= 0 && *value <= 8) {
            const int orientation = value->get<int>();
            if (orientation >= 0 && orientation <= 8) return orientation == 0 ? 1 : orientation;
        }
    }
    return original == 0 ? 1 : original;
}
std::string Identity(const BracketingRecipe& r) {return Serialize(r).dump();}
namespace {
bool Active(const Group& g) {return g.enabled&&std::any_of(g.frames.begin(),g.frames.end(),[](const auto& f){return f.enabled;});}
}
void EditContribution(const BracketingRecipe& recipe,std::vector<double>& shares,std::size_t selected,double value,const std::vector<double>& automatic) {
    std::vector<double> active,suggestion;std::vector<std::size_t> indices;std::size_t index=0;
    for(std::size_t g=0;g<recipe.groups.size();++g) if(Active(recipe.groups[g])) {
        if(g==selected) index=indices.size();indices.push_back(g);active.push_back(shares[g]);
        suggestion.push_back(g<automatic.size()?automatic[g]:1);
    }
    if(!Active(recipe.groups[selected])) return;
    Redistribute(active,index,value,suggestion);std::fill(shares.begin(),shares.end(),0);
    for(std::size_t i=0;i<indices.size();++i) shares[indices[i]]=active[i];
}
void ConstrainEnabledCurves(BracketingRecipe& recipe) {
    std::size_t enabled=0;for(const auto& g:recipe.groups) enabled+=Active(g);
    if(!enabled) return;
    for(auto& k:recipe.knots) for(auto* values:{&k.share,&k.left,&k.right}) {
        if(values->size()!=recipe.groups.size()) continue;
        double sum=0;for(std::size_t g=0;g<values->size();++g) if(Active(recipe.groups[g]))sum+=(*values)[g];
        for(std::size_t g=0;g<values->size();++g) (*values)[g]=!Active(recipe.groups[g])?0:sum>0?(*values)[g]/sum:1.0/enabled;
    }
}
void RemapCurves(BracketingRecipe& recipe,const std::vector<Group>& previous) {
    const auto remap=[&](std::vector<double>& values) {
        std::vector<double> mapped(recipe.groups.size());double sum=0;
        for(std::size_t i=0;i<recipe.groups.size();++i) {
            const auto found=std::find_if(previous.begin(),previous.end(),[&](const Group& g){return g.id==recipe.groups[i].id;});
            if(found!=previous.end()&&static_cast<std::size_t>(found-previous.begin())<values.size())
                mapped[i]=values[found-previous.begin()];
            sum+=mapped[i];
        }
        for(auto& value:mapped) value=sum>0?value/sum:1.0/mapped.size();
        values=std::move(mapped);
    };
    for(auto& knot:recipe.knots) {remap(knot.share);remap(knot.left);remap(knot.right);}
}
} // namespace Raw::Bracketing
