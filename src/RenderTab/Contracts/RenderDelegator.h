#pragma once

#include "SceneCompiler.h"

#include "RenderTab/PathTrace/PathTraceRenderer.h"
#include "RenderTab/Runtime/RenderRasterPreviewRenderer.h"

#include <string>
#include <vector>

namespace RenderContracts {

class AccumulationManager;

class RenderDelegator {
public:
    enum class PathTraceTarget {
        Viewport = 0,
        CameraPreview,
        FinalRender
    };

    RenderDelegator() = default;
    ~RenderDelegator();

    bool RenderViewport(
        const CompiledScene& compiledScene,
        const RenderFoundation::Settings& settings,
        AccumulationManager& accumulationManager,
        int width,
        int height,
        int selectedObjectId,
        unsigned int& outTextureId,
        std::string& errorMessage);

    bool CaptureViewportPixels(
        const CompiledScene& compiledScene,
        const RenderFoundation::Settings& settings,
        const AccumulationManager& accumulationManager,
        int width,
        int height,
        int selectedObjectId,
        std::vector<unsigned char>& outPixels,
        std::string& errorMessage);
    bool RenderPathTraceTarget(
        PathTraceTarget target,
        const CompiledScene& compiledScene,
        const RenderFoundation::Settings& settings,
        AccumulationManager& accumulationManager,
        int width,
        int height,
        bool allowNewSamples,
        unsigned int& outTextureId,
        std::string& errorMessage);
    bool CapturePathTraceTargetPixels(
        PathTraceTarget target,
        int width,
        int height,
        const AccumulationManager& accumulationManager,
        std::vector<unsigned char>& outPixels,
        std::string& errorMessage);
    bool CapturePathTraceTargetLinearPixels(
        PathTraceTarget target,
        const RenderFoundation::Settings& settings,
        int width,
        int height,
        const AccumulationManager& accumulationManager,
        RenderPathTrace::LinearCaptureMode captureMode,
        std::vector<float>& outPixels,
        std::string& errorMessage);
    const RenderPathTrace::PathTraceDebugReadback& GetPathTraceDebugReadback() const {
        return m_ViewportPathTraceRenderer.GetDebugReadback();
    }
    double GetRasterPreviewUploadMilliseconds() const {
        return m_RasterPreviewRenderer.GetLastUploadMilliseconds();
    }

    void Shutdown();

private:
    bool CaptureTexturePixels(
        unsigned int textureId,
        int width,
        int height,
        std::vector<unsigned char>& outPixels,
        std::string& errorMessage) const;
    RenderPathTrace::PathTraceRenderer& ResolvePathTraceRenderer(PathTraceTarget target);
    const RenderPathTrace::PathTraceRenderer& ResolvePathTraceRenderer(PathTraceTarget target) const;

    RenderRasterPreviewRenderer m_RasterPreviewRenderer;
    RenderPathTrace::PathTraceRenderer m_ViewportPathTraceRenderer;
    RenderPathTrace::PathTraceRenderer m_CameraPathTraceRenderer;
    RenderPathTrace::PathTraceRenderer m_FinalPathTraceRenderer;
};

} // namespace RenderContracts
