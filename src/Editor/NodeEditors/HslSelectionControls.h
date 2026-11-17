#pragma once
#include "Animation/ScalarChannel.h"
#include "Raw/HslSelectionTypes.h"
#include <imgui.h>
namespace Stack::Editor {
bool HslSegmented(const char* id,const char* const* labels,int count,int& selected,Animation::ScalarChannel& motion,bool allowOff=false);
bool HslSelectionRow(const RawRecipe::HslSelection& selection,bool selected,bool& toggle);
}
