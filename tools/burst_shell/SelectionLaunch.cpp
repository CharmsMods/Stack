#include "SelectionLaunch.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Stack::BurstShell {
namespace {
struct ComMemoryDeleter { void operator()(wchar_t* value) const { CoTaskMemFree(value); } };
struct HandleDeleter { void operator()(void* value) const { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
using Handle = std::unique_ptr<void, HandleDeleter>;

HRESULT LastErrorResult() { const auto error = GetLastError(); return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE); }

std::string Utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid Unicode selection path.");
    std::string result(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr))
        throw std::runtime_error("Could not encode selection path.");
    result.pop_back();
    return result;
}

void AppendJsonPath(std::string& json, const wchar_t* value) {
    const auto path = Utf8(value);
    constexpr char hex[] = "0123456789abcdef";
    json += '"';
    for (const unsigned char character : path) {
        if (character == '"' || character == '\\') { json += '\\'; json += character; }
        else if (character < 0x20) { json += "\\u00"; json += hex[character >> 4]; json += hex[character & 15]; }
        else json += character;
    }
    json += '"';
}

// Windows command-line quoting, including backslashes immediately before a quote.
std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (character == L'"') result += L'\\';
        result += character;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

struct SelectionFile {
    std::filesystem::path directory;
    bool transferred = false;
    ~SelectionFile() {
        if (!directory.empty() && !transferred) {
            // Never recurse: only the file and empty directory created here.
            DeleteFileW((directory / L"selection.json").c_str());
            RemoveDirectoryW(directory.c_str());
        }
    }
};
}

std::wstring ModulePath(HMODULE module) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("Could not locate the Burst shell component.");
    return {buffer.data(), length};
}

HRESULT LaunchSelection(HMODULE module, IShellItemArray* selection) noexcept {
    try {
        if (!selection) return E_INVALIDARG;
        DWORD count = 0;
        HRESULT result = selection->GetCount(&count);
        if (FAILED(result)) return result;
        if (count < 2 || count > MaximumSelectionFiles) return E_INVALIDARG;

        std::string document = "{\"schemaVersion\":1,\"files\":[";
        for (DWORD index = 0; index < count; ++index) {
            IShellItem* item = nullptr;
            result = selection->GetItemAt(index, &item);
            if (FAILED(result)) return result;
            wchar_t* path = nullptr;
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            item->Release();
            std::unique_ptr<wchar_t, ComMemoryDeleter> pathOwner(path);
            if (FAILED(result)) return result;
            if (index) document += ',';
            AppendJsonPath(document, path);
            if (document.size() > MaximumSelectionBytes - 2) return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
        document += "]}";

        // DLL lives at <installation>/Stack/App/Runtime. Never discover another
        // installation's worker, depend on the current directory, or use PATH.
        const auto runtime = std::filesystem::path(ModulePath(module)).parent_path();
        const auto install = runtime.parent_path().parent_path().parent_path();
        const auto executable = install / L"StackBurst.exe";
        std::vector<wchar_t> temporary(32768);
        const DWORD tempLength = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        if (!tempLength || tempLength >= temporary.size()) return LastErrorResult();
        const auto selections = std::filesystem::path(temporary.data()) / L"StackBurst" / L"Selections";
        std::filesystem::create_directories(selections);
        GUID id{};
        result = CoCreateGuid(&id);
        if (FAILED(result)) return result;
        wchar_t guid[40]{};
        if (!StringFromGUID2(id, guid, 40)) return E_FAIL;
        // Plain canonical GUID spelling without braces is shared with the UI reader.
        const auto directory = selections / std::wstring(guid + 1, 36);
        if (!CreateDirectoryW(directory.c_str(), nullptr)) return LastErrorResult();
        SelectionFile owned{directory};
        const auto manifest = directory / L"selection.json";
        Handle file(CreateFileW(manifest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE) return LastErrorResult();
        DWORD written = 0;
        if (!WriteFile(file.get(), document.data(), static_cast<DWORD>(document.size()), &written, nullptr)) return LastErrorResult();
        if (written != document.size()) return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        file.reset();

        auto command = QuoteArgument(executable.wstring()) + L" --selection " + QuoteArgument(manifest.wstring());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, install.c_str(), &startup, &process)) return LastErrorResult();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        owned.transferred = true; // UI deletes this exact owned manifest after admission.
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_FAIL; }
}
}
