#pragma once
#include "Raw/HslSelections.h"
#include "Renderer/ScopedGLObjects.h"
#include <imgui_internal.h>

struct RawDevelopmentGraphScopeReadback;

namespace Stack::Editor {
struct ColorGradeHover {
    struct Point {float ev=0,hue=0; bool neutral=false;};
    int mapFrame=-10,photoFrame=-10;
    bool overMap=false;
    ImRect mapRect;
    Point query,photo;
    RawRecipe::RawCropRotationRecipe crop;
    Raw::RawWorkingSpace space=Raw::RawWorkingSpace::LinearRec2020D65;
    void DrawImage(const RawDevelopmentGraphScopeReadback&,std::size_t identity,const ImRect&,bool pointerAllowed);
private:
    std::size_t cachedInput=0;
    Raw::RawWorkingSpace cachedSpace{};
    int width=0,height=0;
    Point lastQuery;
    std::vector<Point> samples;
    std::vector<unsigned char> pixels;
    Renderer::ScopedGLTexture overlay;
};
}
