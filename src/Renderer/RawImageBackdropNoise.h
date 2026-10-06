#pragma once

#include <cstddef>

namespace Stack::Renderer {
struct RawImageBackdropFrame;

// Display-only local and brightness-conditioned estimates, with deterministic
// grain. All images remain on the GPU.
// The owning backdrop supplies its context, VAO and borrowed photo patches.
class RawImageBackdropNoise {
public:
    bool Prepare(const RawImageBackdropFrame& frame);
    void Bind(unsigned int presentationProgram, int firstTextureUnit, int brightnessTextureUnit) const;
    void Shutdown();
private:
    void ReleaseProfiles();
    bool Ensure(int width, int height);
    unsigned int analysisProgram = 0, seedProgram = 0, filterProgram = 0, brightnessProgram = 0;
    unsigned int fineProfile = 0, coarseProfile = 0, profileFramebuffer = 0;
    unsigned int filteredFineProfile = 0, filteredCoarseProfile = 0, filteredFramebuffer = 0;
    unsigned int fineGrain = 0, coarseGrain = 0, grainFramebuffer = 0;
    unsigned int fineBrightness = 0, coarseBrightness = 0, brightnessFramebuffer = 0;
    int width = 0, height = 0;
    std::size_t profileIdentity = 0;
    bool profileValid = false;
    bool failed = false;
};
}
