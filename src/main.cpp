#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

#include "App/AppShell.h"
#include "App/AppPaths.h"
#include "App/Validation/ValidationCommandRunner.h"

#include <iostream>
#include <cstring>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>

namespace {

#if defined(_WIN32)
void DiagnosticTerminateHandler() noexcept {
    std::ostringstream trace;
    trace << "[ProjectOpenDiagnostic] std::terminate invoked\n";
    if (const std::exception_ptr exception = std::current_exception()) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& error) {
            trace << "exception: " << error.what() << '\n';
        } catch (...) {
            trace << "exception: non-standard exception\n";
        }
    } else {
        trace << "exception: none\n";
    }

    void* frames[96] {};
    const USHORT frameCount = CaptureStackBackTrace(
        0,
        static_cast<DWORD>(std::size(frames)),
        frames,
        nullptr);
    const std::uintptr_t moduleBase = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    for (USHORT index = 0; index < frameCount; ++index) {
        const std::uintptr_t address =
            reinterpret_cast<std::uintptr_t>(frames[index]);
        trace << "frame[" << index << "] address=0x" << std::hex
              << address;
        if (moduleBase != 0 && address >= moduleBase) {
            trace << " rva=0x" << (address - moduleBase);
        }
        trace << std::dec << '\n';
    }

    const std::string text = trace.str();
    std::cerr << text << std::flush;
    try {
        std::ofstream output(
            AppPaths::GetLogsDirectory() / "ProjectOpenTerminateTrace.log",
            std::ios::app);
        output << text << '\n';
    } catch (...) {
    }
    std::abort();
}

bool IsCommandLineInvocation(int argc, char** argv) {
    return argc > 1 && argv != nullptr && argv[1] != nullptr &&
        argv[1][0] == '-';
}

void AttachParentConsoleForCommandOutput() {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    const bool outputAvailable = output != nullptr && output != INVALID_HANDLE_VALUE;
    const bool errorAvailable = error != nullptr && error != INVALID_HANDLE_VALUE;
    if (outputAvailable && errorAvailable) {
        return;
    }

    const BOOL attached = AttachConsole(ATTACH_PARENT_PROCESS);
    if (!attached && GetLastError() != ERROR_ACCESS_DENIED) {
        return;
    }

    FILE* stream = nullptr;
    if (!outputAvailable) {
        freopen_s(&stream, "CONOUT$", "w", stdout);
    }
    if (!errorAvailable) {
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}

std::string Utf8FromWide(const wchar_t* text) {
    if (text == nullptr || text[0] == L'\0') {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text, -1, utf8.data(), required, nullptr, nullptr);
    utf8.resize(static_cast<std::size_t>(required - 1));
    return utf8;
}
#endif

} // namespace

int RunStackApplication(int argc, char** argv) {
    try {
        AppPaths::MigrateLegacyPortableDataIfNeeded();
        AppPaths::EnsureRuntimeDirectories();

        int validationExitCode = 0;
        if (TryRunValidationCommand(argc, argv, validationExitCode)) {
            return validationExitCode;
        }

        const bool singleProjectOpenDiagnostic = argc == 3 &&
            std::strcmp(argv[1], "--diagnose-project-open") == 0;
        const bool projectSwitchDiagnostic = argc == 4 &&
            std::strcmp(argv[1], "--diagnose-project-switch") == 0;
        const bool projectOpenDiagnostic =
            singleProjectOpenDiagnostic || projectSwitchDiagnostic;
        const bool queueExportDiagnostic = argc == 4 &&
            std::strcmp(argv[1], "--diagnose-queue-export") == 0;
        const bool queueSourceExportDiagnostic = argc == 4 &&
            std::strcmp(argv[1], "--diagnose-queue-source") == 0;
        const bool galleryInspectionDiagnostic = argc == 4 &&
            std::strcmp(argv[1],
                "--diagnose-gallery-inspection") == 0;
#if defined(_WIN32)
        if (projectOpenDiagnostic || queueExportDiagnostic ||
            queueSourceExportDiagnostic || galleryInspectionDiagnostic) {
            std::set_terminate(DiagnosticTerminateHandler);
        }
#endif

        const bool workspaceDiagnostic = argc == 3 && std::strcmp(argv[1], "--diagnose-workspace-switcher") == 0;
        AppShell app;
        if (workspaceDiagnostic) app.ConfigureDiagnosticWorkspaceSwitcher(argv[2]);
        if (projectOpenDiagnostic) {
            app.ConfigureDiagnosticProjectOpen(
                argv[2],
                projectSwitchDiagnostic
                    ? std::filesystem::path(argv[3])
                    : std::filesystem::path());
        }
        if (queueExportDiagnostic || queueSourceExportDiagnostic) {
            app.ConfigureDiagnosticQueueExport(
                argv[2], argv[3], queueSourceExportDiagnostic);
        }
        if (galleryInspectionDiagnostic) {
            app.ConfigureDiagnosticGalleryInspection(argv[2], argv[3]);
        }

        std::cout << "Starting Modular Studio: Stack..." << std::endl;

        if (!app.Initialize("Stack", 1280, 800)) {
            std::cerr << "Failed to initialize Application Shell!" << std::endl;
            return -1;
        }

        app.Run();
        const bool diagnosticSucceeded =
            (!workspaceDiagnostic || app.WasDiagnosticWorkspaceSwitcherSuccessful()) &&
            (!projectOpenDiagnostic ||
             app.WasDiagnosticProjectOpenSuccessful()) &&
            (!(queueExportDiagnostic || queueSourceExportDiagnostic) ||
             app.WasDiagnosticQueueExportSuccessful()) &&
            (!galleryInspectionDiagnostic ||
             app.WasDiagnosticGalleryInspectionSuccessful());
        app.Shutdown();

        return diagnosticSucceeded ? 0 : 67;
    } catch (const std::exception& error) {
        std::cerr << "Stack encountered a fatal error: " << error.what() << std::endl;
        try {
            std::error_code ec;
            std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
            std::ofstream log(AppPaths::GetStartupLogPath(), std::ios::app);
            log << "Fatal exception: " << error.what() << '\n';
        } catch (...) {
        }
        return -1;
    } catch (...) {
        std::cerr << "Stack encountered an unknown fatal error." << std::endl;
        try {
            std::error_code ec;
            std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
            std::ofstream log(AppPaths::GetStartupLogPath(), std::ios::app);
            log << "Fatal exception: unknown\n";
        } catch (...) {
        }
        return -1;
    }
}

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* wideArgs = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wideArgs == nullptr || argc <= 0) {
        char programName[] = "Stack";
        char* fallbackArgs[] = { programName };
        return RunStackApplication(1, fallbackArgs);
    }

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.push_back(Utf8FromWide(wideArgs[index]));
    }
    LocalFree(wideArgs);

    std::vector<char*> rawArguments;
    rawArguments.reserve(arguments.size());
    for (std::string& argument : arguments) {
        rawArguments.push_back(argument.data());
    }

    if (IsCommandLineInvocation(argc, rawArguments.data())) {
        AttachParentConsoleForCommandOutput();
    }
    return RunStackApplication(argc, rawArguments.data());
}
#else
int main(int argc, char** argv) {
    return RunStackApplication(argc, argv);
}
#endif
