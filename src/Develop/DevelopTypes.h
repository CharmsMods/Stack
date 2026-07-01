#pragma once

#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

#include <string>
#include <vector>

namespace Stack::Develop {

enum class RawDevelopUiMode {
    Auto = 0,
    Manual = 1
};

enum class AutoIntent {
    NaturalFinished = 0,
    CleanBase,
    FlatEditingBase,
    BrightNatural,
    DarkNatural,
    PunchyHighContrast,
    MaximumRangeDetail
};

inline const char* AutoIntentStableString(AutoIntent intent) {
    switch (intent) {
        case AutoIntent::CleanBase: return "CleanBase";
        case AutoIntent::FlatEditingBase: return "FlatEditingBase";
        case AutoIntent::BrightNatural: return "BrightNatural";
        case AutoIntent::DarkNatural: return "DarkNatural";
        case AutoIntent::PunchyHighContrast: return "PunchyHighContrast";
        case AutoIntent::MaximumRangeDetail: return "MaximumRangeDetail";
        case AutoIntent::NaturalFinished:
        default:
            return "NaturalFinished";
    }
}

inline const char* AutoIntentLabel(AutoIntent intent) {
    switch (intent) {
        case AutoIntent::CleanBase: return "Clean Base";
        case AutoIntent::FlatEditingBase: return "Flat Editing Base";
        case AutoIntent::BrightNatural: return "Bright Natural";
        case AutoIntent::DarkNatural: return "Dark Natural";
        case AutoIntent::PunchyHighContrast: return "Punchy / High Contrast";
        case AutoIntent::MaximumRangeDetail: return "Maximum Range / Detail";
        case AutoIntent::NaturalFinished:
        default:
            return "Natural Finished";
    }
}

inline const char* AutoIntentDescription(AutoIntent intent) {
    switch (intent) {
        case AutoIntent::CleanBase:
            return "Technically clean, conservative starting point for later editing.";
        case AutoIntent::FlatEditingBase:
            return "Brings useful mids/detail into visible range and lowers final contrast so manual editing is easier.";
        case AutoIntent::BrightNatural:
            return "Realistic but biased toward a brighter rendered result.";
        case AutoIntent::DarkNatural:
            return "Preserves darker scene mood and avoids forcing low-key images into gray mids.";
        case AutoIntent::PunchyHighContrast:
            return "Stronger visual separation and contrast, while still avoiding fake HDR.";
        case AutoIntent::MaximumRangeDetail:
            return "Prioritizes fitting more highlight/shadow information into visible range without claiming to recover missing clipped data.";
        case AutoIntent::NaturalFinished:
        default:
            return "Balanced realistic output intended to look usable immediately.";
    }
}

inline AutoIntent AutoIntentFromStableString(const std::string& value) {
    if (value == "CleanBase") return AutoIntent::CleanBase;
    if (value == "FlatEditingBase") return AutoIntent::FlatEditingBase;
    if (value == "BrightNatural") return AutoIntent::BrightNatural;
    if (value == "DarkNatural") return AutoIntent::DarkNatural;
    if (value == "PunchyHighContrast") return AutoIntent::PunchyHighContrast;
    if (value == "MaximumRangeDetail") return AutoIntent::MaximumRangeDetail;
    return AutoIntent::NaturalFinished;
}

struct AutoGuidance {
    AutoIntent intent = AutoIntent::NaturalFinished;
    float autoStrength = 0.78f;
    float exposureBias = 0.0f;
    float dynamicRange = 1.0f;
    float shadowLift = 0.0f;
    float highlightGuard = 0.0f;
    float highlightCharacter = 0.0f;
    float contrastBias = 0.0f;
    // Guide 05 user intent axes. Neutral keeps automatic weak subject/scene evidence in charge.
    float subjectSceneBias = 0.0f;      // -1 global scene integrity, +1 marked/likely subject priority
    float moodReadabilityBias = 0.0f;   // -1 preserve mood, +1 improve subject/readability
};

enum class SubjectImportanceMode {
    Important = 0,
    Reveal,
    Protect,
    PreserveMood,
    Ignore
};

inline const char* SubjectImportanceModeStableString(SubjectImportanceMode mode) {
    switch (mode) {
        case SubjectImportanceMode::Reveal: return "Reveal";
        case SubjectImportanceMode::Protect: return "Protect";
        case SubjectImportanceMode::PreserveMood: return "PreserveMood";
        case SubjectImportanceMode::Ignore: return "Ignore";
        case SubjectImportanceMode::Important:
        default:
            return "Important";
    }
}

inline const char* SubjectImportanceModeLabel(SubjectImportanceMode mode) {
    switch (mode) {
        case SubjectImportanceMode::Reveal: return "Reveal";
        case SubjectImportanceMode::Protect: return "Protect";
        case SubjectImportanceMode::PreserveMood: return "Preserve Mood";
        case SubjectImportanceMode::Ignore: return "Ignore / Low Priority";
        case SubjectImportanceMode::Important:
        default:
            return "Important";
    }
}

inline const char* SubjectImportanceModeDescription(SubjectImportanceMode mode) {
    switch (mode) {
        case SubjectImportanceMode::Reveal:
            return "Make this region more visible when quality allows.";
        case SubjectImportanceMode::Protect:
            return "Protect this region from clipping, smearing, over-compression, or heavy cleanup.";
        case SubjectImportanceMode::PreserveMood:
            return "Let this region keep darker or brighter scene mood instead of forcing neutral readability.";
        case SubjectImportanceMode::Ignore:
            return "Spend less exposure, range, or cleanup budget here.";
        case SubjectImportanceMode::Important:
        default:
            return "This region matters; Auto should weigh it more strongly without treating it as a hard mask.";
    }
}

inline SubjectImportanceMode SubjectImportanceModeFromStableString(const std::string& value) {
    if (value == "Reveal") return SubjectImportanceMode::Reveal;
    if (value == "Protect") return SubjectImportanceMode::Protect;
    if (value == "PreserveMood") return SubjectImportanceMode::PreserveMood;
    if (value == "Ignore") return SubjectImportanceMode::Ignore;
    return SubjectImportanceMode::Important;
}

struct SubjectImportanceRegion {
    int id = 0;
    SubjectImportanceMode mode = SubjectImportanceMode::Important;
    bool enabled = true;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float radiusX = 0.18f;
    float radiusY = 0.18f;
    float feather = 0.35f;
    float strength = 0.75f;
};

struct SubjectImportanceStrokePoint {
    float x = 0.5f;
    float y = 0.5f;
};

struct SubjectImportanceStroke {
    int id = 0;
    SubjectImportanceMode mode = SubjectImportanceMode::Important;
    bool enabled = true;
    bool subtract = false;
    float radius = 0.045f;
    float feather = 0.35f;
    float strength = 0.75f;
    std::vector<SubjectImportanceStrokePoint> points;
};

struct SubjectImportanceMap {
    int schemaVersion = 2;
    bool enabled = false;
    bool showOverlay = true;
    float overlayOpacity = 0.45f;
    bool showInterpretedMapOverlay = false;
    float interpretedMapOpacity = 0.32f;
    bool showRefinedMapOverlay = false;
    float refinedMapOpacity = 0.36f;
    bool brushEnabled = false;
    bool brushSubtract = false;
    SubjectImportanceMode brushMode = SubjectImportanceMode::Important;
    float brushRadius = 0.045f;
    float brushFeather = 0.35f;
    float brushStrength = 0.75f;
    int activeRegionId = 0;
    int activeStrokeId = 0;
    int nextRegionId = 1;
    int nextStrokeId = 1;
    std::vector<SubjectImportanceRegion> regions;
    std::vector<SubjectImportanceStroke> strokes;
};

struct RawDevelopPayload {
    Raw::RawDevelopSettings settings;
    bool scenePrepEnabled = true;
    Raw::RawDetailFusionSettings scenePrepSettings;
    bool integratedToneEnabled = true;
    nlohmann::json integratedToneLayerJson;
    AutoGuidance autoGuidance;
    SubjectImportanceMap subjectImportance;
    RawDevelopUiMode uiMode = RawDevelopUiMode::Auto;
};

} // namespace Stack::Develop
