#pragma once

#include <filesystem>

class EditorModule;

namespace Stack::Validation {
void ValidateOverlappingProjectFileOperations(
    EditorModule& first, EditorModule& second, const std::filesystem::path& scratch);
}
