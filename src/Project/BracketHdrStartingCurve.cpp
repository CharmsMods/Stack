#include "BracketHdrStartingCurve.h"
#include "Project/RawLayerStack.h"
#include "Raw/Bracketing/Processor.h"
#include "Raw/RawColorTransform.h"
#include <algorithm>
#include <cmath>

namespace Stack::Project {
namespace {
using namespace Raw::Bracketing;

bool IsUntouchedOperation(const RawRecipe::GraphOperation& operation) {
    if (!operation.enabled) return false;
    const auto defaults=RawRecipe::MakeGraphOperation(operation.kind).parameters;
    auto parameters=operation.parameters;
    if (operation.kind==RawRecipe::GraphOperationKind::ToneCurve &&
        parameters.contains("luminanceTone") && parameters["luminanceTone"].is_object()) {
        // Earlier current-format nodes omit these optional, off-by-default
        // settings. Fill only those omissions; every authored value still has
        // to equal its neutral default before the first merge may seed a look.
        for (const char* key:{"localAmount","localRadius","localRangeEv"})
            if (!parameters["luminanceTone"].contains(key))
                parameters["luminanceTone"][key]=defaults.at("luminanceTone").at(key);
    }
    return parameters==defaults;
}

bool HasExposureBracket(const BracketingRecipe& recipe, const BracketingAnalysis& analysis) {
    double low=INFINITY, high=-INFINITY;
    unsigned count=0;
    for (const auto& group : recipe.groups) {
        if (!group.enabled) continue;
        bool measured=false;
        for (const auto& frame : group.frames) {
            const auto ev=analysis.calibratedEv.find(frame.id);
            if (!frame.enabled || ev==analysis.calibratedEv.end() || !std::isfinite(ev->second)) continue;
            low=std::min(low,ev->second); high=std::max(high,ev->second); measured=true;
        }
        if (measured) ++count;
    }
    return count>=2 && high-low>=.75;
}

// Spatial correspondence is essential to merge detail. For the global display
// range, a real unclipped capture can also corroborate the brightness of an
// estimated result. Never fit solely to the magnitude of a fallback pixel.
bool Corroborated(const Preview& p, const BracketingRecipe& recipe, std::size_t i,
    const std::array<double,3>& weights, double y) {
    const auto n=recipe.groups.size();
    const auto pixels=std::size_t(p.width)*p.height;
    if (p.sourceRgb.size()!=pixels*n*3 || p.samples.size()!=pixels*n*4) return false;
    for (std::size_t g=0;g<n;++g) {
        if (!recipe.groups[g].enabled) continue;
        bool measured=true;
        for (unsigned c=0;c<4;++c) {
            const auto& s=p.samples[(i*4+c)*n+g];
            measured &= s.finite && (s.support>0 ? !s.clipped : !s.fallbackClipped);
        }
        const auto* rgb=p.sourceRgb.data()+(i*n+g)*3;
        const double capture=rgb[0]*weights[0]+rgb[1]*weights[1]+rgb[2]*weights[2];
        if (measured && capture>0 && std::abs(capture-y)<=.12*y) return true;
    }
    return false;
}

float CredibleWhiteEv(const Preview& p,const BracketingRecipe& recipe) {
    const auto pixels=std::size_t(p.width)*p.height;
    if (!pixels || p.diagnostics.size()!=pixels || p.resultRgb.size()!=pixels*3) return 0;
    Raw::RawDevelopSettings settings;
    settings.workingSpace=Raw::RawWorkingSpace::LinearRec2020D65;
    const auto wb=Raw::ResolveRawWhiteBalance(p.metadata,settings);
    const auto matrix=Raw::BuildRawCameraToWorkingTransform(p.metadata,settings,true);
    std::array<double,3> weights{};
    for (int c=0;c<3;++c)
        weights[c]=wb[c]*(.2627*matrix[c]+.6780*matrix[3+c]+.0593*matrix[6+c]);
    const unsigned step=std::max(1u,(std::max(p.width,p.height)+191)/192);
    std::vector<float> values;
    for(unsigned row=0;row<p.height;row+=step) for(unsigned col=0;col<p.width;col+=step) {
        const auto i=std::size_t(row)*p.width+col;
        const auto* rgb=p.resultRgb.data()+3*i;
        const double y=weights[0]*rgb[0]+weights[1]*rgb[1]+weights[2]*rgb[2];
        if (!(y>0 && std::isfinite(y)) || (p.diagnostics[i]&Preview::Unrecoverable)) continue;
        if ((p.diagnostics[i]&(Preview::Fallback|Preview::Invalid)) && !Corroborated(p,recipe,i,weights,y)) continue;
        values.push_back(float(y));
    }
    if (values.size()<128) return 0;
    const auto white=values.begin()+((values.size()-1)*99)/100;
    std::nth_element(values.begin(),white,values.end());
    return std::log2(*white/.18f);
}
}

bool FitBracketStartingCurve(RawLayerStackState& state,const BracketingResult& result,const BracketingRecipe& recipe) {
    if (result.panorama || !result.analysis || !HasExposureBracket(recipe,*result.analysis) ||
        state.explicitRouting || !state.layers.empty() || !state.background.enabled ||
        state.background.opacity!=1 || state.background.layerMask) return false;
    // The preview describes the unedited merge. Do not seed a look using that
    // preview once the user has authored upstream exposure, color or tone.
    for (const auto& node : state.background.graph.GetNodes()) {
        if (node.kind==EditorNodeGraph::NodeKind::Layer) return false;
        if (node.kind!=EditorNodeGraph::NodeKind::RawOperation) continue;
        if (!IsUntouchedOperation(node.rawOperation)) return false;
    }
    auto* tone=FindRawOperation(state.background,RawRecipe::GraphOperationKind::ToneCurve);
    if (!tone) return false;
    const float white=CredibleWhiteEv(result.preview,recipe);
    constexpr float knee=1.f, target=2.5f;
    if (!(white>target+.15f)) return false;
    // Place regional scene white below Sigmoid's crowded upper shoulder, while
    // local contrast preserves detail around that base. Leave lower regional
    // bases at their original exposure and keep a positive specular tail.
    const float a=(white-target)/((target-knee)*(white-knee));
    const float tailSlope=std::pow((target-knee)/(white-knee),2.f);
    RawRecipe::SceneTone curve;
    curve.localAmount=1;
    curve.curve.clear();
    for(float ev : {-16.f,knee,1.5f,2.f,2.5f,3.f,4.f,5.f,6.f,8.f,10.f,12.f,16.f}) {
        const float distance=ev-knee;
        const float mapped=ev<=knee?ev:(ev>white?target+(ev-white)*tailSlope:knee+distance/(1+a*distance));
        curve.curve.push_back({(ev+16)/32,(mapped+16)/32});
    }
    auto edited=RawRecipe::ReadGraphOperation(tone->rawOperation);
    edited.finishTone.layerJson["curveMode"]="luminance";
    edited.finishTone.layerJson["luminanceTone"]=RawRecipe::SerializeSceneTone(curve);
    std::string error;
    return WriteRawLayerOperation(state.background,tone->instanceUuid,edited,error);
}
}
