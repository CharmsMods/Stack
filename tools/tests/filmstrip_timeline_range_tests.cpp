#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

using Range = std::pair<std::size_t, std::size_t>;
struct Geometry {
    float originX, contentOffset, scaledTileWidth, scrollX;
    float Center(std::size_t index) const {
        return originX + contentOffset + static_cast<float>(index) *
            (scaledTileWidth + 10.f) + scaledTileWidth * .5f - scrollX;
    }
};
Range Actual(std::size_t count, Geometry geometry, float low, float high) {
    // Production returns before constructing the lambdas for an empty gallery.
    if (!count) return {0, 0};
    const std::vector<int> m_RawWorkspaceLabFilmstripTimelineEntries(count);
    const struct { float x; } origin{geometry.originX};
    const float contentOffset = geometry.contentOffset;
    const float scaledTileWidth = geometry.scaledTileWidth;
    const float tileStep = scaledTileWidth + 10.f;
    const float scrollX = geometry.scrollX;
// PRODUCTION_TIMELINE_RANGE
    return entryRange(low, high);
}
bool Check(bool passed, const char* message) {
    if (!passed) std::cerr << "FAILED: " << message << '\n';
    return passed;
}
std::size_t Nearest(Geometry geometry, Range range, float mouse) {
    float nearest = 7.f;
    std::size_t selected = std::numeric_limits<std::size_t>::max();
    for (auto index = range.first; index < range.second; ++index) {
        const float distance = std::abs(mouse - geometry.Center(index));
        if (distance < nearest) { nearest = distance; selected = index; }
    }
    return selected;
}
int main() {
    bool passed = true;
    for (std::size_t count : {0u, 1u, 1000u, 100000u})
    for (Geometry geometry : {Geometry{8, 0, 124, 0}, {8, 0, 80.6f, 40000},
            {8, 20, 217, 10000000}, {8, 0, -5, 0},
            {8, 1.e20f, 124, 0}, {8, -1.e20f, 124, 0}}) {
        const float center = geometry.Center(count / 2);
        for (Range indices : {Range{0, count}, {count / 2, count / 2}}) {
            const float low = indices.first ? center : 18.f;
            const float high = indices.second == count ? low + 1180.f : center;
            const auto actual = Actual(count, geometry, low, high);
            std::vector<std::size_t> expected, found;
            for (std::size_t i = 0; i < count; ++i)
                if (geometry.Center(i) >= low && geometry.Center(i) <= high) expected.push_back(i);
            for (auto i = actual.first; i < actual.second; ++i) found.push_back(i);
            passed = Check(found == expected, "inclusive range matches full scan, including rounded duplicate centers") && passed;
        }
        for (float mouse : {center, center + 7.f, center - 7.f,
                std::nextafter(center + 7.f, center), center + 2.5f}) {
            passed = Check(Nearest(geometry, Actual(count, geometry, mouse - 7.f, mouse + 7.f), mouse) ==
                Nearest(geometry, {0, count}, mouse), "nearest strict radius and first-index tie match full scan") && passed;
        }
    }
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (Geometry geometry : {Geometry{0, 0, -10, 0}, {0, 0, -20, 0},
            {0, 0, inf, 0}, {0, 0, nan, 0}, {0, inf, 124, 0}, {0, 0, 124, inf}})
        passed = Check(Actual(100, geometry, 0, 100) == Range{0, 100}, "invalid geometry preserves full-scan fallback") && passed;
    for (float bound : {inf, -inf, nan})
        passed = Check(Actual(100, {0, 0, 124, 0}, bound, bound) == Range{0, 100}, "nonfinite bounds preserve full-scan fallback") && passed;
    const auto visible = Actual(100000, {0, 0, 124, 6700000}, 0, 1200);
    passed = Check(visible.second - visible.first <= 10, "large gallery visits only visible candidates") && passed;
    if (!passed) return 1;
    std::cout << "Filmstrip timeline ranges passed (edges, rounded centers, hover ties, scroll, fallback, bounded candidates).\n";
}
