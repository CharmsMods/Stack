#pragma once

#include "Persistence/StackBinaryFormat.h"

namespace Stack::Project {

// Captured on the owning project thread, then moved to a writer. Pixel rows
// already have the orientation used by the project PNG. The writer never
// reads an editor, renderer, active tab, or live document.
struct ProjectSaveCapture {
    StackBinaryFormat::ProjectDocument document;
    std::vector<unsigned char> sourcePixels;
    std::vector<unsigned char> renderedPixels;
    int renderedWidth = 0;
    int renderedHeight = 0;
    std::uint64_t expectedStorageRevision = 0;
    bool includeLibraryPreview = false;
};

struct CapturedProjectSaveResult {
    StackBinaryFormat::ProjectWriteResult project;
    std::vector<unsigned char> libraryPreviewBytes;
    std::string previewWarning;
};

std::vector<unsigned char> EncodeProjectThumbnail(
    const std::vector<unsigned char>& rgba, int width, int height);

CapturedProjectSaveResult WriteCapturedProject(
    const std::filesystem::path& destination,
    ProjectSaveCapture capture,
    bool requireNewStore = false);

} // namespace Stack::Project
