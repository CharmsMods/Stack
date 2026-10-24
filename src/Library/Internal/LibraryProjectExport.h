#pragma once
#include <filesystem>
#include <functional>
#include <string>

namespace Stack::Library {
// Export the saved project and its managed dependencies, without rebinding it.
bool ExportPortableProject(const std::filesystem::path& source,
    const std::filesystem::path& destination, std::string& error,
    const std::function<bool()>& isCanceled = {});
}
