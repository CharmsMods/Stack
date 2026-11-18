#include "Raw/MultiFrameDenoise/PyramidVariance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Raw::Mfd::PixelExtent;
constexpr std::array<double, 5> kKernel { 1. / 16., 4. / 16., 6. / 16., 4. / 16., 1. / 16. };

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

PixelExtent Downsampled(PixelExtent extent, unsigned level) {
    for (unsigned i = 0u; i < level; ++i) {
        extent = { (extent.width + 1u) / 2u, (extent.height + 1u) / 2u };
    }
    return extent;
}

std::size_t Reflect(std::int64_t index, std::size_t extent) {
    if (extent == 1u) return 0u;
    const auto period = static_cast<std::int64_t>(2u * (extent - 1u));
    index = (index % period + period) % period;
    return static_cast<std::size_t>(std::min(index, period - index));
}

// Independent reference: send one native unit basis image at a time through the
// actual 2D signal-filter cascade, then sum squared final responses * variances.
// No composed stencils, separable variance pass or production helper is reused.
std::vector<double> BasisReference(const std::vector<double>& inputVariance,
    PixelExtent extent, unsigned level, const std::array<double, 5>& kernel) {
    const auto target = Downsampled(extent, level);
    std::vector<double> result(target.width * target.height, 0.0);
    for (std::size_t source = 0u; source < inputVariance.size(); ++source) {
        std::vector<double> response(inputVariance.size(), 0.0);
        response[source] = 1.0;
        auto previousExtent = extent;
        for (unsigned depth = 0u; depth < level; ++depth) {
            const auto nextExtent = Downsampled(previousExtent, 1u);
            std::vector<double> next(nextExtent.width * nextExtent.height, 0.0);
            for (std::size_t y = 0u; y < nextExtent.height; ++y) {
                for (std::size_t x = 0u; x < nextExtent.width; ++x) {
                    for (int ky = -2; ky <= 2; ++ky) {
                        for (int kx = -2; kx <= 2; ++kx) {
                            const auto sx = Reflect(2 * static_cast<std::int64_t>(x) + kx,
                                static_cast<std::size_t>(previousExtent.width));
                            const auto sy = Reflect(2 * static_cast<std::int64_t>(y) + ky,
                                static_cast<std::size_t>(previousExtent.height));
                            next[y * nextExtent.width + x] += kernel[kx + 2] * kernel[ky + 2] *
                                response[sy * previousExtent.width + sx];
                        }
                    }
                }
            }
            response = std::move(next);
            previousExtent = nextExtent;
        }
        for (std::size_t i = 0u; i < result.size(); ++i) {
            result[i] += response[i] * response[i] * inputVariance[source];
        }
    }
    return result;
}

void CheckBasis(PixelExtent extent, const std::array<double, 5>& kernel) {
    std::vector<double> input(extent.width * extent.height);
    for (std::size_t i = 0u; i < input.size(); ++i) input[i] = .2 + .03 * ((i * 7u) % 23u);
    const auto original = input;
    for (unsigned level = 1u; level <= 3u; ++level) {
        const auto target = Downsampled(extent, level);
        std::vector<double> actual(target.width * target.height, -1.0);
        std::string error;
        Check(Raw::Mfd::PropagatePyramidLevelVariance(input, extent, level, kernel,
            actual, &error), error.c_str());
        const auto expected = BasisReference(input, extent, level, kernel);
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            Check(std::abs(actual[i] - expected[i]) <= 2e-13 * std::max(1.0, expected[i]),
                "Exact pyramid marginal disagrees with independent basis propagation.");
        }
    }
    Check(input == original, "Pyramid variance propagation modified its native input.");
}

void CheckInterior() {
    const PixelExtent extent { 99u, 97u };
    std::vector<double> input(extent.width * extent.height, 1.0);
    constexpr double expected[] = {
        0., 0.07476806640625, 0.015245858579874039, 0.0036432665840493428
    };
    for (unsigned level = 1u; level <= 3u; ++level) {
        const auto target = Downsampled(extent, level);
        std::vector<double> actual(target.width * target.height);
        Check(Raw::Mfd::PropagatePyramidLevelVariance(input, extent, level, kKernel, actual),
            "Interior pyramid variance propagation failed.");
        const auto middle = (target.height / 2u) * target.width + target.width / 2u;
        Check(std::abs(actual[middle] - expected[level]) < 1e-15,
            "Composed binomial kernel has incorrect interior variance.");
    }
}

} // namespace

int main() {
    try {
        CheckInterior();
        for (PixelExtent extent : { PixelExtent{1u, 1u}, {2u, 3u}, {7u, 6u},
                {9u, 7u}, {10u, 8u}, {17u, 13u}, {67u, 3u} }) {
            CheckBasis(extent, kKernel);
        }
        CheckBasis({9u, 7u}, {.1, .15, .4, .3, .05});
        CheckBasis({9u, 7u}, {0., 0., 1., 0., 0.});
        std::cout << "Pyramid variance passed: native-independent marginal values at levels 1-3, "
            "reflected/odd/small boundaries, column-block seam, heterogeneous noise, "
            "asymmetric and identity kernels; fixed scratch allowance "
            << Raw::Mfd::kPyramidVarianceScratchBytes << " bytes.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
