#include "Editor/Internal/Project/ProjectLifecycleSupport.h"

#include <array>
#include <chrono>
#include <system_error>
#include <utility>
#include <vector>

namespace Stack::Editor::ProjectInternal {
namespace {

std::vector<unsigned char> MinimalTransparentPngBytes() {
    static constexpr std::array<unsigned char, 70> kPng = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x01, 0x63, 0x60, 0x60, 0x60, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x01, 0x0b, 0x0e, 0x0c,
        0x1a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
        0x44, 0xae, 0x42, 0x60, 0x82
    };
    return std::vector<unsigned char>(kPng.begin(), kPng.end());
}

} // namespace

std::int64_t RawWorkspaceProjectFileTimeTicks(
    const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(path, ec);
    return ec
        ? 0
        : std::chrono::duration_cast<std::chrono::microseconds>(
              writeTime.time_since_epoch())
              .count();
}

void EnsureMinimalProjectDocument(
    StackBinaryFormat::ProjectDocument& document,
    const std::string& name) {
    if (document.metadata.projectKind.empty()) {
        document.metadata.projectKind = StackBinaryFormat::kRawProjectKind;
    }
    if (document.metadata.projectName.empty()) {
        document.metadata.projectName =
            name.empty() ? "Untitled RAW Project" : name;
    }
    if (document.metadata.sourceWidth <= 0) {
        document.metadata.sourceWidth = 1;
    }
    if (document.metadata.sourceHeight <= 0) {
        document.metadata.sourceHeight = 1;
    }
    if (document.thumbnailBytes.empty()) {
        document.thumbnailBytes = MinimalTransparentPngBytes();
    }
    if (document.sourceImageBytes.empty()) {
        document.sourceImageBytes = MinimalTransparentPngBytes();
    }
}

void OverlayOwnedJsonFields(
    nlohmann::json& current,
    const nlohmann::json& owned) {
    if (!current.is_object() || !owned.is_object()) {
        current = owned;
        return;
    }

    for (auto it = owned.begin(); it != owned.end(); ++it) {
        auto currentIt = current.find(it.key());
        if (currentIt != current.end() &&
            currentIt->is_object() &&
            it->is_object()) {
            OverlayOwnedJsonFields(*currentIt, *it);
        } else {
            current[it.key()] = *it;
        }
    }
}

void ApplyRawWorkspaceProjectInfoToSource(
    RawWorkspace::SourceRecord& source,
    const StackBinaryFormat::ProjectDocument& document,
    const std::filesystem::path& projectPath,
    const std::filesystem::path& projectRelativePath,
    bool autosaved,
    bool dirty,
    std::string associationReason) {
    RawWorkspace::ProjectInfo info;
    if (!RawWorkspace::ReadProjectInfoFromDocument(document, info, nullptr)) {
        info = source.project;
        info.status = RawWorkspace::ProjectStatus::Invalid;
        if (info.errorMessage.empty()) {
            info.errorMessage =
                "Project does not contain RAW Workspace metadata.";
        }
    }

    info.absolutePath = projectPath.lexically_normal();
    info.relativePath = projectRelativePath.lexically_normal();
    info.projectModifiedTimeTicks =
        RawWorkspaceProjectFileTimeTicks(info.absolutePath);
    if (info.status != RawWorkspace::ProjectStatus::Invalid) {
        info.status = info.embeddedRaw
            ? RawWorkspace::ProjectStatus::Embedded
            : RawWorkspace::ProjectStatus::Existing;
    }
    info.autosaved = autosaved;
    info.dirty = dirty;
    info.associationReason = std::move(associationReason);
    source.project = std::move(info);
}

} // namespace Stack::Editor::ProjectInternal
