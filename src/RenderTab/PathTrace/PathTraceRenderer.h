#pragma once

#include "PathTraceTypes.h"
#include "Renderer/GLLoader.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace RenderContracts {
class AccumulationManager;
}

namespace RenderPathTrace {

struct PathTraceBuffers {
    std::array<unsigned int, 2> hdrAccumTextures {};
    std::array<unsigned int, 2> displayTextures {};
    std::array<unsigned int, 2> framebuffers {};
    std::array<GLsync, 2> completionFences {};
    unsigned int materialBuffer = 0;
    unsigned int materialLayerBuffer = 0;
    unsigned int sphereBuffer = 0;
    unsigned int triangleBuffer = 0;
    unsigned int primitiveRefBuffer = 0;
    unsigned int bvhBuffer = 0;
    unsigned int lightBuffer = 0;
    unsigned int environmentBuffer = 0;
    unsigned int rayStateBuffer = 0;
    unsigned int hitStateBuffer = 0;
    unsigned int activeQueueBuffer = 0;
    unsigned int nextQueueBuffer = 0;
    unsigned int shadowQueueBuffer = 0;
    unsigned int queueCountBuffer = 0;
    unsigned int debugLogBuffer = 0;
    int width = 0;
    int height = 0;
};

class PathTraceRenderer {
public:
    PathTraceRenderer() = default;
    ~PathTraceRenderer();

    bool RenderFrame(
        const CompiledPathTraceScene& scene,
        const RenderFoundation::Settings& settings,
        RenderContracts::AccumulationManager& accumulationManager,
        int width,
        int height,
        unsigned int& outTextureId,
        std::string& errorMessage);

    bool CaptureVisiblePixels(
        int width,
        int height,
        const RenderContracts::AccumulationManager& accumulationManager,
        std::vector<unsigned char>& outPixels,
        std::string& errorMessage) const;
    const PathTraceDebugReadback& GetDebugReadback() const { return m_DebugReadback; }

    void Shutdown();

private:
    bool InitializePrograms(std::string& errorMessage);
    bool EnsureBuffers(int width, int height, std::string& errorMessage);
    bool UploadScene(const CompiledPathTraceScene& scene, std::string& errorMessage);
    bool DispatchSample(
        const CompiledPathTraceScene& scene,
        const RenderFoundation::Settings& settings,
        const RenderContracts::AccumulationManager& accumulationManager,
        int width,
        int height,
        std::string& errorMessage);
    void ReadDebugLog(
        const RenderFoundation::Settings& settings,
        int width,
        int height);
    bool PollCompletedFence(
        const RenderFoundation::Settings& settings,
        RenderContracts::AccumulationManager& accumulationManager,
        std::string& errorMessage);
    void ReleaseSceneBuffers();
    void ReleaseTextures();
    void ReleasePrograms();
    bool CaptureTexturePixels(
        unsigned int textureId,
        int width,
        int height,
        std::vector<unsigned char>& outPixels,
        std::string& errorMessage) const;

private:
    PathTraceBuffers m_Buffers {};
    unsigned int m_GenerateProgram = 0;
    unsigned int m_IntersectProgram = 0;
    unsigned int m_ShadeProgram = 0;
    unsigned int m_ShadowProgram = 0;
    unsigned int m_AccumulateProgram = 0;
    std::uint64_t m_UploadedSceneHash = 0;
    bool m_SubmissionInFlight = false;
    int m_InFlightSlotIndex = -1;
    int m_InFlightWidth = 0;
    int m_InFlightHeight = 0;
    PathTraceDebugReadback m_DebugReadback {};
    std::string m_LastError;
};

} // namespace RenderPathTrace
