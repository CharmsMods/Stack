#pragma once

#include <cstddef>

namespace Stack::Renderer {
struct RawImageBackdropFrame;

// Display-only local and brightness-conditioned color covariance in three
// spatial bands, with deterministic grain. All images remain on the GPU.
// The owning backdrop supplies its context and borrowed photo patches.
class RawImageBackdropNoise {
public:
    bool Prepare(const RawImageBackdropFrame& frame);
    void Bind(unsigned int presentationProgram, int firstTextureUnit, int brightnessTextureUnit) const;
    void Shutdown();
private:
    void ReleaseProfiles();
    bool Ensure(int width, int height);
    unsigned int analysisProgram = 0, seedProgram = 0, filterProgram = 0, brightnessProgram = 0;
    unsigned int profileVariance = 0, profileCovariance = 0;
    unsigned int filteredVariance = 0, filteredCovariance = 0;
    unsigned int grain = 0;
    unsigned int brightnessVariance = 0, brightnessCovariance = 0;
    // Uniform locations live with their linked programs, not profile textures.
    struct AnalysisUniforms {
        int image = -1, previous = -1, nativeExtent = -1, clearOnly = -1;
        int rect = -1, textureRect = -1, previousOffset = -1, previousScale = -1;
        int fadeAmount = -1, encodedSrgb = -1;
    } analysisUniforms;
    struct FilterUniforms {
        int varianceInput = -1, covarianceInput = -1, rejectOutliers = -1, direction = -1;
    } filterUniforms;
    struct BrightnessUniforms {
        int varianceInput = -1, covarianceInput = -1;
    } brightnessUniforms;
    int width = 0, height = 0;
    std::size_t profileIdentity = 0;
    bool profileValid = false;
    bool failed = false;
};
}
