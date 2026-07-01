#include "EditorNodeGraphLutSerialization.h"

#include <cstddef>
#include <string>
#include <vector>

namespace EditorNodeGraph {
namespace {

std::string LutImportFormatToString(ColorLut::LutImportFormat format) {
    switch (format) {
        case ColorLut::LutImportFormat::Cube: return "Cube";
        case ColorLut::LutImportFormat::Format3dl: return "3dl";
        case ColorLut::LutImportFormat::Spi1d: return "Spi1d";
        case ColorLut::LutImportFormat::Spi3d: return "Spi3d";
        case ColorLut::LutImportFormat::Unknown:
        default:
            return "Unknown";
    }
}

ColorLut::LutImportFormat LutImportFormatFromString(const std::string& value) {
    if (value == "Cube") return ColorLut::LutImportFormat::Cube;
    if (value == "3dl" || value == "Format3dl") return ColorLut::LutImportFormat::Format3dl;
    if (value == "Spi1d") return ColorLut::LutImportFormat::Spi1d;
    if (value == "Spi3d") return ColorLut::LutImportFormat::Spi3d;
    return ColorLut::LutImportFormat::Unknown;
}

std::string LutUseModeToString(ColorLut::LutUseMode mode) {
    switch (mode) {
        case ColorLut::LutUseMode::PreViewTransform: return "PreViewTransform";
        case ColorLut::LutUseMode::PostViewTransform:
        default:
            return "PostViewTransform";
    }
}

ColorLut::LutUseMode LutUseModeFromString(const std::string& value) {
    if (value == "PreViewTransform") return ColorLut::LutUseMode::PreViewTransform;
    return ColorLut::LutUseMode::PostViewTransform;
}

std::string LutTransferFunctionToString(ColorLut::LutTransferFunction transform) {
    switch (transform) {
        case ColorLut::LutTransferFunction::SrgbEncode: return "SrgbEncode";
        case ColorLut::LutTransferFunction::Gamma22Encode: return "Gamma22Encode";
        case ColorLut::LutTransferFunction::SrgbDecode: return "SrgbDecode";
        case ColorLut::LutTransferFunction::Gamma22Decode: return "Gamma22Decode";
        case ColorLut::LutTransferFunction::None:
        default:
            return "None";
    }
}

ColorLut::LutTransferFunction LutTransferFunctionFromString(const std::string& value) {
    if (value == "SrgbEncode") return ColorLut::LutTransferFunction::SrgbEncode;
    if (value == "Gamma22Encode") return ColorLut::LutTransferFunction::Gamma22Encode;
    if (value == "SrgbDecode") return ColorLut::LutTransferFunction::SrgbDecode;
    if (value == "Gamma22Decode") return ColorLut::LutTransferFunction::Gamma22Decode;
    return ColorLut::LutTransferFunction::None;
}

nlohmann::json SerializeLut1DStage(const ColorLut::Lut1DStage& stage) {
    return {
        { "size", stage.size },
        { "values", stage.values },
        { "domainMin", { stage.domainMin[0], stage.domainMin[1], stage.domainMin[2] } },
        { "domainMax", { stage.domainMax[0], stage.domainMax[1], stage.domainMax[2] } }
    };
}

ColorLut::Lut1DStage DeserializeLut1DStage(const nlohmann::json& value) {
    ColorLut::Lut1DStage stage;
    stage.size = value.value("size", 0);
    stage.values = value.value("values", std::vector<float>{});
    const nlohmann::json domainMin = value.value("domainMin", nlohmann::json::array());
    const nlohmann::json domainMax = value.value("domainMax", nlohmann::json::array());
    for (int i = 0; i < 3; ++i) {
        stage.domainMin[static_cast<std::size_t>(i)] =
            domainMin.is_array() && domainMin.size() > static_cast<std::size_t>(i)
                ? domainMin[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMin[static_cast<std::size_t>(i)];
        stage.domainMax[static_cast<std::size_t>(i)] =
            domainMax.is_array() && domainMax.size() > static_cast<std::size_t>(i)
                ? domainMax[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMax[static_cast<std::size_t>(i)];
    }
    return stage;
}

nlohmann::json SerializeLut3DStage(const ColorLut::Lut3DStage& stage) {
    return {
        { "size", stage.size },
        { "values", stage.values },
        { "domainMin", { stage.domainMin[0], stage.domainMin[1], stage.domainMin[2] } },
        { "domainMax", { stage.domainMax[0], stage.domainMax[1], stage.domainMax[2] } }
    };
}

ColorLut::Lut3DStage DeserializeLut3DStage(const nlohmann::json& value) {
    ColorLut::Lut3DStage stage;
    stage.size = value.value("size", 0);
    stage.values = value.value("values", std::vector<float>{});
    const nlohmann::json domainMin = value.value("domainMin", nlohmann::json::array());
    const nlohmann::json domainMax = value.value("domainMax", nlohmann::json::array());
    for (int i = 0; i < 3; ++i) {
        stage.domainMin[static_cast<std::size_t>(i)] =
            domainMin.is_array() && domainMin.size() > static_cast<std::size_t>(i)
                ? domainMin[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMin[static_cast<std::size_t>(i)];
        stage.domainMax[static_cast<std::size_t>(i)] =
            domainMax.is_array() && domainMax.size() > static_cast<std::size_t>(i)
                ? domainMax[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMax[static_cast<std::size_t>(i)];
    }
    return stage;
}

} // namespace

nlohmann::json SerializeLutPayload(const ColorLut::LutPayload& payload) {
    return {
        { "sourcePath", payload.sourcePath },
        { "label", payload.label },
        { "importedTitle", payload.importedTitle },
        { "importError", payload.importError },
        { "importFormat", LutImportFormatToString(payload.importFormat) },
        { "useMode", LutUseModeToString(payload.useMode) },
        { "inputTransform", LutTransferFunctionToString(payload.inputTransform) },
        { "outputTransform", LutTransferFunctionToString(payload.outputTransform) },
        { "lut1D", SerializeLut1DStage(payload.lut1D) },
        { "shaper1D", SerializeLut1DStage(payload.shaper1D) },
        { "lut3D", SerializeLut3DStage(payload.lut3D) }
    };
}

ColorLut::LutPayload DeserializeLutPayload(const nlohmann::json& value) {
    ColorLut::LutPayload payload;
    payload.sourcePath = value.value("sourcePath", std::string());
    payload.label = value.value("label", std::string());
    payload.importedTitle = value.value("importedTitle", std::string());
    payload.importError = value.value("importError", std::string());
    payload.importFormat = LutImportFormatFromString(value.value("importFormat", std::string("Unknown")));
    const std::string serializedMode = value.value(
        "useMode",
        value.value("lookupMode", std::string("PostViewTransform")));
    payload.useMode = LutUseModeFromString(serializedMode);
    payload.inputTransform = LutTransferFunctionFromString(value.value("inputTransform", std::string("None")));
    payload.outputTransform = LutTransferFunctionFromString(value.value("outputTransform", std::string("None")));
    payload.lut1D = DeserializeLut1DStage(value.value("lut1D", nlohmann::json::object()));
    payload.shaper1D = DeserializeLut1DStage(value.value("shaper1D", nlohmann::json::object()));
    payload.lut3D = DeserializeLut3DStage(value.value("lut3D", nlohmann::json::object()));
    return payload;
}

} // namespace EditorNodeGraph
