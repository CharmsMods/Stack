#include "Editor/GraphCaptureSaveJob.h"
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
int main() try {
    using Stack::EditorGraphCapture::RunCaptureSaveJob;
    const unsigned char* encodedAddress = nullptr;
    const auto png = [&](auto& bytes) { bytes = {1,2,3}; encodedAddress = bytes.data(); return true; };
    const auto bmp = [](auto& bytes) { bytes = {4,5}; return true; };
    auto result = RunCaptureSaveJob(false, png, bmp, [&](const auto& bytes, auto&) {
        Require(bytes.data() == encodedAddress, "PNG save copied encoded bytes"); return true;
    });
    Require(result.fileSaved && result.pngBytes == std::vector<unsigned char>({1,2,3}), "PNG success lost clipboard data");
    result = RunCaptureSaveJob(true, png, bmp, [](const auto& bytes, auto&) { return bytes == std::vector<unsigned char>({4,5}); });
    Require(result.fileSaved && result.pngBytes.size() == 3, "BMP success lost PNG clipboard data");
    for (int stage = 0; stage < 4; ++stage) {
        result = RunCaptureSaveJob(stage == 1,
            [&](auto& bytes) { if (stage == 0) throw std::bad_alloc(); return png(bytes); },
            [&](auto& bytes) { if (stage == 1) throw std::runtime_error("encoder"); return bmp(bytes); },
            [&](const auto&, auto&) -> bool {
                if (stage == 2) throw std::runtime_error("path preparation");
                throw std::runtime_error("writer");
            });
        // The production caller unconditionally posts this result to its
        // owner-tracked main completion instead of throwing out of the worker.
        Require(!result.fileSaved && !result.error.empty(), "exception escaped worker result boundary");
    }
    result = RunCaptureSaveJob(false, png, bmp, [](const auto&, auto& error) { error = "cancelled"; return false; });
    Require(!result.fileSaved && result.error == "cancelled", "cancellation lost");
    bool saved = false;
    result = RunCaptureSaveJob(false, [](auto&) { return false; }, bmp,
        [&](const auto&, auto&) { saved = true; return true; });
    Require(!saved && !result.fileSaved && !result.error.empty(), "failed encoder attempted save");
    std::cout << "Graph capture save job success, no-copy PNG, cancel and exception completion passed.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
