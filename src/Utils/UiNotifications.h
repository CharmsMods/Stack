#pragma once

#include <string>

enum class UiNotificationSeverity {
    Info,
    Success,
    Error
};

struct UiNotificationEvent {
    UiNotificationSeverity severity = UiNotificationSeverity::Info;
    std::string message;
    std::string dedupeKey;
};
