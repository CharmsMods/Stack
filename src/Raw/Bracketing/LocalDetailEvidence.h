#pragma once
#include <array>
#include <vector>
#include <functional>

namespace Raw::Bracketing {
struct LocalDetailCapture {
    std::array<std::vector<double>,4> coefficients;
    // Conditional covariance spectral bounds include the actual interpolation
    // footprint. Model uncertainty is separate from independent sensor noise.
    std::array<double,4> measurementBound{},uncertaintyBound{};
    std::array<double,4> precision{};
    double alignmentReliability=1;
};
enum class LocalDetailDecision {Weak,Consistent,Contradictory,Canceled};
struct LocalDetailEvidence {
    // [capture][coefficient]. Empty selections retain the temporal baseline.
    std::vector<std::vector<double>> weights;
    std::vector<unsigned char> selected;
    unsigned selectedCount=0;
    bool mayExpand=false,canceled=false;
    double minimumCaptureSupport=0;
};
LocalDetailEvidence AnalyzeLocalDetail(const std::vector<LocalDetailCapture>&,unsigned size,
    const std::function<bool()>& shouldCancel={});
}
