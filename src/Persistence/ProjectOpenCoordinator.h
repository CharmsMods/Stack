#pragma once

#include "Persistence/LoadedProjectData.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Stack::Project {

enum class ProjectOpenFormat {
    CurrentBundle,
    PackedPortable,
    Unsupported
};

struct ProjectFormatProbe {
    ProjectOpenFormat format = ProjectOpenFormat::Unsupported;
    std::filesystem::path normalizedPath;
    bool supported = false;
    bool rawProject = false;
    bool multiFrameProject = false;
    std::string projectKind;
    std::string error;
};

struct ProjectOpenResult {
    ProjectFormatProbe probe;
    std::shared_ptr<LoadedProjectData> candidate;
    std::string warning;
    std::string error;

    explicit operator bool() const {
        return candidate != nullptr && error.empty();
    }
};

// Read-only format probing and candidate construction. This coordinator never
// mutates the active Editor session; callers may stage work on a background
// thread and swap the candidate only after their own validation succeeds.
class ProjectOpenCoordinator {
public:
    static ProjectFormatProbe Probe(const std::filesystem::path& path);
    static ProjectOpenResult Load(const std::filesystem::path& path);
};

} // namespace Stack::Project
