#include "PreparedTileSampler.h"
#include "BorderSupport.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Raw::Bracketing {

PreparedTileSampler::PreparedTileSampler(
    const std::filesystem::path& root,
    std::size_t capacity)
    : m_Backing(root),
      m_Capacity(std::max<std::size_t>(4, capacity)) {}

std::string PreparedTileSampler::Key(
    const std::string& cacheKey,
    std::uint32_t tileX,
    std::uint32_t tileY) {
    return cacheKey + ':' + std::to_string(tileX) + ':' +
        std::to_string(tileY);
}

Mfd::TileCacheReadStatus PreparedTileSampler::GetTile(
    const std::string& cacheKey,
    std::uint32_t tileX,
    std::uint32_t tileY,
    const Mfd::PreparedRawTile*& tile,
    std::string* error) {
    if(m_LastTile&&m_LastX==tileX&&m_LastY==tileY&&m_LastCacheKey==cacheKey) {
        tile=m_LastTile;return Mfd::TileCacheReadStatus::Hit;
    }
    m_LastTile=nullptr;
    const auto remember=[&] {
        m_LastTile=tile;m_LastCacheKey=cacheKey;m_LastX=tileX;m_LastY=tileY;
    };
    const std::string key = Key(cacheKey, tileX, tileY);
    auto found = m_Entries.find(key);
    if (found != m_Entries.end()) {
        m_Order.splice(m_Order.end(), m_Order, found->second.order);
        tile = &found->second.tile;
        remember();
        return Mfd::TileCacheReadStatus::Hit;
    }

    Mfd::PreparedRawTile loaded;
    const auto status = m_Backing.Read(
        cacheKey, tileX, tileY, loaded, error);
    if (status != Mfd::TileCacheReadStatus::Hit) return status;

    if (m_Entries.size() >= m_Capacity) {
        m_Entries.erase(m_Order.front());
        m_Order.pop_front();
    }
    m_Order.push_back(key);
    Entry entry;
    entry.tile = std::move(loaded);
    entry.order = std::prev(m_Order.end());
    found = m_Entries.emplace(key, std::move(entry)).first;
    tile = &found->second.tile;
    remember();
    return status;
}

Mfd::TileCacheReadStatus PreparedTileSampler::Read(
    const std::string& cacheKey,
    std::uint32_t tileX,
    std::uint32_t tileY,
    Mfd::PreparedRawTile& tile,
    std::string* error) {
    const Mfd::PreparedRawTile* resident = nullptr;
    const auto status = GetTile(
        cacheKey, tileX, tileY, resident, error);
    if (status == Mfd::TileCacheReadStatus::Hit) tile = *resident;
    return status;
}

bool PreparedTileSampler::Write(
    const std::string& cacheKey,
    const Mfd::PreparedRawTile& tile,
    std::string* error) {
    if (!m_Backing.Write(cacheKey, tile, error)) return false;
    // The cache interface permits rewriting a tile. Do not serve an older
    // resident copy after the backing store has accepted its replacement.
    const auto key = Key(cacheKey, tile.tileX, tile.tileY);
    const auto found = m_Entries.find(key);
    if (found != m_Entries.end()) {
        if (m_LastTile == &found->second.tile) m_LastTile = nullptr;
        m_Order.erase(found->second.order);
        m_Entries.erase(found);
    }
    return true;
}

bool PreparedTileSampler::ReadRegion(
    const Mfd::PreparedRawFrame& frame,
    std::uint64_t left, std::uint64_t top,
    unsigned width, unsigned height,
    Mfd::PreparedRawTile& region,
    std::string* error) {
    region = {};
    if (!frame.tileRawPixels || left > frame.activeExtent.width ||
        top > frame.activeExtent.height || width > frame.activeExtent.width - left ||
        height > frame.activeExtent.height - top ||
        (height && width > std::numeric_limits<std::size_t>::max() / height)) {
        if (error) *error = "The prepared RAW region is outside its source frame.";
        return false;
    }
    region.originX = left; region.originY = top; region.extent = {width, height};
    const auto count = static_cast<std::size_t>(width) * height;
    region.normalizedMosaic.resize(count);
    region.comparisonGain.resize(count);
    region.sampleFlags.resize(count);
    for (unsigned y = 0; y < height;) {
        const auto rawY = top + y;
        const auto rows = std::min<std::uint64_t>(height - y,
            frame.tileRawPixels - rawY % frame.tileRawPixels);
        for (unsigned x = 0; x < width;) {
            const auto rawX = left + x;
            const auto columns = std::min<std::uint64_t>(width - x,
                frame.tileRawPixels - rawX % frame.tileRawPixels);
            const Mfd::PreparedRawTile* tile = nullptr;
            if (GetTile(frame.cacheKey, static_cast<std::uint32_t>(rawX / frame.tileRawPixels),
                    static_cast<std::uint32_t>(rawY / frame.tileRawPixels), tile, error) !=
                    Mfd::TileCacheReadStatus::Hit) {
                if (error && error->empty()) *error = "A prepared RAW region tile is unavailable.";
                return false;
            }
            if (!tile || rawX < tile->originX || rawY < tile->originY ||
                rawX - tile->originX > tile->extent.width || rawY - tile->originY > tile->extent.height ||
                columns > tile->extent.width - (rawX - tile->originX) ||
                rows > tile->extent.height - (rawY - tile->originY)) {
                if (error) *error = "A prepared RAW tile does not cover its requested region.";
                return false;
            }
            const auto offset = static_cast<std::size_t>(rawY - tile->originY) *
                tile->extent.width + rawX - tile->originX;
            const auto end = offset + (rows - 1) * tile->extent.width + columns;
            if (end > tile->normalizedMosaic.size() || end > tile->comparisonGain.size() ||
                end > tile->sampleFlags.size()) {
                if (error) *error = "A prepared RAW tile has incomplete sample data.";
                return false;
            }
            for (std::size_t row = 0; row < rows; ++row) {
                const auto from = offset + row * tile->extent.width;
                const auto to = (static_cast<std::size_t>(y) + row) * width + x;
                std::copy_n(tile->normalizedMosaic.data() + from, columns, region.normalizedMosaic.data() + to);
                std::copy_n(tile->comparisonGain.data() + from, columns, region.comparisonGain.data() + to);
                std::copy_n(tile->sampleFlags.data() + from, columns, region.sampleFlags.data() + to);
            }
            x += static_cast<unsigned>(columns);
        }
        y += static_cast<unsigned>(rows);
    }
    return true;
}

bool PreparedTileSampler::ReadSample(
    const Mfd::PreparedRawFrame& frame,
    std::uint64_t rawX,
    std::uint64_t rawY,
    Mfd::SameCfaTapInput& sample,
    std::string* error) {
    if (rawX >= frame.activeExtent.width ||
        rawY >= frame.activeExtent.height) return false;
    if (frame.tileRawPixels == 0) {
        if (error) *error = "The prepared RAW frame has an invalid tile size.";
        return false;
    }
    const auto tileX = static_cast<std::uint32_t>(
        rawX / frame.tileRawPixels);
    const auto tileY = static_cast<std::uint32_t>(
        rawY / frame.tileRawPixels);
    const Mfd::PreparedRawTile* tile = nullptr;
    if (GetTile(frame.cacheKey, tileX, tileY, tile, error) !=
        Mfd::TileCacheReadStatus::Hit) {
        if (error && error->empty())
            *error = "A prepared RAW tile required for bracketing is unavailable.";
        return false;
    }
    if (!tile ||
        rawX < tile->originX || rawY < tile->originY ||
        rawX >= tile->originX + tile->extent.width ||
        rawY >= tile->originY + tile->extent.height) {
        if (error) *error = "A prepared RAW tile does not cover its requested sample.";
        return false;
    }
    const auto index = static_cast<std::size_t>(rawY - tile->originY) *
        tile->extent.width + rawX - tile->originX;
    if (index >= tile->normalizedMosaic.size() ||
        index >= tile->comparisonGain.size() ||
        index >= tile->sampleFlags.size()) {
        if (error) *error = "A prepared RAW tile has incomplete sample data.";
        return false;
    }
    sample.normalizedSample = tile->normalizedMosaic[index];
    sample.comparisonGain = tile->comparisonGain[index];
    sample.sampleFlags = tile->sampleFlags[index];
    return true;
}

bool PreparedTileSampler::SampleSameCfa(
    const Mfd::PreparedRawFrame& frame,
    const Mfd::NoiseModel& noiseModel,
    Mfd::RawCoordinate sourceRaw,
    Mfd::CfaSite site,
    const Mfd::SameCfaScalarParameters& parameters,
    Mfd::SameCfaSampleResult& result,
    CaptureFallback& fallback,
    std::string* error) {
    result = {};
    fallback = {};
    result.site = site;
    result.sourceRaw = sourceRaw;
    if (!std::isfinite(sourceRaw.x) || !std::isfinite(sourceRaw.y) ||
        noiseModel.quality == Mfd::NoiseModelQuality::Unavailable ||
        noiseModel.preparedFrameCacheKey != frame.cacheKey) return false;

    Mfd::CfaLayout layout;
    if (!Mfd::CfaLayout::TryCreate(frame.activeCfaPattern, layout))
        return false;
    const auto sourcePlane = layout.RawToPlane(sourceRaw, site);
    result.sourcePlane = sourcePlane;
    Mfd::KeysBicubicFootprint footprint;
    if (!BuildSupportedFootprint(sourcePlane,
            layout.PlaneExtent(site, frame.activeExtent),footprint)) return false;

    std::array<Mfd::SameCfaTapInput, Mfd::kSameCfaTapCount> taps;
    for (std::size_t index = 0; index < footprint.taps.size(); ++index) {
        const auto raw = layout.PlanePixelToRaw(footprint.taps[index]);
        if (!std::isfinite(raw.x) || !std::isfinite(raw.y) ||
            raw.x < 0 || raw.y < 0 ||
            raw.x >= static_cast<double>(frame.activeExtent.width) ||
            raw.y >= static_cast<double>(frame.activeExtent.height)) return false;
        const auto rawX = static_cast<std::uint64_t>(raw.x);
        const auto rawY = static_cast<std::uint64_t>(raw.y);
        if (layout.SiteAt(
                static_cast<std::int64_t>(rawX),
                static_cast<std::int64_t>(rawY)) != site) return false;
        if (!ReadSample(frame, rawX, rawY, taps[index], error)) {
            result.failure = Mfd::SameCfaSampleFailure::TileUnavailable;
            result.message = error && !error->empty() ? *error :
                "A prepared RAW footprint tile is unavailable.";
            return false;
        }
    }

    const auto siteIndex = static_cast<std::size_t>(site);
    Mfd::SameCfaSampleResult evaluated;
    if (siteIndex >= noiseModel.sites.size() ||
        !Mfd::EvaluateSameCfaFootprintScalar(
            footprint,
            taps,
            noiseModel.sites[siteIndex],
            frame.calibration.usableSpanByCfaSite[siteIndex],
            parameters,
            evaluated)) {
        fallback=InterpolateCaptureFallback(footprint,taps,parameters.exposureScale);
        return false;
    }
    evaluated.sourceRaw = sourceRaw;
    result = std::move(evaluated);
    return true;
}

} // namespace Raw::Bracketing
