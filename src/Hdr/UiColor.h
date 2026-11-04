#pragma once
#include "Hdr/Color.h"
#include "imgui.h"
namespace Stack::Hdr::Ui {
inline ImVec4 TargetColor(ImVec4 color,bool linear) {
    if(linear) {
        color.x=Linear(color.x);
        color.y=Linear(color.y);
        color.z=Linear(color.z);
    }
    return color;
}
}
