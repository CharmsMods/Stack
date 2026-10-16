#pragma once
#include "Raw/Bracketing/Processor.h"
#include <filesystem>
#include <functional>
#include <string>

namespace Stack::BurstEngine::Output {
enum class Format { Dng, Tiff16, Jpeg, Png };
struct Request {
    Format format = Format::Dng;
    // Desired filename in an existing directory. Existing files are never
    // replaced: publication chooses a numbered sibling atomically.
    std::filesystem::path destination;
    // Finished images use as-shot WB, shared RAW MHC/color development,
    // explicit exposure and sRGB encoding. DNG retains editable RAW samples;
    // exposureStops is a default-rendering metadata adjustment for DNG.
    float exposureStops = 0;
    unsigned jpegQuality = 95;
    std::uint64_t memoryBudgetBytes = 1024ull * 1024 * 1024;
    std::function<bool()> shouldCancel;
    std::function<void(double, const std::string&)> reportProgress;
};
struct Result {
    enum class Status { Completed, Canceled, Failed };
    Status status = Status::Failed;
    std::filesystem::path path;
    std::string message;
    unsigned width = 0, height = 0;
};
// Blocking worker-only operation. Standard Bayer results need their live,
// in-process prepared analysis so reference calibration is applied exactly
// once. A process-client lease or serialized mosaic alone is insufficient.
// No project, editor, active-tab or preview image dependency.
Result Write(const Raw::Bracketing::BracketingResult&, const Request&);
const char* Extension(Format);
}
