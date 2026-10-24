#pragma once

#include <string>
#include <vector>

namespace Stack::EditorGraphCapture {
struct CaptureSaveJobResult {
    std::vector<unsigned char> pngBytes;
    std::string error;
    bool fileSaved = false;
};

// CPU-only worker boundary. Retain PNG bytes for clipboard delivery without
// making a second encoded copy just to save the PNG. All preparation failures
// return a result so the owner can complete its busy state on the main thread.
template<class EncodePng, class EncodeBmp, class Save>
CaptureSaveJobResult RunCaptureSaveJob(bool bmp, EncodePng encodePng,
    EncodeBmp encodeBmp, Save save) {
    CaptureSaveJobResult result;
    try {
        const bool pngEncoded = encodePng(result.pngBytes);
        std::vector<unsigned char> bmpBytes;
        const bool diskEncoded = bmp ? encodeBmp(bmpBytes) : pngEncoded;
        if (diskEncoded) {
            result.fileSaved = save(bmp ? bmpBytes : result.pngBytes, result.error);
        } else {
            result.error = bmp ? "BMP encoding failed." : "PNG encoding failed.";
        }
    } catch (...) {
        result.error = "Graph image encoding or saving failed unexpectedly.";
    }
    return result;
}
} // namespace Stack::EditorGraphCapture
