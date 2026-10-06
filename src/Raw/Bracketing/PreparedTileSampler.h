#pragma once

#include "Raw/MultiFrameDenoise/SameCfaSampler.h"
#include "CaptureFallback.h"

#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>

namespace Raw::Bracketing {

// Bracketing evaluates every output sample through the shared same-CFA math.
// This bounded view keeps prepared tiles resident and exposes their taps
// directly, avoiding a full tile-vector copy for every 16-tap footprint.
class PreparedTileSampler final : public Mfd::NormalizedTileCache {
public:
    explicit PreparedTileSampler(
        const std::filesystem::path& root,
        std::size_t capacity = 8);

    Mfd::TileCacheReadStatus Read(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY,
        Mfd::PreparedRawTile& tile,
        std::string* error = nullptr) override;

    bool Write(
        const std::string& cacheKey,
        const Mfd::PreparedRawTile& tile,
        std::string* error = nullptr) override;

    bool ReadSample(
        const Mfd::PreparedRawFrame& frame,
        std::uint64_t rawX,
        std::uint64_t rawY,
        Mfd::SameCfaTapInput& sample,
        std::string* error = nullptr);

    bool SampleSameCfa(
        const Mfd::PreparedRawFrame& frame,
        const Mfd::NoiseModel& noiseModel,
        Mfd::RawCoordinate sourceRaw,
        Mfd::CfaSite site,
        const Mfd::SameCfaScalarParameters& parameters,
        Mfd::SameCfaSampleResult& result,
        CaptureFallback& fallback);

private:
    struct Entry {
        Mfd::PreparedRawTile tile;
        std::list<std::string>::iterator order;
    };

    static std::string Key(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY);

    Mfd::TileCacheReadStatus GetTile(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY,
        const Mfd::PreparedRawTile*& tile,
        std::string* error);

    Mfd::DirectoryNormalizedTileCache m_Backing;
    std::size_t m_Capacity = 8;
    std::list<std::string> m_Order;
    std::unordered_map<std::string, Entry> m_Entries;
    const Mfd::PreparedRawTile* m_LastTile=nullptr;
    std::string m_LastCacheKey;
    std::uint32_t m_LastX=0,m_LastY=0;
};

} // namespace Raw::Bracketing
