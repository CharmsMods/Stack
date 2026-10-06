#include "Editor/Internal/RawLab/RawLabAreaMask.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include <algorithm>

namespace Stack::Editor::RawLabInternal {
bool RawLabAreaGuideReady(const RawRecipe::RawDevelopmentRecipe& recipe,const RawDevelopmentGraphScopeReadback& scope) {
    return scope.valid && scope.stage==RawDevelopmentGraphScopeStage::LocalRangeInput &&
        scope.zoneGuide && scope.zoneGuide->Valid() && (scope.graphInputFingerprint || scope.zoneGuide->recipeFingerprint==
        Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0).calibratedNeutral);
}

void RememberRawLabAreaGuide(EditorModuleTypes::RawZoneAreaUiState& ui,
    const RawDevelopmentGraphScopeReadback& scope) {
    if (!scope.zoneGuide || !scope.zoneGuide->Valid()) return;
    // An adaptive render may temporarily replace/clear the graph scope. Keep
    // the neutral guide independently and hold it fixed throughout a stroke.
    if (!ui.neutralGuide || !ui.active ||
        ui.neutralGuide->recipeFingerprint!=scope.zoneGuide->recipeFingerprint)
        ui.neutralGuide=scope.zoneGuide;
}

std::shared_ptr<const RawRecipe::ImageGuide> RawLabAreaGuide(
    EditorModuleTypes::RawZoneAreaUiState& ui,const RawRecipe::RawDevelopmentRecipe& recipe,
    const RawDevelopmentGraphScopeReadback& scope) {
    if (scope.graphInputFingerprint) {
        if (RawLabAreaGuideReady(recipe,scope)) RememberRawLabAreaGuide(ui,scope);
        else if (!ui.active) ui.neutralGuide.reset();
        return ui.neutralGuide;
    }
    const auto expected=Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0).calibratedNeutral;
    if (scope.zoneGuide && scope.zoneGuide->recipeFingerprint==expected) RememberRawLabAreaGuide(ui,scope);
    if (ui.neutralGuide && ui.neutralGuide->recipeFingerprint!=expected) ui.neutralGuide.reset();
    return ui.neutralGuide;
}

std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview> ResolveRawLabAreaMask(
    EditorModuleTypes::RawZoneAreaUiState& ui,const RawRecipe::RawZoneArea& area,
    const RawRecipe::RawDevelopmentRecipe& recipe,const RawDevelopmentGraphScopeReadback& scope,
    const Async::ActivityMetadata& activity) {
    const auto guide=RawLabAreaGuide(ui,recipe,scope);
    const auto referenceKey=scope.graphInputFingerprint ? scope.graphInputFingerprint
        : Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0).calibratedNeutral;
    const auto fingerprint=RawRecipe::ZoneAreaMaskFingerprint(area);
    auto& cache=ui.masks[area.id];
    if (cache.key!=referenceKey) {cache={};cache.key=referenceKey;}
    // Current renderer coverage is authoritative. Older results cannot replace
    // a more recent mouse sample or a mask restored by undo.
    if (guide && RawLabAreaGuideReady(recipe,scope)) for (const auto& stats:scope.zoneAreas)
        if (stats.areaId==area.id && stats.maskFingerprint==fingerprint && stats.maskPreview) {
            if (!cache.fullResolution || stats.fullResolution || !cache.preview || cache.preview->maskFingerprint!=fingerprint) {
                cache.preview=stats.maskPreview;cache.fullResolution=stats.fullResolution;
                cache.authoritative=true;cache.provisional=false;cache.guide=guide;
            }
            ui.maskPending=false;
            return cache.preview;
        }
    if (cache.preview && cache.preview->maskFingerprint==fingerprint) {
        // Provisional geometry must be refined once the neutral guide arrives.
        if (cache.authoritative || (!cache.provisional && cache.guide==guide)) {ui.maskPending=false;return cache.preview;}
    }
    if (!ui.selectedId.empty() && area.id!=ui.selectedId) return cache.preview;
    if (!ui.maskWorker) ui.maskWorker=std::make_shared<Stack::Editor::RawZoneAreaPreview>();
    ui.maskWorker->Request(area,recipe.cropRotation,guide,referenceKey,activity);
    const auto result=ui.maskWorker->Latest();
    if (result.mask && result.referenceKey==referenceKey) {
        cache.preview=result.mask;cache.fullResolution=false;
        cache.authoritative=false;cache.provisional=result.provisional;cache.guide=guide;
    } else if (cache.preview && cache.preview->maskFingerprint!=fingerprint && !ui.active) {
        cache.preview.reset();
    }
    ui.maskPending=ui.maskWorker->Pending() || (RawRecipe::ZoneAreaUsesGuidance(area) && !guide);
    return cache.preview;
}

float SampleRawLabAreaMask(const RawRecipe::RawZoneAreaMaskPreview& mask,float u,float v,
    const RawRecipe::RawCropRotationRecipe& transform) {
    if (mask.width<=0 || mask.height<=0 || mask.coverage.size()!=std::size_t(mask.width)*mask.height) return 0;
    const auto p=RawRecipe::ZoneAreaDisplayPoint(u,v,transform,false);
    const int x=std::clamp(int(p.u*mask.width),0,mask.width-1);
    const int y=std::clamp(int(p.v*mask.height),0,mask.height-1);
    return mask.coverage[std::size_t(mask.height-1-y)*mask.width+x];
}
}
