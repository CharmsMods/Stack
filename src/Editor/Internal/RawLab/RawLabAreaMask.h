#pragma once
#include "Editor/EditorModuleTypes.h"

namespace Stack::Editor::RawLabInternal {
void RememberRawLabAreaGuide(EditorModuleTypes::RawZoneAreaUiState& ui,
    const RawDevelopmentGraphScopeReadback& scope);
std::shared_ptr<const RawRecipe::ImageGuide> RawLabAreaGuide(
    EditorModuleTypes::RawZoneAreaUiState& ui,const RawRecipe::RawDevelopmentRecipe& recipe,
    const RawDevelopmentGraphScopeReadback& scope);
bool RawLabAreaGuideReady(const RawRecipe::RawDevelopmentRecipe& recipe,
    const RawDevelopmentGraphScopeReadback& scope);
std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview> ResolveRawLabAreaMask(
    EditorModuleTypes::RawZoneAreaUiState& ui,const RawRecipe::RawZoneArea& area,
    const RawRecipe::RawDevelopmentRecipe& recipe,const RawDevelopmentGraphScopeReadback& scope,
    const Async::ActivityMetadata& activity = {});
float SampleRawLabAreaMask(const RawRecipe::RawZoneAreaMaskPreview& mask,
    float sourceU,float sourceV,const RawRecipe::RawCropRotationRecipe& transform);
}
