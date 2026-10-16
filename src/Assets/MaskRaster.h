#pragma once
#include "Utils/HashUtils.h"
#include <memory>
#include <utility>
#include <vector>

namespace Stack::Assets {
// Published coverage is immutable. Documents, undo entries and worker requests
// share its storage; replacing pixels creates a new value without changing any
// in-flight request. The fingerprint is computed once at publication.
class MaskRaster {
public:
    MaskRaster() = default;
    MaskRaster(std::vector<float> pixels) { Set(std::move(pixels)); }
    MaskRaster& operator=(std::vector<float> pixels) { Set(std::move(pixels)); return *this; }
    static MaskRaster Share(std::shared_ptr<const std::vector<float>> pixels, std::size_t fingerprint) {
        MaskRaster value;
        if(pixels && !pixels->empty()) value.m_Data=std::make_shared<const Storage>(std::move(pixels),fingerprint);
        return value;
    }
    const std::vector<float>& Values() const {
        static const std::vector<float> empty;
        return m_Data ? (m_Data->sharedPixels ? *m_Data->sharedPixels : m_Data->pixels) : empty;
    }
    bool empty() const { return Values().empty(); }
    std::size_t size() const { return Values().size(); }
    const float* data() const { return Values().data(); }
    float operator[](std::size_t i) const { return Values()[i]; }
    std::size_t Fingerprint() const { return m_Data ? m_Data->fingerprint : 0; }
    bool SharesStorage(const MaskRaster& other) const {
        return m_Data == other.m_Data || (m_Data && other.m_Data && m_Data->sharedPixels &&
            m_Data->sharedPixels==other.m_Data->sharedPixels);
    }
    bool operator==(const MaskRaster& other) const {
        return SharesStorage(other) || Values() == other.Values();
    }
    bool operator==(const std::vector<float>& other) const { return Values() == other; }
private:
    struct Storage {
        std::vector<float> pixels;
        std::shared_ptr<const std::vector<float>> sharedPixels;
        std::size_t fingerprint = 0;
        Storage(std::shared_ptr<const std::vector<float>> value,std::size_t hash)
            : sharedPixels(std::move(value)),fingerprint(hash) {}
        explicit Storage(std::vector<float> value) : pixels(std::move(value)) {
            if (!pixels.empty()) fingerprint = StackHash::HashBytes(
                reinterpret_cast<const unsigned char*>(pixels.data()), pixels.size()*sizeof(float));
        }
    };
    void Set(std::vector<float> pixels) {
        m_Data = pixels.empty() ? nullptr : std::make_shared<const Storage>(std::move(pixels));
    }
    std::shared_ptr<const Storage> m_Data;
};
}
