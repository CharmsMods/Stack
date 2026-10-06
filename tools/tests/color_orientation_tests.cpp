#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Raw/Bracketing/Recipe.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckProjection() {
    using namespace Stack::Editor::RawLabInternal;
    const auto neutral=ProjectColorDisc(0,0);
    Require(neutral.x==0 && neutral.y==0,"Neutral must stay centered");
    float previous=0;
    for(float radius : {0.001f,0.02f,0.1f,0.3f,0.45f,1.0f,4.0f,20.0f}) {
        const auto projected=ProjectColorDisc(radius*0.6f,radius*0.8f);
        const float displayed=std::hypot(projected.x,projected.y);
        Require(displayed>previous && displayed<ColorDiscExtent,"HDR chroma must remain ordered inside the disc");
        Require(std::abs(projected.y/projected.x-4.0f/3.0f)<1e-5f,"Projection must preserve hue");
        const auto restored=UnprojectColorDisc(projected.x,projected.y);
        Require(std::hypot(restored.x-radius*0.6f,restored.y-radius*0.8f)<std::max(1e-6f,radius*1e-4f),
            "Picking must recover the original scene coordinate");
        previous=displayed;
    }
    Require(ProjectColorDisc(0.04f,0).x>0.10f,"Near-neutral colors need more screen separation");
    const auto a=ProjectColorDisc(-0.13f,0.08f);
    const auto b=ProjectColorDisc(0.13f,-0.08f);
    Require(std::abs(a.x+b.x)<1e-6f && std::abs(a.y+b.y)<1e-6f,"Opposite hues must be symmetric");
    const std::array<float,3> rgb={0.1f,0.5f,0.8f};
    Require(ColorDiscDisplayRgb(rgb,Raw::RawWorkingSpace::LinearSrgbD65)==rgb,"sRGB data must not be transformed twice");
    const auto white=ColorDiscDisplayRgb({1,1,1},Raw::RawWorkingSpace::LinearRec2020D65);
    for(float channel:white) Require(std::abs(channel-1)<1e-5f,"Rec.2020 neutral must remain neutral");
    const auto red=ColorDiscDisplayRgb({1,0,0},Raw::RawWorkingSpace::LinearRec2020D65);
    Require(red[0]>1.6f && red[1]<0 && red[2]<0,"Rec.2020 primaries require display conversion without changing scene values");
}
void CheckOrientationRecipe() {
    using namespace Raw::Bracketing;
    BracketingRecipe recipe;
    recipe.originFrameId="reference";
    Group group;group.id="exposure";group.name="Exposure";
    group.frames={{"reference",true,false,0},{"alternate",true,false,0},{"unused",false,false,0}};
    recipe.groups={group};recipe.knots=EqualCurves(1);
    std::string error;
    Require(Validate(recipe,error),"Fixture recipe must be valid");
    const auto originalIdentity=Identity(recipe);
    recipe.orientationOverrides={{0,6},{3,6}};
    Require(ResolveOrientation(recipe,0)==6 && ResolveOrientation(recipe,3)==6,
        "Different camera tags must resolve to the confirmed common orientation");
    Require(ResolveOrientation(recipe,8)==8,"Unspecified tags must retain their orientation");
    Require(Identity(recipe)!=originalIdentity,"Corrections must invalidate cached processing");
    const auto saved=Serialize(recipe);
    BracketingRecipe reopened;
    Require(Deserialize(saved,reopened,error),"Saved corrections must reopen");
    Require(reopened.orientationOverrides==recipe.orientationOverrides,"Orientation corrections must survive round trip");
    Require(ResolveStoredOrientation(saved,0)==ResolveOrientation(reopened,0),
        "Project compatibility and RAW preparation must use the same correction");
    Require(UsesStoredFrame(saved,"reference") && UsesStoredFrame(saved,"alternate"),"Active captures must be checked");
    Require(!UsesStoredFrame(saved,"unused") && !UsesStoredFrame(saved,"new-draft"),
        "Disabled and staged captures must not invalidate a published result");
    auto disabledOrigin=saved;disabledOrigin["groups"][0]["enabled"]=false;
    Require(UsesStoredFrame(disabledOrigin,"reference"),"The calibration origin is required even in a disabled group");
    auto legacy=saved;legacy["version"]=4;legacy.erase("orientationOverrides");
    Require(Deserialize(legacy,reopened,error) && reopened.orientationOverrides.empty(),"Old projects must load without adding corrections");
    Require(ResolveOrientation(reopened,0)==ResolveOrientation(reopened,1),"Upright and unspecified orientation must agree");
    auto invalid=saved;invalid["orientationOverrides"]["3"]=9;
    Require(!Deserialize(invalid,reopened,error),"Invalid orientations must be rejected");
    invalid=saved;invalid["orientationOverrides"]["3x"]=1;
    Require(!Deserialize(invalid,reopened,error),"Invalid source orientation keys must be rejected");
    invalid=saved;invalid["orientationOverrides"]["3"]=2.5;
    Require(!Deserialize(invalid,reopened,error),"Fractional orientation codes must be rejected");
}
}
int main() {
    try {CheckProjection();CheckOrientationRecipe();std::cout<<"Color projection and bracket orientation checks passed.\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
