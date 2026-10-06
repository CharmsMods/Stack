#include "Persistence/ProjectSaveCapture.h"
#include "Utils/PngEncodingUtils.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Stack::Project {

std::vector<unsigned char> EncodeProjectThumbnail(
    const std::vector<unsigned char>& rgba, int width, int height) {
    if (!PixelBuffer::HasCompletePixelBuffer(rgba.size(), width, height, 4)) return {};
    constexpr int maximumDimension = 400;
    if (width <= maximumDimension && height <= maximumDimension) {
        return PngEncoding::EncodeInterleaved(rgba, width, height, 4);
    }
    const int thumbnailWidth = width > height ? maximumDimension :
        std::max(1, static_cast<int>((static_cast<float>(width) / height) * maximumDimension));
    const int thumbnailHeight = height >= width ? maximumDimension :
        std::max(1, static_cast<int>((static_cast<float>(height) / width) * maximumDimension));
    std::vector<unsigned char> thumbnail(
        static_cast<std::size_t>(thumbnailWidth) * thumbnailHeight * 4u);
    for (int y = 0; y < thumbnailHeight; ++y) {
        for (int x = 0; x < thumbnailWidth; ++x) {
            const auto sourceX = static_cast<std::size_t>(x) * width / thumbnailWidth;
            const auto sourceY = static_cast<std::size_t>(y) * height / thumbnailHeight;
            const auto sourceOffset = (sourceY * width + sourceX) * 4u;
            const auto targetOffset = (static_cast<std::size_t>(y) * thumbnailWidth + x) * 4u;
            std::copy_n(rgba.data() + sourceOffset, 4u, thumbnail.data() + targetOffset);
        }
    }
    return PngEncoding::EncodeInterleaved(thumbnail, thumbnailWidth, thumbnailHeight, 4);
}

CapturedProjectSaveResult WriteCapturedProject(
    const std::filesystem::path& destination,
    ProjectSaveCapture capture,
    bool requireNewStore) {
    CapturedProjectSaveResult result;
    try {
        auto& document = capture.document;
        if (document.sourceImageBytes.empty()) {
            document.sourceImageBytes = PngEncoding::EncodeInterleaved(capture.sourcePixels,
                document.metadata.sourceWidth, document.metadata.sourceHeight, 4);
        }
        if (document.sourceImageBytes.empty()) {
            throw std::runtime_error("Failed to encode the project source image.");
        }
        if (document.thumbnailBytes.empty()) {
            document.thumbnailBytes = EncodeProjectThumbnail(capture.renderedPixels,
                capture.renderedWidth, capture.renderedHeight);
        }
        result.project = StackBinaryFormat::WriteProjectFileWithResult(destination,
            document, requireNewStore, capture.expectedStorageRevision);
        if (result.project && capture.includeLibraryPreview) {
            result.libraryPreviewBytes = PngEncoding::EncodeInterleaved(capture.renderedPixels,
                capture.renderedWidth, capture.renderedHeight, 4);
            if (result.libraryPreviewBytes.empty()) {
                result.previewWarning = "the preview image could not be encoded";
            }
        }
    } catch (const std::exception& error) {
        if (result.project) result.previewWarning = error.what();
        else result.project.commit.message = error.what();
    } catch (...) {
        if (result.project) result.previewWarning = "the preview image could not be encoded";
        else result.project.commit.message = "An unknown error interrupted the project write.";
    }
    return result;
}

} // namespace Stack::Project
