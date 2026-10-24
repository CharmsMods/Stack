#include "Editor/CompositeRasterSafety.h"

#include <cstdlib>
#include <iostream>

namespace {
enum class AllocationFailure { None, BadAlloc, LengthError };
AllocationFailure nextAllocationFailure = AllocationFailure::None;
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

void* operator new(std::size_t size) {
    const auto failure = nextAllocationFailure;
    nextAllocationFailure = AllocationFailure::None;
    if (failure == AllocationFailure::BadAlloc) throw std::bad_alloc();
    if (failure == AllocationFailure::LengthError) throw std::length_error("injected allocation failure");
    if (void* memory = std::malloc(size ? size : 1u)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    using namespace Stack::EditorRendering;
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const int maximum = std::numeric_limits<int>::max();
    int value = 0;
    Check(TryRoundCompositeDimension(2.5, value) && value == 3, "ordinary nearest rounding");
    Check(TryRoundCompositeDimension(-8.0, value) && value == 1, "existing minimum normalization");
    Check(TryRoundCompositeDimension(maximum, value) && value == maximum, "largest representable dimension");
    Check(!TryRoundCompositeDimension(static_cast<double>(maximum) + 1.0, value) && value == 0,
        "out-of-range dimension rejected before integer conversion");
    Check(!TryRoundCompositeDimension(infinity, value) && !TryRoundCompositeDimension(nan, value),
        "nonfinite dimensions rejected");
    Check(TryResolveCompositePairedDimension(1920, 16.0 / 9.0, true, value) && value == 1080,
        "ordinary width-driven sizing");
    Check(TryResolveCompositePairedDimension(1080, 16.0 / 9.0, false, value) && value == 1920,
        "ordinary height-driven sizing");
    Check(TryResolveCompositePairedDimension(1025, 2.0f / 3.0f, true, value) && value == 1538,
        "half-pixel sizing preserves prior float rounding");
    Check(!TryResolveCompositePairedDimension(250000, 0.0001, true, value) && value == 0,
        "extreme aspect cannot overflow height");
    Check(!TryResolveCompositePairedDimension(maximum, 2.0, false, value) && value == 0,
        "extreme aspect cannot overflow width");
    Check(!TryResolveCompositePairedDimension(1, infinity, false, value) &&
        !TryResolveCompositePairedDimension(1, nan, true, value), "nonfinite aspect rejected");
    Check(!TryResolveCompositePairedDimension(1, std::numeric_limits<double>::max(), false, value),
        "finite aspect outside float representation is rejected before narrowing");
    CompositeRasterSize size;
    Check(TryPlanCompositeRaster(2, 1.0, 1.0, size) && size.width == 2 && size.height == 2 && size.bytes == 16,
        "small square planned");
    Check(!TryPlanCompositeRaster(2, 0.0, 1.0, size) && size.width == 0 && size.bytes == 0,
        "invalid bounds clear the size plan");
    Check(!TryPlanCompositeRaster(2, infinity, 1.0, size) &&
        !TryPlanCompositeRaster(2, 1.0, nan, size), "nonfinite bounds rejected");
    Check(!TryPlanCompositeRaster(2, std::numeric_limits<double>::max(), 1.0, size),
        "unrepresentable planned aspect is rejected before narrowing");
    // Planning only: these cases must never allocate an image-sized buffer.
    if (std::numeric_limits<std::size_t>::max() > 0xffffffffull) {
        Check(TryPlanCompositeRaster(32768, 1.0, 1.0, size) && size.bytes == 4294967296ull,
            "32768-square RGBA bytecount does not overflow signed int");
        Check(CompositeRgbaOffset(32768, 32767, 32767) == 4294967292ull,
            "source and destination offsets promote before multiplication");
    }
    Check(!TryPlanCompositeRaster(maximum, 1.0, 1.0, size),
        "unrepresentable container size rejected without allocation");
    int first = 0, last = 0;
    Check(TryCompositeRasterSpan(-1.0e30, 1.0e30, 0.0, 10.0, 100, first, last) && first == 0 && last == 99,
        "huge covering bounds clamp before integer conversion");
    Check(!TryCompositeRasterSpan(1.0e30, 2.0e30, 0.0, 10.0, 100, first, last),
        "huge right off-canvas item has no span");
    Check(!TryCompositeRasterSpan(-2.0e30, -1.0e30, 0.0, 10.0, 100, first, last),
        "huge left off-canvas item has no span");
    Check(!TryCompositeRasterSpan(nan, 1.0, 0.0, 1.0, 100, first, last), "nonfinite span rejected");
    Check(TryCompositeRasterSpan(0.2, 0.4, 0.0, 1.0, 100, first, last) && first == 20 && last == 40,
        "ordinary span preserves floor/ceil coverage");
    Check(TryPlanCompositeRaster(2, 1.0, 1.0, size), "tiny allocation plan");
    std::vector<unsigned char> pixels;
    int width = 9, height = 9;
    for (const auto failure : {AllocationFailure::BadAlloc, AllocationFailure::LengthError}) {
        nextAllocationFailure = failure;
        const bool allocated = TryAllocateCompositeRaster(size, pixels, width, height);
        Check(!allocated && pixels.empty() && width == 0 && height == 0 &&
            nextAllocationFailure == AllocationFailure::None, "allocation exception resets all outputs");
    }
    Check(TryAllocateCompositeRaster(size, pixels, width, height) && width == 2 && height == 2 &&
        pixels.size() == 16 && std::all_of(pixels.begin(), pixels.end(), [](unsigned char value) { return value == 0; }),
        "tiny allocation succeeds after failures");
    size.bytes = 1;
    Check(!TryAllocateCompositeRaster(size, pixels, width, height) && pixels.empty() && width == 0 && height == 0,
        "inconsistent public size plan cannot allocate or retain old outputs");
    if (failures) return 1;
    std::cout << "Composite raster safety checks passed; largest real buffer was 16 bytes.\n";
}
