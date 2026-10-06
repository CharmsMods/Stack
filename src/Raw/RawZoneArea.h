#pragma once

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/ImageGuidance.h"
#include <array>
#include <functional>
#include <memory>

namespace Stack::RawRecipe {

inline constexpr std::size_t kMaxZoneAreas = 32;
inline constexpr std::size_t kMaxZoneStrokes = 4096;
inline constexpr std::size_t kMaxZoneStrokePoints = 65536;
inline constexpr float kZoneMinimumEv = -32.0f;
inline constexpr float kZoneMaximumEv = 32.0f;

void SanitizeZoneAreas(std::vector<RawZoneArea>& areas);
nlohmann::json SerializeZoneAreas(const std::vector<RawZoneArea>& areas);
std::vector<RawZoneArea> DeserializeZoneAreas(const nlohmann::json& json);
bool HasZoneAreaGain(const RawLocalRangeRecipe& recipe);
std::size_t ZoneAreaMaskFingerprint(const RawZoneArea& area);
bool EqualZoneAreas(const std::vector<RawZoneArea>& a, const std::vector<RawZoneArea>& b);
std::size_t ZoneStrokeFingerprint(const RawZoneBrushStroke& stroke);
std::size_t ZoneAreaGainFingerprint(const RawZoneArea& area);
bool ZoneAreaUsesGuidance(const RawZoneArea& area);

// Input/output coordinates are top-down. Crop is applied after rotation/flips.
RawZoneBrushPoint ZoneAreaSourcePoint(float u, float v, const RawCropRotationRecipe& transform, bool cropped);
RawZoneBrushPoint ZoneAreaDisplayPoint(float u, float v, const RawCropRotationRecipe& transform, bool cropped);
float ZoneStrokeCoverage(const RawZoneBrushStroke& stroke, float aspect, float u, float v);
float ZoneAreaCoverage(const RawZoneArea& area, float u, float v);
std::vector<float> RasterizeZoneArea(const RawZoneArea& area, int width, int height,
    const RawCropRotationRecipe& transform, const std::function<bool()>& cancelled = {},
    const ImageGuide* guide = nullptr);
std::vector<float> RasterizeGuidedZoneArea(const RawZoneArea& area, int width, int height,
    const RawCropRotationRecipe& transform, const ImageGuide& guide,
    const std::function<bool()>& cancelled);

struct RawZoneAreaMaskPreview {
    int width = 0, height = 0;
    std::size_t maskFingerprint = 0;
    std::size_t strokeCount = 0, lastStrokePointCount = 0;
    // Bottom-up, before output crop, matching the renderer's mask texture.
    std::vector<float> coverage;
};
std::shared_ptr<const RawZoneAreaMaskPreview> MakeZoneAreaMaskPreview(
    const std::vector<float>& pixels, int width, int height, std::size_t fingerprint, int maxEdge = 768,
    const RawZoneArea* area = nullptr);

std::vector<RawBezierCurvePoint> ZoneAreaBezierPoints(const RawZoneArea& area,
    float minimumEv, float maximumEv, float minimumGain, float maximumGain);
void StoreZoneAreaBezierPoints(RawZoneArea& area, const std::vector<RawBezierCurvePoint>& points,
    float minimumEv, float maximumEv, float minimumGain, float maximumGain);
float ZoneAreaCurveGain(const RawZoneArea& area, float referenceEv);
float ZoneAreaReferenceEv(float luminance);

struct RawZoneAreaStatistics {
    std::string areaId;
    std::size_t maskFingerprint = 0;
    bool valid = false;
    bool fullResolution = false;
    bool hasBlack = false;
    float minimumEv = 0.0f;
    float maximumEv = 0.0f;
    // Fixed physical EV bins. UI can re-bin without modifying authored curves.
    std::array<float, 256> histogram {};
    std::shared_ptr<const RawZoneAreaMaskPreview> maskPreview;
};

} // namespace Stack::RawRecipe
