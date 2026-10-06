#pragma once

#include <filesystem>
#include <system_error>
#include <optional>

namespace Stack::FileSave {

enum class NormalizedTargetAction {
    Ready,
    ConfirmReplacement,
    Unavailable
};

// Permission is for the target observed when the user confirms. Recheck it
// immediately before writing so a delayed encoder cannot replace a new file.
struct TargetApproval {
    std::filesystem::path path;
    bool existed = false;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified {};
};

inline std::optional<TargetApproval> CaptureTargetApproval(
    const std::filesystem::path& target, std::error_code& error) {
    error.clear();
    if (target.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return std::nullopt;
    }
    TargetApproval approval;
    approval.path = target;
    approval.existed = std::filesystem::exists(target, error);
    if (error) return std::nullopt;
    if (approval.existed) {
        if (!std::filesystem::is_regular_file(target, error) || error) {
            if (!error) error = std::make_error_code(std::errc::is_a_directory);
            return std::nullopt;
        }
        approval.size = std::filesystem::file_size(target, error);
        if (error) return std::nullopt;
        approval.modified = std::filesystem::last_write_time(target, error);
        if (error) return std::nullopt;
    }
    return approval;
}

inline bool TargetApprovalStillMatches(const TargetApproval& approval, std::error_code& error) {
    const auto current = CaptureTargetApproval(approval.path, error);
    return current && current->existed == approval.existed &&
        (!approval.existed || (current->size == approval.size && current->modified == approval.modified));
}

// A native save dialog confirms only the path it returned. If the application
// later changes the extension, an existing different target needs its own
// confirmation. Equivalent paths include case aliases and hard links.
inline NormalizedTargetAction ReviewNormalizedTarget(
    const std::filesystem::path& selected,
    const std::filesystem::path& target,
    std::error_code& error) {
    error.clear();
    if (selected.empty() || target.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return NormalizedTargetAction::Unavailable;
    }
    if (selected.lexically_normal() == target.lexically_normal()) {
        return NormalizedTargetAction::Ready;
    }
    const bool targetExists = std::filesystem::exists(target, error);
    if (error) return NormalizedTargetAction::Unavailable;
    if (!targetExists) return NormalizedTargetAction::Ready;

    const bool selectedExists = std::filesystem::exists(selected, error);
    if (error) return NormalizedTargetAction::Unavailable;
    if (selectedExists) {
        const bool sameFile = std::filesystem::equivalent(selected, target, error);
        if (error) return NormalizedTargetAction::Unavailable;
        if (sameFile) return NormalizedTargetAction::Ready;
    }
    return NormalizedTargetAction::ConfirmReplacement;
}

} // namespace Stack::FileSave
