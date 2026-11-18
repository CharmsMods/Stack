#include "Raw/Curves/PreparedCurve.h"
#include "Raw/Tone/SceneTone.h"
#include "Renderer/LocalEvCurveUniforms.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Stack::RawRecipe;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool Same(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }
float Cubic(float a, float b, float c, float d, float t) {
    const float u = 1.f - t;
    return u*u*u*a + 3.f*u*u*t*b + 3.f*u*t*t*c + t*t*t*d;
}
// The pre-optimization scalar evaluator is the comparison contract: ordinary
// handles discarded the inverse solve, while manual X handles retain it.
float Reference(const std::vector<RawBezierCurvePoint>& points, float x) {
    if (points.empty()) return std::clamp(x, 0.f, 1.f);
    if (points.size() == 1) return std::clamp(points.front().y, 0.f, 1.f);
    x = std::clamp(x, 0.f, 1.f);
    if (x <= points.front().x) return std::clamp(points.front().y, 0.f, 1.f);
    if (x >= points.back().x) return std::clamp(points.back().y, 0.f, 1.f);
    std::size_t i = 0;
    while (i+1 < points.size() && x > points[i+1].x) ++i;
    const auto s = BuildRawBezierSegment(points, std::min(i, points.size()-2));
    float lo=0, hi=1;
    for (int iteration=0; iteration<18; ++iteration) {
        const float t=(lo+hi)*.5f;
        if (Cubic(s.left.x,s.leftHandleX,s.rightHandleX,s.right.x,t) < x) lo=t;
        else hi=t;
    }
    const float span=s.right.x-s.left.x;
    const bool linear=std::abs(s.leftHandleX-(s.left.x+span/3))<1e-6f &&
        std::abs(s.rightHandleX-(s.right.x-span/3))<1e-6f;
    const float t=linear ? (x-s.left.x)/span : (lo+hi)*.5f;
    return std::clamp(Cubic(s.left.y,s.leftHandleY,s.rightHandleY,s.right.y,t),0.f,1.f);
}
void PreparedCurves() {
    std::vector<std::vector<RawBezierCurvePoint>> cases{
        {}, {{.3f,.8f}}, {{0,0,{1},{1}},{1,1,{1},{1}}},
        {{0,.1f,{1},{1}},{.2f,.12f,{1},{1}},{.45f,.6f,{1},{1}},{.8f,.7f,{1},{1}},{1,.95f,{1},{1}}}};
    auto manual=cases.back();
    manual[1].outgoing={1.4f,true,.2f,.35f};
    manual[2].incoming={.8f,true,-.03f,-.4f};
    manual[2].outgoing={1.f,true,.3f,-.15f};
    manual[3].incoming={1.f,true,-.2f,.25f};
    cases.push_back(manual);
    for (const auto& points:cases) {
        const PreparedRawBezierCurve prepared(points);
        for (int i=-1;i<=4096;++i) {
            const float x=static_cast<float>(i)/4095.f;
            const float expected=Reference(points,x);
            Require(Same(EvaluateRawBezierCurve(points,x),expected),"scalar Bezier output changed");
            Require(Same(prepared.Evaluate(x),expected),"prepared Bezier output changed");
        }
    }
}
void SceneTables() {
    SceneTone direct; direct.curve={{0,0,{1},{1}},{.3f,.2f,{1},{1}},{.6f,.75f,{1},{1}},{1,1,{1},{1}}};
    auto manual=direct; manual.curve[1].outgoing={1.f,true,.05f,.3f};
    manual.curve[2].incoming={1.2f,true,-.15f,-.2f};
    SceneTone legacy; legacy.direct=false;legacy.anchorOffsetEv=.3f;legacy.contrast=1.8f;
    legacy.outerProtection=.8f;legacy.points[2].contrast=.5f;
    auto disabled=manual;disabled.enabled=false;
    for (const auto& input:{direct,manual,legacy,disabled}) {
        const auto tone=SanitizeSceneTone(input);
        for (int count:{4096,4097}) {
            const auto table=BuildSceneToneLut(tone,count);
            Require(table.size()==static_cast<std::size_t>(count),"scene table size changed");
            for (int i=0;i<count;++i) {
                const float ev=-40.f+80.f*i/(count-1);
                Require(Same(table[i],EvaluateSceneToneEv(tone,ev)),"scene-tone table or HDR tail changed");
            }
        }
    }
}
void LocalEvPreparation() {
    using namespace Stack::Renderer;
    auto recipe=SanitizeLocalRangeRecipe(DefaultLocalRangeRecipe());
    recipe.enabled=true;recipe.smoothness=0;
    auto prepared=PrepareLocalEvCurveUniforms(recipe);
    Require(!prepared.curveActive && !prepared.lightActive && prepared.globalOffsetEv==0,
        "zero Local EV mapping must skip the inverse curve");
    Require(!LocalEvNeedsLightAnalysis(recipe,prepared),"unused histogram/guide must be skipped");
    recipe.light.amounts[2]=20;
    prepared=PrepareLocalEvCurveUniforms(recipe);
    Require(prepared.lightActive && LocalEvNeedsLightAnalysis(recipe,prepared),
        "automatic active tonal bands need their measured range");
    recipe.light.automatic=false;
    Require(!LocalEvNeedsLightAnalysis(recipe,prepared),"manual tonal bands do not need a histogram");
    recipe.smoothness=.5f;
    Require(LocalEvNeedsLightAnalysis(recipe,prepared),"smoothing needs the input guide");
    recipe.points[0].outgoing={1.f,true,.1f,.15f};
    prepared=PrepareLocalEvCurveUniforms(recipe);
    Require(prepared.curveActive,"manual handles can bend zero anchors");
    const auto bezier=RawLocalRangeBezierPoints(recipe);
    const float offset=LocalEvGlobalOffsetEv(recipe),span=std::max(.1f,recipe.maxEv-recipe.minEv);
    Require(Same(prepared.globalOffsetEv,offset),"active Local EV offset changed");
    for (int i=0;i<prepared.pointCount;++i) {
        Require(Same(prepared.points[i][0],recipe.points[i].ev) &&
            Same(prepared.points[i][1],recipe.points[i].deltaEv-offset),"Local EV point uniform changed");
        if (i+1==prepared.pointCount) break;
        const auto s=BuildRawBezierSegment(bezier,static_cast<std::size_t>(i));
        const std::array<float,4> expected{recipe.minEv+s.leftHandleX*span,-4.f+s.leftHandleY*8.f-offset,
            recipe.minEv+s.rightHandleX*span,-4.f+s.rightHandleY*8.f-offset};
        Require(prepared.handles[i]==expected,"Local EV handle uniform changed");
    }
}
}

int main() {
    try { PreparedCurves();SceneTables();LocalEvPreparation(); }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
    std::cout<<"Prepared tonal curves preserve scalar samples, scene/HDR tails and Local EV analysis demand.\n";
    return 0;
}
