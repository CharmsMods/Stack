#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>

namespace Stack::Renderer::RawDevelopmentCache {

enum class Stage {
    RawBase,
    NeutralPlacement,
    RawPlacement,
    PostLocalExposure,
    PostLocalRange,
    PostFinishTone
};

inline std::size_t NonzeroHash(const nlohmann::json& value) {
    const std::size_t fingerprint =
        std::hash<std::string>{}(value.dump());
    return fingerprint == 0 ? 1 : fingerprint;
}

inline std::string BuildSourceDataIdentity(
    const Stack::RawRecipe::RawSourceReference& source) {
    return nlohmann::json{
        { "sourcePath", source.sourcePath },
        { "fingerprint", source.fingerprint },
        { "fileSizeBytes", source.fileSizeBytes },
        { "modifiedTimeTicks", source.modifiedTimeTicks }
    }.dump();
}

inline std::size_t BuildStageFingerprint(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    int previewMaxDimension,
    Stage stage) {
    Stack::RawRecipe::RawDevelopmentRecipe stageRecipe = recipe;

    // These fields identify the project/source in the UI but do not change
    // decoded pixels. Keeping them out avoids throwing away exact GPU stages
    // when a source is renamed or relinked to the same content identity.
    stageRecipe.source.relativePathKey.clear();
    stageRecipe.source.displayName.clear();
    stageRecipe.previewOutput = {};
    stageRecipe.stageOrder = Stack::RawRecipe::DefaultStageOrder();
    stageRecipe.toneCurve = {};

    if (!stageRecipe.rgbDenoise.enabled) {
        stageRecipe.rgbDenoise = {};
    }
    if (!Stack::RawRecipe::IsLocalExposureEnabled(recipe)) {
        stageRecipe.localExposure = {};
    }
    if (!Stack::RawRecipe::IsLocalRangeEnabled(recipe)) {
        stageRecipe.localRange =
            Stack::RawRecipe::DefaultLocalRangeRecipe();
    }

    if (stage == Stage::RawBase) {
        stageRecipe.rgbDenoise = {};
    }
    if (stage == Stage::RawBase || stage == Stage::NeutralPlacement) {
        stageRecipe.preToneExposureEv = 0.0f;
    }
    if (stage == Stage::RawBase ||
        stage == Stage::NeutralPlacement ||
        stage == Stage::RawPlacement) {
        stageRecipe.localExposure = {};
    }
    if (stage == Stage::RawBase ||
        stage == Stage::NeutralPlacement ||
        stage == Stage::RawPlacement ||
        stage == Stage::PostLocalExposure) {
        stageRecipe.localRange =
            Stack::RawRecipe::DefaultLocalRangeRecipe();
    } else {
        // This is a presentation preference. The Local Range image shader
        // never consumes it.
        stageRecipe.localRange.maskPreviewMode = "none";
    }
    if (stage != Stage::PostFinishTone) {
        stageRecipe.finishTone.layerJson =
            Stack::RawRecipe::DefaultFinishToneJson();
    }
    stageRecipe.viewTransform.layerJson =
        Stack::RawRecipe::DefaultViewTransformJson();

    nlohmann::json fingerprintJson =
        Stack::RawRecipe::SerializeRecipe(stageRecipe);
    fingerprintJson["rawStageCache"] = {
        { "version", 2 },
        { "stage", static_cast<int>(stage) },
        { "previewMaxDimension", std::max(0, previewMaxDimension) }
    };
    return NonzeroHash(fingerprintJson);
}

template <typename T>
inline void HashSelectionValue(std::size_t& fingerprint, const T& value) {
    fingerprint ^= std::hash<T>{}(value) +
        static_cast<std::size_t>(0x9e3779b97f4a7c15ull) +
        (fingerprint << 6u) +
        (fingerprint >> 2u);
}

inline std::size_t BuildLocalRangeSelectionFingerprint(
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    Raw::RawWorkingSpace workingSpace,
    std::size_t inputStageFingerprint,
    int maximumSelectionDimension) {
    std::size_t fingerprint =
        static_cast<std::size_t>(1469598103934665603ull);
    HashSelectionValue(fingerprint, inputStageFingerprint);
    HashSelectionValue(fingerprint, static_cast<int>(workingSpace));
    HashSelectionValue(
        fingerprint,
        std::clamp(maximumSelectionDimension, 64, 1536));
    HashSelectionValue(fingerprint, localRange.middleGrey);
    for (const Stack::RawRecipe::RawLocalRangeTargetZone& zone :
         localRange.targetZones) {
        HashSelectionValue(fingerprint, zone.id);
        HashSelectionValue(fingerprint, zone.enabled);
        HashSelectionValue(fingerprint, zone.centerEv);
        HashSelectionValue(fingerprint, zone.coreHalfWidthEv);
        HashSelectionValue(fingerprint, zone.featherEv);
        HashSelectionValue(fingerprint, static_cast<int>(zone.scope));
        HashSelectionValue(fingerprint, zone.colorEnabled);
        HashSelectionValue(fingerprint, zone.targetUPrime);
        HashSelectionValue(fingerprint, zone.targetVPrime);
        HashSelectionValue(fingerprint, zone.targetChroma);
        HashSelectionValue(fingerprint, zone.colorRadius);
        HashSelectionValue(fingerprint, zone.colorFeather);
        for (const Stack::RawRecipe::RawLocalRangeTargetSeed& seed :
             zone.seeds) {
            HashSelectionValue(fingerprint, seed.sourceU);
            HashSelectionValue(fingerprint, seed.sourceV);
        }
    }
    return fingerprint == 0 ? 1 : fingerprint;
}

} // namespace Stack::Renderer::RawDevelopmentCache
