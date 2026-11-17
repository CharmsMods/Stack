#pragma once
#include "Renderer/ViewportPreviewTypes.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Renderer {
// Native, settled pixels for one immutable document/view revision. CPU-only:
// reads/writes run on the render worker; coverage queries never touch disk or
// wait on payload transfers. Published sections are immutable for this store's
// lifetime; readers keep the store alive while polling its completion metadata.
class ViewportSectionStore {
public:
    static constexpr int Edge = 1024;
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
    bool Load(int index, std::vector<float>& pixels, std::vector<float>& hdr, std::string& error) const;
    bool Store(int index, std::vector<float> pixels, std::vector<float> hdr, std::string& error);
    std::uint64_t DiskBytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}
