#pragma once

#include <cstdint>

enum class RawPreviewSkipReason : std::uint8_t {
    None, InputCoalesced, OverloadDeadline, PriorityChange, Canceled, ObsoleteView
};

enum class RawRenderPurpose : std::uint8_t {
    InteractivePresentation = 0,
    InteractiveSample,
    ViewportRefinement,
    ExplicitInspection,
    ExplicitExport,
    AnalysisScopes,
    DenoiseCompletion,
    Thumbnail,
    CachePrewarm,
    ViewportCalibration,
    ViewportOverview,
};

constexpr int RawRenderPurposePriority(RawRenderPurpose purpose) {
    switch (purpose) {
        case RawRenderPurpose::InteractivePresentation: return 0;
        case RawRenderPurpose::InteractiveSample: return 1;
        case RawRenderPurpose::ExplicitExport: return 2;
        case RawRenderPurpose::ExplicitInspection: return 3;
        case RawRenderPurpose::ViewportRefinement: return 3;
        case RawRenderPurpose::DenoiseCompletion: return 4;
        case RawRenderPurpose::AnalysisScopes: return 5;
        case RawRenderPurpose::Thumbnail: return 6;
        case RawRenderPurpose::CachePrewarm: return 7;
        case RawRenderPurpose::ViewportCalibration: return 8;
        case RawRenderPurpose::ViewportOverview: return 6;
    }
    return 9;
}

constexpr bool RawRenderPurposeMayPublishPresentation(
    RawRenderPurpose purpose) {
    return purpose == RawRenderPurpose::InteractivePresentation ||
        purpose == RawRenderPurpose::ViewportRefinement ||
        purpose == RawRenderPurpose::DenoiseCompletion ||
        purpose == RawRenderPurpose::ExplicitInspection;
}

constexpr const char* RawRenderPurposeName(RawRenderPurpose purpose) {
    switch (purpose) {
        case RawRenderPurpose::InteractivePresentation:
            return "interactive-presentation";
        case RawRenderPurpose::InteractiveSample:
            return "interactive-sample";
        case RawRenderPurpose::ViewportRefinement:
            return "viewport-refinement";
        case RawRenderPurpose::ExplicitInspection:
            return "explicit-inspection";
        case RawRenderPurpose::ExplicitExport:
            return "explicit-export";
        case RawRenderPurpose::AnalysisScopes:
            return "analysis-scopes";
        case RawRenderPurpose::DenoiseCompletion:
            return "denoise-completion";
        case RawRenderPurpose::Thumbnail:
            return "thumbnail";
        case RawRenderPurpose::CachePrewarm:
            return "cache-prewarm";
        case RawRenderPurpose::ViewportCalibration:
            return "viewport-calibration";
        case RawRenderPurpose::ViewportOverview:
            return "viewport-overview";
    }
    return "unknown";
}
