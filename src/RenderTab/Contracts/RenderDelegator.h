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
    const RenderPathTrace::PathTraceDebugReadback& GetPathTraceDebugReadback() const {
        return m_PathTraceRenderer.GetDebugReadback();
    }

    void Shutdown();

private:
    bool CaptureTexturePixels(
        unsigned int textureId,
        int width,
        int height,
        std::vector<unsigned char>& outPixels,
        std::string& errorMessage) const;

    RenderRasterPreviewRenderer m_RasterPreviewRenderer;
    RenderPathTrace::PathTraceRenderer m_PathTraceRenderer;
};

} // namespace RenderContracts
