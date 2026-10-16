#pragma once
#include "Raw/RawImageData.h"
#include <array>
#include <string_view>

namespace Raw::Demosaic {
struct MethodInfo {
    DemosaicMethod method;
    const char* key;
    const char* label;
    const char* description;
};
// Stable keys are shared by recipes and graph source settings. The first four
// enum values retain their existing serialized/GPU identities.
inline constexpr std::array<MethodInfo, 20> Methods {{
    {DemosaicMethod::NearestNeighbor, "nearest-neighbor", "Nearest Neighbor", "Nearest recorded sample for each color. Fast, with visible block artifacts."},
    {DemosaicMethod::Bilinear, "bilinear", "Bilinear", "Averages neighboring samples. Fast and soft."},
    {DemosaicMethod::MalvarHeCutler, "malvar-he-cutler-5x5", "Malvar-He-Cutler 5x5", "Linear 5x5 reconstruction with color correction."},
    {DemosaicMethod::HamiltonAdams, "hamilton-adams", "Hamilton-Adams", "Chooses horizontal or vertical green reconstruction, then interpolates color differences."},
    {DemosaicMethod::EdgeWeighted, "edge-weighted", "Edge-weighted color difference", "Blends directional green estimates using edge strength; weights color differences by green similarity."},
    {DemosaicMethod::SmoothHue, "smooth-hue", "Smooth hue", "Interpolates color-to-green ratios, with color differences near black or negative samples."},
    {DemosaicMethod::Lmmse, "lmmse", "LMMSE (Zhang-Wu)", "Combines horizontal and vertical color estimates using their estimated uncertainty."},
    {DemosaicMethod::LmmseRefined, "lmmse-refined", "LMMSE + chroma refinement", "LMMSE followed by two median color-difference passes. Recorded CFA samples are preserved."},
    {DemosaicMethod::Dcb, "dcb", "DCB", "DCB reconstruction with three direction-map correction iterations."},
    {DemosaicMethod::DcbEnhanced, "dcb-enhanced", "DCB + refinement", "DCB with its additional green and color refinement passes."},
    {DemosaicMethod::MalvarHamilton, "malvar-hamilton", "MHC + Hamilton-Adams (edges)", "MHC in smooth areas; Hamilton-Adams at stronger edges, with a gradual transition."},
    {DemosaicMethod::HamiltonLmmse, "hamilton-lmmse", "Hamilton-Adams + LMMSE (detail)", "LMMSE in smooth areas; Hamilton-Adams where local detail increases."},
    {DemosaicMethod::DcbLmmseDetail, "dcb-lmmse-detail", "DCB + LMMSE (detail)", "LMMSE in smooth areas; refined DCB in detailed areas."},
    {DemosaicMethod::DcbLmmseShadows, "dcb-lmmse-shadows", "DCB + LMMSE (shadows)", "LMMSE in darker regions; refined DCB in brighter regions."},
    {DemosaicMethod::DcbLmmseChroma, "dcb-lmmse-chroma", "DCB + LMMSE (chroma)", "Green detail from refined DCB; red-green and blue-green differences from LMMSE."},
    {DemosaicMethod::DcbLmmseMalvar, "dcb-lmmse-malvar", "DCB + LMMSE + MHC", "LMMSE in dark or smooth areas, refined DCB for detail, and MHC near bright highlights."},
    {DemosaicMethod::Ppg, "ppg", "PPG", "Patterned Pixel Grouping. Directional green interpolation and diagonal color selection."},
    {DemosaicMethod::Vng, "vng", "VNG", "Variable Number of Gradients. Averages color differences along the locally accepted directions."},
    {DemosaicMethod::VngFourColor, "vng4", "VNG4", "Variable Number of Gradients with the two green sensor positions reconstructed separately."},
    {DemosaicMethod::Ahd, "ahd", "AHD", "Adaptive Homogeneity-Directed. Chooses horizontal or vertical reconstruction using perceptual color similarity."}
}};
inline const MethodInfo& Describe(DemosaicMethod method) {
    for (const auto& item : Methods) if (item.method == method) return item;
    return Methods[1];
}
inline DemosaicMethod FromKey(std::string_view key) {
    // Historical graph settings used these spellings.
    if (key == "MalvarHeCutler") return DemosaicMethod::MalvarHeCutler;
    if (key == "NearestNeighbor") return DemosaicMethod::NearestNeighbor;
    if (key == "HamiltonAdams") return DemosaicMethod::HamiltonAdams;
    for (const auto& item : Methods) if (key == item.key) return item.method;
    return DemosaicMethod::Bilinear;
}
inline bool UsesMultipassGpu(DemosaicMethod method) {
    return static_cast<int>(method) >= static_cast<int>(DemosaicMethod::HamiltonAdams);
}
} // namespace Raw::Demosaic
