#include "Utils/ImageClipboard.h"

#include <cstdint>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ImageClipboard {

#if defined(_WIN32)
namespace {

HGLOBAL AllocateGlobalBytes(std::size_t size) {
    if (size == 0 || size > static_cast<std::size_t>(std::numeric_limits<SIZE_T>::max())) {
        return nullptr;
    }
    return GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(size));
}

std::string LastErrorMessage(const char* prefix) {
    return std::string(prefix ? prefix : "Clipboard operation failed") +
        " (Windows error " + std::to_string(static_cast<unsigned long>(GetLastError())) + ").";
}

} // namespace
#endif

PublishResult PublishRgbaImage(
    void* ownerWindow,
    const std::vector<unsigned char>& topLeftStraightRgba,
    int width,
    int height,
    const std::vector<unsigned char>& pngBytes) {
    PublishResult result;
#if defined(_WIN32)
    if (width <= 0 || height <= 0) {
        result.error = "Clipboard image dimensions are invalid.";
        return result;
    }
    const std::uint64_t pixelBytes64 =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
    if (pixelBytes64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        topLeftStraightRgba.size() != static_cast<std::size_t>(pixelBytes64)) {
        result.error = "Clipboard image pixels are incomplete.";
        return result;
    }

    const std::size_t pixelBytes = static_cast<std::size_t>(pixelBytes64);
    const std::size_t dibBytes = sizeof(BITMAPV5HEADER) + pixelBytes;
    HGLOBAL dibHandle = AllocateGlobalBytes(dibBytes);
    HGLOBAL pngHandle = pngBytes.empty() ? nullptr : AllocateGlobalBytes(pngBytes.size());
    if (!dibHandle || (!pngBytes.empty() && !pngHandle)) {
        if (dibHandle) GlobalFree(dibHandle);
        if (pngHandle) GlobalFree(pngHandle);
        result.error = "Windows could not allocate enough clipboard memory for the graph image.";
        return result;
    }

    auto* dibMemory = static_cast<unsigned char*>(GlobalLock(dibHandle));
    if (!dibMemory) {
        GlobalFree(dibHandle);
        if (pngHandle) GlobalFree(pngHandle);
        result.error = LastErrorMessage("Windows could not lock the graph clipboard bitmap");
        return result;
    }
    std::memset(dibMemory, 0, dibBytes);
    auto* header = reinterpret_cast<BITMAPV5HEADER*>(dibMemory);
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = width;
    header->bV5Height = -height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(pixelBytes);
    header->bV5RedMask = 0x00ff0000u;
    header->bV5GreenMask = 0x0000ff00u;
    header->bV5BlueMask = 0x000000ffu;
    header->bV5AlphaMask = 0xff000000u;
    header->bV5CSType = LCS_sRGB;
    header->bV5Intent = LCS_GM_IMAGES;

    unsigned char* bgra = dibMemory + sizeof(BITMAPV5HEADER);
    for (std::size_t index = 0; index < pixelBytes; index += 4) {
        bgra[index + 0] = topLeftStraightRgba[index + 2];
        bgra[index + 1] = topLeftStraightRgba[index + 1];
        bgra[index + 2] = topLeftStraightRgba[index + 0];
        bgra[index + 3] = topLeftStraightRgba[index + 3];
    }
    GlobalUnlock(dibHandle);

    if (pngHandle) {
        void* pngMemory = GlobalLock(pngHandle);
        if (!pngMemory) {
            GlobalFree(dibHandle);
            GlobalFree(pngHandle);
            result.error = LastErrorMessage("Windows could not lock the PNG clipboard data");
            return result;
        }
        std::memcpy(pngMemory, pngBytes.data(), pngBytes.size());
        GlobalUnlock(pngHandle);
    }

    if (!OpenClipboard(static_cast<HWND>(ownerWindow))) {
        GlobalFree(dibHandle);
        if (pngHandle) GlobalFree(pngHandle);
        result.error = LastErrorMessage("Windows could not open the clipboard");
        return result;
    }

    if (!EmptyClipboard()) {
        result.error = LastErrorMessage("Windows could not clear the clipboard");
        CloseClipboard();
        GlobalFree(dibHandle);
        if (pngHandle) GlobalFree(pngHandle);
        return result;
    }

    if (SetClipboardData(CF_DIBV5, dibHandle)) {
        result.dibV5Published = true;
        dibHandle = nullptr;
    } else {
        result.error = LastErrorMessage("Windows could not publish the DIBV5 clipboard image");
    }

    if (pngHandle) {
        const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
        if (pngFormat != 0 && SetClipboardData(pngFormat, pngHandle)) {
            result.pngPublished = true;
            pngHandle = nullptr;
        } else if (result.error.empty()) {
            result.error = LastErrorMessage("Windows could not publish the PNG clipboard image");
        }
    }

    CloseClipboard();
    if (dibHandle) GlobalFree(dibHandle);
    if (pngHandle) GlobalFree(pngHandle);
    if (result.dibV5Published && result.pngPublished) {
        result.error.clear();
    }
#else
    (void)ownerWindow;
    (void)topLeftStraightRgba;
    (void)width;
    (void)height;
    (void)pngBytes;
    result.error = "Image clipboard publishing is only available on Windows.";
#endif
    return result;
}

} // namespace ImageClipboard
