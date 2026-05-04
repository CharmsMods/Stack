#include "BundlerToolAdapter.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Subprocess execution
// ─────────────────────────────────────────────────────────────────────────────

SubprocessResult BundlerToolAdapter::RunCommand(const std::string& commandLine) {
    SubprocessResult result;

#ifdef _WIN32
    // Use CreateProcess with pipes for stdout/stderr
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
    HANDLE hStdErrRead = nullptr, hStdErrWrite = nullptr;

    CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0);
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0);
    SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdOutWrite;
    si.hStdError  = hStdErrWrite;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcessA needs a mutable string
    std::vector<char> cmdBuf(commandLine.begin(), commandLine.end());
    cmdBuf.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    // Close write ends so reads will terminate
    CloseHandle(hStdOutWrite);
    CloseHandle(hStdErrWrite);

    if (!ok) {
        result.exitCode = -1;
        result.stdErr = "Failed to create process: " + commandLine;
        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);
        return result;
    }

    // Read stdout
    {
        std::array<char, 4096> buf;
        DWORD bytesRead = 0;
        while (ReadFile(hStdOutRead, buf.data(), (DWORD)buf.size(), &bytesRead, nullptr) && bytesRead > 0) {
            result.stdOut.append(buf.data(), bytesRead);
        }
    }

    // Read stderr
    {
        std::array<char, 4096> buf;
        DWORD bytesRead = 0;
        while (ReadFile(hStdErrRead, buf.data(), (DWORD)buf.size(), &bytesRead, nullptr) && bytesRead > 0) {
            result.stdErr.append(buf.data(), bytesRead);
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdOutRead);
    CloseHandle(hStdErrRead);
#else
    // POSIX fallback using popen (stdout only)
    FILE* pipe = popen((commandLine + " 2>&1").c_str(), "r");
    if (!pipe) {
        result.exitCode = -1;
        result.stdErr = "popen failed for: " + commandLine;
        return result;
    }
    std::array<char, 4096> buf;
    while (fgets(buf.data(), (int)buf.size(), pipe)) {
        result.stdOut += buf.data();
    }
    result.exitCode = pclose(pipe);
#endif

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tool detection
// ─────────────────────────────────────────────────────────────────────────────

static ToolInfo ProbeToolOnPath(const std::string& name, const std::string& versionFlag) {
    ToolInfo info;
    info.name = name;

#ifdef _WIN32
    // Use `where` to find the tool
    auto whereResult = BundlerToolAdapter::RunCommand("where " + name + " 2>nul");
    if (whereResult.succeeded() && !whereResult.stdOut.empty()) {
        // First line is the path
        std::istringstream iss(whereResult.stdOut);
        std::getline(iss, info.path);
        // Trim trailing whitespace
        while (!info.path.empty() && (info.path.back() == '\r' || info.path.back() == '\n' || info.path.back() == ' '))
            info.path.pop_back();
        info.available = true;
    }
#else
    auto whichResult = BundlerToolAdapter::RunCommand("which " + name + " 2>/dev/null");
    if (whichResult.succeeded() && !whichResult.stdOut.empty()) {
        info.path = whichResult.stdOut;
        while (!info.path.empty() && (info.path.back() == '\r' || info.path.back() == '\n'))
            info.path.pop_back();
        info.available = true;
    }
#endif

    if (info.available && !versionFlag.empty()) {
        auto vResult = BundlerToolAdapter::RunCommand(name + " " + versionFlag);
        if (vResult.succeeded()) {
            info.version = vResult.stdOut;
            // Trim
            while (!info.version.empty() &&
                   (info.version.back() == '\r' || info.version.back() == '\n'))
                info.version.pop_back();
        }
    }

    return info;
}

void BundlerToolAdapter::DetectTools() {
    m_Esbuild      = ProbeToolOnPath("esbuild",      "--version");
    m_LightningCSS = ProbeToolOnPath("lightningcss",  "--version");
    m_Terser       = ProbeToolOnPath("terser",        "--version");

    // minify-html CLI may be installed as `minify-html` or `minhtml`
    m_MinifyHTML = ProbeToolOnPath("minify-html", "--version");
    if (!m_MinifyHTML.available) {
        m_MinifyHTML = ProbeToolOnPath("minhtml", "--version");
    }
}

bool BundlerToolAdapter::HasAnyTool() const {
    return m_Esbuild.available || m_LightningCSS.available ||
           m_MinifyHTML.available || m_Terser.available;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-file minification helpers
// ─────────────────────────────────────────────────────────────────────────────

bool BundlerToolAdapter::MinifyJS(const std::string& inPath, const std::string& outPath) const {
    if (m_Esbuild.available) {
        // esbuild --minify --outfile=out.js in.js
        std::string cmd = "esbuild --minify --outfile=\"" + outPath + "\" \"" + inPath + "\"";
        auto r = RunCommand(cmd);
        return r.succeeded();
    }
    if (m_Terser.available) {
        // terser in.js -o out.js --compress --mangle
        std::string cmd = "terser \"" + inPath + "\" -o \"" + outPath + "\" --compress --mangle";
        auto r = RunCommand(cmd);
        return r.succeeded();
    }
    return false;
}

bool BundlerToolAdapter::MinifyCSS(const std::string& inPath, const std::string& outPath) const {
    if (m_LightningCSS.available) {
        // lightningcss --minify -o out.css in.css
        std::string cmd = "lightningcss --minify -o \"" + outPath + "\" \"" + inPath + "\"";
        auto r = RunCommand(cmd);
        return r.succeeded();
    }
    if (m_Esbuild.available) {
        // esbuild also handles CSS minification
        std::string cmd = "esbuild --minify --outfile=\"" + outPath + "\" \"" + inPath + "\"";
        auto r = RunCommand(cmd);
        return r.succeeded();
    }
    return false;
}

bool BundlerToolAdapter::MinifyHTML(const std::string& inPath, const std::string& outPath) const {
    if (m_MinifyHTML.available) {
        // minify-html --output out.html in.html
        std::string cmd = m_MinifyHTML.name + " --output \"" + outPath + "\" \"" + inPath + "\"";
        auto r = RunCommand(cmd);
        return r.succeeded();
    }
    return false;
}
