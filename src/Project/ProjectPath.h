#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace Stack::Project {

inline bool SameProjectPath(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) return false;
    const auto key = [](const std::filesystem::path& path) {
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(path, error);
        auto value = (error ? path.lexically_normal() : canonical).generic_wstring();
#if defined(_WIN32)
        std::transform(value.begin(), value.end(), value.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
#endif
        return value;
    };
    return key(a) == key(b);
}

} // namespace Stack::Project
