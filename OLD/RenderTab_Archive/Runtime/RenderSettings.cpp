#include "RenderSettings.h"

#include <algorithm>
#include <cctype>

RenderSettings::RenderSettings()
    : m_ResolutionX(1920)
    , m_ResolutionY(1080)
    , m_PreviewSampleTarget(64)
    , m_AccumulationEnabled(true)
    , m_IntegratorMode(RenderIntegratorMode::RasterPreview)
    , m_MaxBounceCount(4)
    , m_DisplayMode(RenderDisplayMode::Color)
    , m_TonemapMode(RenderTonemapMode::AcesFilm)
    , m_GizmoMode(RenderGizmoMode::Translate)
    , m_TransformSpace(RenderTransformSpace::World)
    , m_DebugViewMode(RenderDebugViewMode::Disabled)
    , m_UseBvhTraversal(true)
    , m_Revision(1)
    , m_LastChangeReason("Initial render settings.") {
}

bool RenderSettings::SetResolution(int width, int height) {
    const int clampedWidth = std::clamp(width, 64, 8192);
    const int clampedHeight = std::clamp(height, 64, 8192);
    if (m_ResolutionX == clampedWidth && m_ResolutionY == clampedHeight) {
        return false;
    }

    m_ResolutionX = clampedWidth;
    m_ResolutionY = clampedHeight;
    Touch("Render resolution changed.");
    return true;
}

bool RenderSettings::SetPreviewSampleTarget(int target) {
    const int clampedTarget = std::clamp(target, 1, 4096);
    if (m_PreviewSampleTarget == clampedTarget) {
        return false;
    }

    m_PreviewSampleTarget = clampedTarget;
    return true;
}

bool RenderSettings::SetAccumulationEnabled(bool enabled) {
    if (m_AccumulationEnabled == enabled) {
        return false;
    }

    m_AccumulationEnabled = enabled;
    Touch("Accumulation mode changed.");
    return true;
}

bool RenderSettings::SetIntegratorMode(RenderIntegratorMode mode) {
    if (m_IntegratorMode == mode) {
        return false;
    }

    m_IntegratorMode = mode;
    if (m_IntegratorMode != RenderIntegratorMode::DebugPreview &&
        m_DebugViewMode != RenderDebugViewMode::Disabled) {
        m_DebugViewMode = RenderDebugViewMode::Disabled;
    }
    switch (m_IntegratorMode) {
    case RenderIntegratorMode::RasterPreview:
        Touch("Integrator changed to raster preview.");
        break;
    case RenderIntegratorMode::PathTracePreview:
        Touch("Integrator changed to path-trace preview.");
        break;
    case RenderIntegratorMode::DebugPreview:
        Touch("Integrator changed to debug preview.");
        break;
    }
    return true;
}

bool RenderSettings::SetMaxBounceCount(int count) {
    const int clampedCount = std::clamp(count, 1, 12);
    if (m_MaxBounceCount == clampedCount) {
        return false;
    }

    m_MaxBounceCount = clampedCount;
    Touch("Path-trace bounce limit changed.");
    return true;
}

bool RenderSettings::SetDisplayMode(RenderDisplayMode mode) {
    if (m_DisplayMode == mode) {
        return false;
    }

    m_DisplayMode = mode;
    Touch("Display mode changed.");
    return true;
}

bool RenderSettings::SetTonemapMode(RenderTonemapMode mode) {
    if (m_TonemapMode == mode) {
        return false;
    }

    m_TonemapMode = mode;
    Touch("Preview tonemap mode changed.");
    return true;
}

bool RenderSettings::SetGizmoMode(RenderGizmoMode mode) {
    if (m_GizmoMode == mode) {
        return false;
    }

    m_GizmoMode = mode;
    Touch("Viewport gizmo mode changed.");
    return true;
}

bool RenderSettings::SetTransformSpace(RenderTransformSpace mode) {
    if (m_TransformSpace == mode) {
        return false;
    }

    m_TransformSpace = mode;
    Touch("Viewport transform space changed.");
    return true;
}

bool RenderSettings::SetDebugViewMode(RenderDebugViewMode mode) {
    if (m_DebugViewMode == mode) {
        return false;
    }

    m_DebugViewMode = mode;
    if (m_DebugViewMode != RenderDebugViewMode::Disabled &&
        m_IntegratorMode != RenderIntegratorMode::DebugPreview) {
        m_IntegratorMode = RenderIntegratorMode::DebugPreview;
    }
    Touch("Debug view mode changed.");
    return true;
}

bool RenderSettings::SetFinalRenderResolution(int width, int height) {
    const int clampedWidth = std::clamp(width, 64, 8192);
    const int clampedHeight = std::clamp(height, 64, 8192);
    if (m_FinalRenderSettings.resolutionX == clampedWidth &&
        m_FinalRenderSettings.resolutionY == clampedHeight) {
        return false;
    }

    m_FinalRenderSettings.resolutionX = clampedWidth;
    m_FinalRenderSettings.resolutionY = clampedHeight;
    Touch("Final render resolution changed.");
    return true;
}

bool RenderSettings::SetFinalRenderSampleTarget(int target) {
    const int clampedTarget = std::clamp(target, 1, 8192);
    if (m_FinalRenderSettings.sampleTarget == clampedTarget) {
        return false;
    }

    m_FinalRenderSettings.sampleTarget = clampedTarget;
    Touch("Final render sample target changed.");
    return true;
}

bool RenderSettings::SetFinalRenderMaxBounceCount(int count) {
    const int clampedCount = std::clamp(count, 1, 16);
    if (m_FinalRenderSettings.maxBounceCount == clampedCount) {
        return false;
    }

    m_FinalRenderSettings.maxBounceCount = clampedCount;
    Touch("Final render bounce limit changed.");
    return true;
}

bool RenderSettings::SetFinalRenderOutputName(const std::string& outputName) {
    std::string trimmed = outputName;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), trimmed.end());
    if (trimmed.empty()) {
        trimmed = "Final Render";
    }

    if (m_FinalRenderSettings.outputName == trimmed) {
        return false;
    }

    m_FinalRenderSettings.outputName = trimmed;
    Touch("Final render output name changed.");
    return true;
}

bool RenderSettings::SetBvhTraversalEnabled(bool enabled) {
    if (m_UseBvhTraversal == enabled) {
        return false;
    }

    m_UseBvhTraversal = enabled;
    Touch(enabled ? "BVH traversal enabled." : "BVH traversal disabled.");
    return true;
}

void RenderSettings::Touch(const std::string& reason) {
    ++m_Revision;
    m_LastChangeReason = reason;
}
