#pragma once
#include "Renderer/ViewportSectionIndex.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Renderer {
// Native, settled pixels for one immutable document/view revision. CPU-only:
// reads/writes run on the render worker; coverage queries never touch disk.
class ViewportSectionStore {
public:
    static constexpr int Edge = ViewportSectionIndex::Edge;
    struct Pixels {
        std::vector<float> pixels, hdr;
        Pixels() = default;
        ~Pixels();
        Pixels(const Pixels&) = delete;
        Pixels& operator=(const Pixels&) = delete;
    private:
        friend class ViewportSectionStore;
        std::uint64_t retainedBytes = 0;
    };
    using PixelLease = std::shared_ptr<const Pixels>;

    ViewportSectionStore(int width, int height, std::uint64_t ramAllowance = 256ull * 1024 * 1024);
    ~ViewportSectionStore();
    ViewportSectionStore(const ViewportSectionStore&) = delete;
    ViewportSectionStore& operator=(const ViewportSectionStore&) = delete;
    int Count() const;
    ImageRegion Bounds(int index) const;
    ImageRegion Raster(int index) const;
    bool Has(int index) const;
    bool Complete() const;
    bool Covers(const ImageRegion& region) const;
    int NextMissing(const ImageRegion& visible, bool finishWhole) const;
    // Worker-only. RAM hits borrow immutable pixels without duplicating them.
    // Disk misses own a bounded transfer payload. Keep the lease through upload.
    PixelLease LoadShared(int index, std::string& error) const;
    bool Load(int index, std::vector<float>& pixels, std::vector<float>& hdr, std::string& error) const;
    bool Store(int index, std::vector<float> pixels, std::vector<float> hdr, std::string& error);
    std::uint64_t DiskBytes() const;
    // Aggregate retained capacity across stores AND outstanding RAM leases.
    static std::uint64_t RetainedRamBytes();
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}
