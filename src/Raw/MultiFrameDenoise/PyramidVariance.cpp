#include "Raw/MultiFrameDenoise/PyramidVariance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Raw::Mfd {
namespace {

constexpr std::size_t kColumnBlock = 32u;
constexpr std::size_t kMaximumTaps =
    4u * ((std::size_t{1} << kMaximumPyramidVarianceLevel) - 1u) + 1u;

struct Tap {
    std::int64_t index = 0;
    double coefficient = 0.0;
};

struct AxisStencil {
    std::array<Tap, kMaximumTaps> taps {};
    std::size_t count = 0u;
};

struct Workspace {
    std::array<AxisStencil, kColumnBlock> columns;
    std::array<std::array<double, kColumnBlock>, kMaximumTaps> rows;
    std::array<std::int64_t, kMaximumTaps> rowKeys;
    std::array<std::uint64_t, kMaximumTaps> rowAges;
};

// The explicit numerical scratch includes a nested stencil-construction call.
// The remaining allowance covers scalar state and the small sort call stack.
static_assert(sizeof(Workspace) + sizeof(AxisStencil) +
    sizeof(std::array<Tap, 5u * kMaximumTaps>) +
    sizeof(std::array<std::int64_t, kMaximumPyramidVarianceLevel + 1u>) +
    4096u <= kPyramidVarianceScratchBytes);

bool Fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

std::int64_t Reflect(std::int64_t index, std::int64_t extent) {
    if (extent <= 1) return 0;
    // A five-tap step can be at most two sites outside this level. Iteration
    // also covers the one- and two-pixel levels without a special approximation.
    while (index < 0 || index >= extent) {
        if (index < 0) index = -index;
        if (index >= extent) index = 2 * extent - 2 - index;
    }
    return index;
}

bool BuildStencil(std::uint64_t baseExtent, std::uint32_t level,
    std::uint64_t outputIndex, const std::array<double, 5>& kernel,
    AxisStencil& stencil) {
    std::array<std::int64_t, kMaximumPyramidVarianceLevel + 1u> extents;
    extents[0] = static_cast<std::int64_t>(baseExtent);
    for (std::uint32_t depth = 1u; depth <= level; ++depth) {
        extents[depth] = (extents[depth - 1u] + 1) / 2;
    }
    stencil.count = 1u;
    stencil.taps[0] = { static_cast<std::int64_t>(outputIndex), 1.0 };
    std::array<Tap, 5u * kMaximumTaps> expanded;
    for (std::uint32_t depth = level; depth > 0u; --depth) {
        std::size_t count = 0u;
        for (std::size_t i = 0u; i < stencil.count; ++i) {
            for (std::size_t k = 0u; k < kernel.size(); ++k) {
                const double coefficient = stencil.taps[i].coefficient * kernel[k];
                if (coefficient == 0.0) continue;
                expanded[count++] = {
                    Reflect(2 * stencil.taps[i].index +
                        static_cast<std::int64_t>(k) - 2, extents[depth - 1u]),
                    coefficient
                };
            }
        }
        std::sort(expanded.begin(), expanded.begin() + count,
            [](const Tap& a, const Tap& b) { return a.index < b.index; });
        stencil.count = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            if (stencil.count != 0u &&
                stencil.taps[stencil.count - 1u].index == expanded[i].index) {
                stencil.taps[stencil.count - 1u].coefficient += expanded[i].coefficient;
            } else {
                if (stencil.count == kMaximumTaps) return false;
                stencil.taps[stencil.count++] = expanded[i];
            }
        }
    }
    return stencil.count != 0u;
}

bool CheckedCount(PixelExtent extent, std::size_t& count) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::size_t>::max() / extent.height) {
        return false;
    }
    count = static_cast<std::size_t>(extent.width * extent.height);
    return true;
}

} // namespace

bool PropagatePyramidLevelVariance(const std::vector<double>& baseVariance,
    PixelExtent baseExtent, std::uint32_t level,
    const std::array<double, 5>& kernel, std::vector<double>& levelVariance,
    std::string* error) {
    if (level == 0u || level > kMaximumPyramidVarianceLevel ||
        &baseVariance == &levelVariance ||
        baseExtent.width > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max() / 2 - 2) ||
        baseExtent.height > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max() / 2 - 2)) {
        return Fail(error, "MFD pyramid variance request has invalid level or geometry.");
    }
    std::size_t baseCount = 0u;
    if (!CheckedCount(baseExtent, baseCount) || baseVariance.size() != baseCount) {
        return Fail(error, "MFD pyramid variance source does not match its extent.");
    }
    PixelExtent target = baseExtent;
    for (std::uint32_t depth = 0u; depth < level; ++depth) {
        target.width = (target.width + 1u) / 2u;
        target.height = (target.height + 1u) / 2u;
    }
    std::size_t targetCount = 0u;
    if (!CheckedCount(target, targetCount) || levelVariance.size() != targetCount) {
        return Fail(error, "MFD pyramid variance destination does not match its extent.");
    }
    double kernelSum = 0.0;
    for (double coefficient : kernel) {
        if (!std::isfinite(coefficient) || coefficient < 0.0) {
            return Fail(error, "MFD pyramid variance kernel is invalid.");
        }
        kernelSum += coefficient;
    }
    if (std::abs(kernelSum - 1.0) > 1.0e-12) {
        return Fail(error, "MFD pyramid variance kernel must sum to one.");
    }

    Workspace workspace;
    AxisStencil rowStencil;
    for (std::uint64_t blockX = 0u; blockX < target.width; blockX += kColumnBlock) {
        const std::size_t columns = static_cast<std::size_t>(
            std::min<std::uint64_t>(kColumnBlock, target.width - blockX));
        for (std::size_t column = 0u; column < columns; ++column) {
            if (!BuildStencil(baseExtent.width, level, blockX + column, kernel,
                    workspace.columns[column])) {
                return Fail(error, "MFD pyramid variance horizontal support overflowed.");
            }
        }
        workspace.rowKeys.fill(-1);
        workspace.rowAges.fill(0u);
        std::uint64_t age = 0u;
        for (std::uint64_t y = 0u; y < target.height; ++y) {
            if (!BuildStencil(baseExtent.height, level, y, kernel, rowStencil)) {
                return Fail(error, "MFD pyramid variance vertical support overflowed.");
            }
            double* output = levelVariance.data() +
                static_cast<std::size_t>(y * target.width + blockX);
            std::fill(output, output + columns, 0.0);
            for (std::size_t tap = 0u; tap < rowStencil.count; ++tap) {
                const auto& row = rowStencil.taps[tap];
                std::size_t slot = 0u;
                while (slot < kMaximumTaps && workspace.rowKeys[slot] != row.index) ++slot;
                if (slot == kMaximumTaps) {
                    slot = static_cast<std::size_t>(std::min_element(
                        workspace.rowAges.begin(), workspace.rowAges.end()) -
                        workspace.rowAges.begin());
                    const double* input = baseVariance.data() +
                        static_cast<std::size_t>(row.index) *
                        static_cast<std::size_t>(baseExtent.width);
                    for (std::size_t column = 0u; column < columns; ++column) {
                        const auto& stencil = workspace.columns[column];
                        double variance = 0.0;
                        for (std::size_t xTap = 0u; xTap < stencil.count; ++xTap) {
                            const auto& source = stencil.taps[xTap];
                            variance += source.coefficient * source.coefficient *
                                input[source.index];
                        }
                        workspace.rows[slot][column] = variance;
                    }
                    workspace.rowKeys[slot] = row.index;
                }
                workspace.rowAges[slot] = ++age;
                const double weight = row.coefficient * row.coefficient;
                for (std::size_t column = 0u; column < columns; ++column) {
                    output[column] += weight * workspace.rows[slot][column];
                }
            }
        }
    }
    return true;
}

} // namespace Raw::Mfd
