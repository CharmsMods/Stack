#include "Library/Internal/LibraryStorageHelpers.h"
#include "Utils/AtomicExportFile.h"

namespace Stack::Library::StorageHelpers {

bool WriteFileBytes(const std::filesystem::path& path,
    const std::vector<unsigned char>& bytes) {
    // This helper writes Library raster assets and derived previews, not
    // arbitrary files. Keep the format boundary independent of the supplied
    // filename, so a camera original can never become a write destination.
    static const std::vector<std::string> extensions {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga"
    };
    FileSave::ExportFileTarget target;
    std::string error;
    // Callers still own collision decisions. This snapshot only protects the
    // target observed at invocation from changes during staging/publication.
    // Atomic replacement also leaves an original inode behind a hardlink
    // untouched, and the shared writer rejects symbolic-link destinations.
    return FileSave::PrepareExportFileTarget(path, {}, extensions, target, error) &&
        FileSave::WriteExportFileAtomically(target, bytes, error);
}

} // namespace Stack::Library::StorageHelpers
