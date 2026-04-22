#pragma once

namespace Async {

enum class TaskState : unsigned char {
    Idle = 0,
    Queued,
    Running,
    Applying,
    Failed
};

inline bool IsBusy(TaskState state) {
    return state == TaskState::Queued ||
           state == TaskState::Running ||
           state == TaskState::Applying;
}

inline const char* ToString(TaskState state) {
    switch (state) {
        case TaskState::Idle: return "Idle";
        case TaskState::Queued: return "Queued";
        case TaskState::Running: return "Running";
        case TaskState::Applying: return "Applying";
        case TaskState::Failed: return "Failed";
    }

    return "Unknown";
}

} // namespace Async
