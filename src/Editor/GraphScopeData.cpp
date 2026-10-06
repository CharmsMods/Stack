#include "GraphScopeData.h"
#include <algorithm>
#include <cstdint>

std::shared_ptr<const GraphScopeData> AnalyzeGraphScopePixels(
    const std::vector<unsigned char>& pixels, int w, int h) {
    auto data = std::make_shared<GraphScopeData>();

    if (w <= 0 || h <= 0 || static_cast<std::size_t>(w) * h > pixels.size() / 4) {
        return data;
    }
    
    // Parade Setup (1 bin per pixel column if small enough)
    data->ParadeData.assign(w, GraphScopeData::ParadeColumn{});

    float maxHist = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            uint8_t r = pixels[idx];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            
            // 1. Histogram
            data->HistR[r]++;
            data->HistG[g]++;
            data->HistB[b]++;
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            data->HistL[(int)lum]++;

            // 2. Vectorscope (YUV Chroma)
            // Simplified conversion for plotting
            float rf = r / 255.0f;
            float gf = g / 255.0f;
            float bf = b / 255.0f;
            float u = -0.14713f * rf - 0.28886f * gf + 0.43692f * bf;
            float v =  0.61501f * rf - 0.51499f * gf - 0.10001f * bf;
            
            // Sample only some points for performance if buffer is large
            if ((x + y) % 2 == 0) {
                data->VectorPoints.push_back({u, v});
            }

            // 3. RGB Parade
            data->ParadeData[x].r[r]++;
            data->ParadeData[x].g[g]++;
            data->ParadeData[x].b[b]++;
        }
    }

    // Normalize Histograms
    for (int i = 0; i < 256; i++) {
        maxHist = std::max({maxHist, data->HistR[i], data->HistG[i], data->HistB[i], data->HistL[i]});
    }
    if (maxHist > 0) {
        for (int i = 0; i < 256; i++) {
            data->HistR[i] /= maxHist;
            data->HistG[i] /= maxHist;
            data->HistB[i] /= maxHist;
            data->HistL[i] /= maxHist;
        }
    }

    return data;
}
