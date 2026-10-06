#pragma once
#include <array>
#include <memory>
#include <vector>

struct GraphScopeData {
    std::array<float, 256> HistR{}, HistG{}, HistB{}, HistL{};
    struct ParadeColumn { float r[256]{}, g[256]{}, b[256]{}; };
    struct VectorPoint { float u, v; };
    std::vector<ParadeColumn> ParadeData;
    std::vector<VectorPoint> VectorPoints;
};

std::shared_ptr<const GraphScopeData> AnalyzeGraphScopePixels(
    const std::vector<unsigned char>& pixels, int width, int height);
