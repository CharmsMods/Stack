#include "App/AppShell.h"
#include "Editor/LayerRegistry.h"
#include <cstring>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

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

    if (argc > 1 && std::strcmp(argv[1], "--validate-layer-registry") == 0) {
        std::vector<std::string> errors;
        if (!LayerRegistry::ValidateRegistry(&errors)) {
            for (const std::string& error : errors) {
                std::cerr << "LayerRegistry validation failed: " << error << std::endl;
            }
            return 2;
        }

        std::cout << "LayerRegistry validation passed." << std::endl;
        return 0;
    }

    AppShell app;
    
    std::cout << "Starting Modular Studio: Stack..." << std::endl;

    if (!app.Initialize("Modular Studio Stack", 1280, 800)) {
        std::cerr << "Failed to initialize Application Shell!" << std::endl;
        return -1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}
