#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "App/AppShell.h"
#include "App/AppPaths.h"
#include "App/Validation/ValidationCommandRunner.h"

#include <filesystem>
#include <iostream>

namespace {

void SetWorkingDirectoryToExecutableFolder() {
#ifdef _WIN32
    char modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path executablePath(modulePath);
    if (executablePath.has_parent_path()) {
        std::filesystem::current_path(executablePath.parent_path(), ec);
    }
#endif
}

} // namespace

int main(int argc, char** argv) {
    SetWorkingDirectoryToExecutableFolder();
    AppPaths::EnsureRuntimeDirectories();
    AppPaths::MigrateLegacyPortableDataIfNeeded();

    int validationExitCode = 0;
    if (TryRunValidationCommand(argc, argv, validationExitCode)) {
        return validationExitCode;
    }

    AppShell app;

    std::cout << "Starting Modular Studio: Stack..." << std::endl;

    if (!app.Initialize("Stack", 1280, 800)) {
        std::cerr << "Failed to initialize Application Shell!" << std::endl;
        return -1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}
