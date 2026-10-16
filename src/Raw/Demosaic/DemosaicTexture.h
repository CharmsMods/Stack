#pragma once
#include "Raw/RawImageData.h"
#include "GpuDemosaic.h"
#include <cstddef>
#include <functional>
#include <string>

namespace Raw::Demosaic {
// Owned by one RawGpuPipeline and used on that pipeline's GL context/worker.
// Retains only a completed RGB texture and compiled GPU programs. Intermediate
// images stay on the GPU and are released after reconstruction.
class DemosaicTexture {
public:
    ~DemosaicTexture();
    DemosaicTexture() = default;
    DemosaicTexture(const DemosaicTexture&) = delete;
    DemosaicTexture& operator=(const DemosaicTexture&) = delete;
    void Clear();
    unsigned int GetOrRender(std::size_t key,int width,int height,CfaPattern pattern,
        DemosaicMethod method,const std::function<bool()>& renderPreparedMosaic,
        const std::function<bool()>& cancel,std::string& error,
        const std::array<float,9>& cameraToSrgb = {1,0,0,0,1,0,0,0,1});
private:
    GpuDemosaic m_Gpu;
    unsigned int m_Texture = 0;
    std::size_t m_Key = 0;
};
} // namespace Raw::Demosaic
