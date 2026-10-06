#include "Raw/Internal/RawWorkspaceStorageIO.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Stack::RawWorkspace {
namespace StorageIO {
bool WriteJsonFile(
    const std::filesystem::path& path,
    const nlohmann::json& json,
    const PersistenceCommitPredicate& shouldCommit,
    std::string* outError);

bool WriteJsonFileWithIndent(
    const std::filesystem::path& path,
    const nlohmann::json& json,
    const PersistenceCommitPredicate& shouldCommit,
    std::string* outError,
    int indentation);

bool WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& json, std::string* outError) {
    return WriteJsonFile(path, json, nullptr, outError);
}

std::filesystem::path MakeTempPath(const std::filesystem::path& path) {
    const auto nowTicks = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::size_t threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::filesystem::path temp = path;
    temp += ".tmp.";
    temp += std::to_string(nowTicks);
    temp += ".";
    temp += std::to_string(threadHash);
    return temp;
}

bool ReplaceFileAtomically(
    const std::filesystem::path& temporaryPath,
    const std::filesystem::path& destinationPath,
    std::string* outError) {
#if defined(_WIN32)
    if (MoveFileExW(
            temporaryPath.c_str(),
            destinationPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    if (outError) {
        *outError = "Failed to atomically replace " +
            destinationPath.u8string() + ".";
    }
    return false;
#else
    std::error_code ec;
    std::filesystem::rename(temporaryPath, destinationPath, ec);
    if (!ec) {
        return true;
    }
    if (outError) {
        *outError = "Failed to atomically replace " +
            destinationPath.u8string() + ": " + ec.message();
    }
    return false;
#endif
}

bool WriteJsonFile(
    const std::filesystem::path& path,
    const nlohmann::json& json,
    const PersistenceCommitPredicate& shouldCommit,
    std::string* outError) {
    return WriteJsonFileWithIndent(
        path, json, shouldCommit, outError, 2);
}

bool WriteCompactJsonFile(
    const std::filesystem::path& path,
    const nlohmann::json& json,
    const PersistenceCommitPredicate& shouldCommit,
    std::string* outError) {
    return WriteJsonFileWithIndent(
        path, json, shouldCommit, outError, -1);
}

bool WriteJsonFileWithIndent(
    const std::filesystem::path& path,
    const nlohmann::json& json,
    const PersistenceCommitPredicate& shouldCommit,
    std::string* outError,
    int indentation) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create " + path.parent_path().u8string() + ": " + ec.message();
        }
        return false;
    }

    const std::filesystem::path tempPath = MakeTempPath(path);
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (outError) {
            *outError = "Failed to open " + tempPath.u8string() + " for writing.";
        }
        return false;
    }

    out << json.dump(indentation);
    if (!out.good()) {
        if (outError) {
            *outError = "Failed to write " + tempPath.u8string() + ".";
        }
        out.close();
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    out.close();
    if (!out.good()) {
        if (outError) {
            *outError = "Failed to close " + tempPath.u8string() + ".";
        }
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    if (shouldCommit && !shouldCommit()) {
        std::filesystem::remove(tempPath, ec);
        return true;
    }

    if (!ReplaceFileAtomically(tempPath, path, outError)) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    outJson = nlohmann::json::parse(in, nullptr, false);
    if (outJson.is_discarded() || !outJson.is_object()) {
        outJson = nlohmann::json::object();
        return false;
    }
    return true;
}


}
namespace {
std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}
std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

}
using namespace StorageIO;

ManagedLayout BuildManagedLayout(const std::filesystem::path& workspaceRoot) {
    ManagedLayout layout;
    layout.workspaceRoot = NormalizePath(workspaceRoot);
    layout.dataDirectory = layout.workspaceRoot / "Closet";
    layout.manifestPath = layout.dataDirectory / "workspace.json";
    layout.thumbnailsDirectory = layout.dataDirectory / "Thumbnails" / "Standard";
    layout.transientThumbnailsDirectory = layout.dataDirectory / "Thumbnails" / "Quick";
    layout.projectCoversDirectory = layout.dataDirectory / "Thumbnails" / "Project Covers";
    layout.projectsDirectory = layout.dataDirectory / "Projects";
    layout.catalogDirectory = layout.dataDirectory / "Catalog";
    layout.galleryPath = layout.catalogDirectory / "gallery.json";
    layout.sourceTrashDirectory = layout.dataDirectory / "Trash" / "Sources";
    layout.projectTrashDirectory = layout.dataDirectory / "Trash" / "Projects";
    layout.catalogPath = layout.catalogDirectory / kCatalogFileName;
    layout.ratingsPath = layout.catalogDirectory / kRatingsFileName;
    return layout;
}

bool IsManagedFolderName(const std::string& folderName) {
    const std::string lowered = ToLowerAscii(folderName);
    return lowered == ToLowerAscii(kThumbnailsFolderName) ||
        lowered == ToLowerAscii(kProjectsFolderName) ||
        lowered == ToLowerAscii(kCatalogFolderName) ||
        lowered == "trash";
}

bool EnsureManagedFolders(const std::filesystem::path& workspaceRoot, std::string* outError) {
    if (workspaceRoot.empty()) {
        if (outError) *outError = "No photo folder selected.";
        return false;
    }
    const ManagedLayout layout = BuildManagedLayout(workspaceRoot);
    std::error_code ec;
    if (!std::filesystem::is_directory(layout.workspaceRoot, ec) || ec) {
        if (outError) *outError = "The selected photo folder is unavailable.";
        return false;
    }
    const bool existed = std::filesystem::exists(layout.dataDirectory, ec);
    const bool initialized = std::filesystem::exists(layout.manifestPath, ec);
    if (initialized) {
        nlohmann::json manifest;
        if (!ReadJsonFile(layout.manifestPath, manifest) ||
            !manifest.contains("schema") || manifest["schema"] != "stack.rawWorkspace.closet" ||
            !manifest.contains("schemaVersion") || manifest["schemaVersion"] != 1) {
            if (outError) *outError = "Closet/workspace.json is damaged or unsupported.";
            return false;
        }
    } else if (existed && (!std::filesystem::is_directory(layout.dataDirectory, ec) ||
                          !std::filesystem::is_empty(layout.dataDirectory, ec))) {
        if (outError) *outError = "This folder already contains a Closet that is not a Stack workspace. Rename that Closet folder and try again.";
        return false;
    }
    const auto writable = [&](const std::filesystem::path& directory) {
        const auto probe = MakeTempPath(directory / ".stack-write-test");
        std::ofstream output(probe, std::ios::binary);
        output.put('1');
        output.close();
        const bool written = output.good();
        std::error_code probeError;
        const bool removed = std::filesystem::remove(probe, probeError);
        return written && removed && !probeError;
    };
    const auto failWritable = [&]() {
        if (outError) *outError = "This folder is not writable. Copy or move the photos to a writable location, then open that folder in Stack.";
        return false;
    };
    if (!writable(layout.workspaceRoot)) return failWritable();

    // Publish a complete new Closet in one rename. A failed initialization
    // only removes the unique temporary child created by this operation.
    const auto target = initialized ? layout.dataDirectory : MakeTempPath(layout.dataDirectory);
    const auto rollback = [&]() {
        if (!initialized && target.parent_path() == layout.workspaceRoot &&
            target != layout.dataDirectory) {
            std::error_code cleanup;
            std::filesystem::remove_all(target, cleanup);
        }
    };
    for (const auto& path : {layout.dataDirectory, layout.catalogDirectory,
            layout.thumbnailsDirectory, layout.projectCoversDirectory, layout.projectsDirectory}) {
        const auto destination = target / path.lexically_relative(layout.dataDirectory);
        std::filesystem::create_directories(destination, ec);
        if (ec || !writable(destination)) {
            rollback();
            return failWritable();
        }
    }
    if (!initialized) {
        const auto catalog = target / "Catalog";
        if (!WriteJsonFile(target / "workspace.json",
                {{"schema", "stack.rawWorkspace.closet"}, {"schemaVersion", 1}}, outError) ||
            !WriteJsonFile(catalog / "gallery.json",
                {{"schema", "stack.rawWorkspace.gallery"}, {"schemaVersion", 1},
                 {"grouping", SerializeRawGalleryManualGrouping({})}}, outError) ||
            !WriteJsonFile(catalog / "ratings.json",
                {{"schema", "stack.rawWorkspace.ratings"}, {"schemaVersion", 1},
                 {"ratings", nlohmann::json::object()}}, outError)) {
            rollback();
            return failWritable();
        }
        if (existed) {
            // remove() only accepts the still-empty original directory.
            std::filesystem::remove(layout.dataDirectory, ec);
            if (ec) { rollback(); return failWritable(); }
        }
        std::filesystem::rename(target, layout.dataDirectory, ec);
        if (ec) { rollback(); return failWritable(); }
    }
    return true;
}

bool LoadGalleryState(const ManagedLayout& layout, RawGalleryManualGrouping& grouping, std::string* error) {
    grouping = {};
    std::error_code ec;
    if (!std::filesystem::exists(layout.galleryPath, ec)) return !ec;
    nlohmann::json value;
    try {
    if (ReadJsonFile(layout.galleryPath, value) &&
        value.contains("schema") && value["schema"] == "stack.rawWorkspace.gallery" &&
        value.contains("schemaVersion") && value["schemaVersion"] == 1 &&
        value.contains("grouping") && value["grouping"].is_object() &&
        DeserializeRawGalleryManualGrouping(value["grouping"], grouping)) return true;
    } catch (const nlohmann::json::exception&) {
        grouping = {};
    }
    if (error) *error = "Closet/Catalog/gallery.json is damaged or unsupported.";
    return false;
}

bool SaveGalleryState(const ManagedLayout& layout, const RawGalleryManualGrouping& grouping, std::string* error) {
    return WriteJsonFile(layout.galleryPath,
        {{"schema", "stack.rawWorkspace.gallery"}, {"schemaVersion", 1},
         {"grouping", SerializeRawGalleryManualGrouping(grouping)}}, error);
}


} // namespace Stack::RawWorkspace
