#include "Restormer/RestormerClient.h"

#include "App/AppPaths.h"
#include "ThirdParty/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef STACK_ENABLE_RESTORMER_EXECUTION
#define STACK_ENABLE_RESTORMER_EXECUTION 0
#endif

namespace Stack::Restormer {
namespace {

std::string ValidationCacheKey(
    const std::filesystem::path& root,
    const RawRecipe::RawRgbDenoiseRecipe& settings) {
    std::ostringstream key;
    key << root.generic_string() << '|'
        << RawRecipe::RgbDenoiseMethodStableString(settings.method) << '|'
        << settings.packageVersion << '|'
        << settings.modelSha256 << '|'
        << settings.adapterVersion;
    return key.str();
}

#if defined(_WIN32)
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length) != length) {
        return {};
    }
    return result;
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned int backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

bool ReadExact(HANDLE pipe, void* bytes, DWORD size) {
    auto* destination = static_cast<unsigned char*>(bytes);
    DWORD total = 0;
    while (total < size) {
        DWORD read = 0;
        if (!ReadFile(pipe, destination + total, size - total, &read, nullptr) ||
            read == 0) {
            return false;
        }
        total += read;
    }
    return true;
}

bool WriteExact(HANDLE pipe, const void* bytes, DWORD size) {
    const auto* source = static_cast<const unsigned char*>(bytes);
    DWORD total = 0;
    while (total < size) {
        DWORD written = 0;
        if (!WriteFile(pipe, source + total, size - total, &written, nullptr) ||
            written == 0) {
            return false;
        }
        total += written;
    }
    return true;
}

bool SendRequest(HANDLE pipe, const ProtocolRequest& request) {
    const std::string bytes = SerializeProtocolRequest(request).dump();
    if (bytes.empty() || bytes.size() > 1024U * 1024U) {
        return false;
    }
    const std::uint32_t length = static_cast<std::uint32_t>(bytes.size());
    return WriteExact(pipe, &length, sizeof(length)) &&
        WriteExact(pipe, bytes.data(), length);
}

bool ReadResponse(HANDLE pipe, ProtocolResponse& response, std::string& error) {
    std::uint32_t length = 0;
    if (!ReadExact(pipe, &length, sizeof(length)) ||
        length == 0 || length > 1024U * 1024U) {
        error = "StackModelService closed its control pipe.";
        return false;
    }
    std::string bytes(length, '\0');
    if (!ReadExact(pipe, bytes.data(), length)) {
        error = "StackModelService returned a truncated response.";
        return false;
    }
    const nlohmann::json value =
        nlohmann::json::parse(bytes, nullptr, false);
    if (value.is_discarded() ||
        !ParseProtocolResponse(value, response, error)) {
        if (error.empty()) {
            error = "StackModelService returned invalid JSON.";
        }
        return false;
    }
    return true;
}

class SharedMapping {
public:
    ~SharedMapping() {
        if (m_Data != nullptr) {
            UnmapViewOfFile(m_Data);
        }
        if (m_Handle != nullptr) {
            CloseHandle(m_Handle);
        }
    }

    bool Create(const std::string& name, std::size_t byteSize) {
        if (byteSize == 0 ||
            byteSize > static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }
        const std::wstring wideName = Utf8ToWide(name);
        const std::uint64_t size = static_cast<std::uint64_t>(byteSize);
        m_Handle = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            static_cast<DWORD>(size >> 32U),
            static_cast<DWORD>(size & 0xffffffffULL),
            wideName.c_str());
        if (m_Handle == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
            return false;
        }
        m_Data = MapViewOfFile(
            m_Handle, FILE_MAP_ALL_ACCESS, 0, 0, byteSize);
        return m_Data != nullptr;
    }

    float* Data() const {
        return static_cast<float*>(m_Data);
    }

private:
    HANDLE m_Handle = nullptr;
    void* m_Data = nullptr;
};
#endif

} // namespace

struct Client::Impl {
    std::mutex mutex;
    std::string validationKey;
    std::filesystem::file_time_type validationManifestWriteTime {};
    ValidationResult validation;
#if defined(_WIN32)
    HANDLE process = nullptr;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string runningManifestSha256;
    std::string token;
    std::uint64_t requestSerial = 0;
#endif
};

Client& Client::Instance() {
    static Client client;
    return client;
}

bool IsExecutionEnabled() {
    return STACK_ENABLE_RESTORMER_EXECUTION != 0;
}

Client::~Client() {
    Shutdown();
    delete m_Impl;
    m_Impl = nullptr;
}

ValidationResult Client::Validate(
    const RawRecipe::RawRgbDenoiseRecipe& requestedSettings,
    bool allowUnpinnedDevelopmentSelection) {
    if (!IsExecutionEnabled()) {
        ValidationResult disabled;
        disabled.error =
            "Restormer package execution is disabled in this build. "
            "Select Classical Multiscale instead.";
        return disabled;
    }
    if (m_Impl == nullptr) {
        m_Impl = new Impl();
    }
    const RawRecipe::RawRgbDenoiseRecipe settings =
        RawRecipe::SanitizeRgbDenoiseRecipe(requestedSettings);
    ValidationResult result;
    if (!IsAiMethod(settings.method)) {
        result.error = "Classical Multiscale does not use a Restormer package.";
        return result;
    }
    if (settings.packageId != RawRecipe::kRestormerDenoisePackageId) {
        result.error =
            "This project references an unsupported Restormer package ID.";
        return result;
    }
    if (!allowUnpinnedDevelopmentSelection &&
        (settings.packageVersion.empty() ||
         settings.modelSha256.empty())) {
        result.error =
            "The Restormer method is not pinned to an exact package and model. "
            "Use Check Package in RAW Lab, or select Classical Multiscale.";
        return result;
    }

    const std::filesystem::path root =
        ResolvePackageRoot(AppPaths::GetResourcesDirectory());
    const std::string cacheKey = ValidationCacheKey(root, settings);
    std::error_code ec;
    const std::filesystem::file_time_type manifestWriteTime =
        std::filesystem::last_write_time(root / "manifest.json", ec);
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    if (!ec &&
        cacheKey == m_Impl->validationKey &&
        manifestWriteTime == m_Impl->validationManifestWriteTime) {
        return m_Impl->validation;
    }

    ValidationRequest request;
    request.method = settings.method;
    request.exactPackageVersion = settings.packageVersion;
    request.exactModelSha256 = settings.modelSha256;
    request.exactAdapterVersion = settings.adapterVersion;
    result = ValidatePackage(root, request, TrustPolicyFromEnvironment());
    m_Impl->validationKey = cacheKey;
    m_Impl->validationManifestWriteTime = manifestWriteTime;
    m_Impl->validation = result;
    return result;
}

#if defined(_WIN32)
namespace {

void CloseServiceHandles(Client::Impl& impl) {
    if (impl.pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(impl.pipe);
        impl.pipe = INVALID_HANDLE_VALUE;
    }
    if (impl.process != nullptr) {
        CloseHandle(impl.process);
        impl.process = nullptr;
    }
    impl.runningManifestSha256.clear();
    impl.token.clear();
}

bool StartService(
    Client::Impl& impl,
    const ValidationResult& package,
    std::string& error) {
    if (impl.pipe != INVALID_HANDLE_VALUE &&
        impl.process != nullptr &&
        impl.runningManifestSha256 == package.manifestSha256) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(impl.process, &exitCode) &&
            exitCode == STILL_ACTIVE) {
            return true;
        }
        CloseServiceHandles(impl);
    } else if (impl.pipe != INVALID_HANDLE_VALUE || impl.process != nullptr) {
        CloseServiceHandles(impl);
    }

    const std::uint64_t tokenValue =
        static_cast<std::uint64_t>(GetCurrentProcessId()) ^
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream token;
    token << std::hex << tokenValue;
    impl.token = token.str();
    const std::wstring pipeName =
        L"\\\\.\\pipe\\StackRestormer-" + Utf8ToWide(impl.token);

    const auto findModel = [&](ModelKind kind) -> std::filesystem::path {
        const auto found = std::find_if(
            package.manifest.models.begin(),
            package.manifest.models.end(),
            [kind](const ModelArtifact& model) {
                return model.kind == kind;
            });
        return found == package.manifest.models.end()
            ? std::filesystem::path()
            : package.packageRoot / found->relativePath;
    };
    const std::filesystem::path servicePath =
        package.packageRoot / package.manifest.service.relativePath;
    const std::filesystem::path realModel = findModel(ModelKind::RealPhoto);
    const std::filesystem::path gaussianModel =
        findModel(ModelKind::GaussianBlind);
    if (realModel.empty() || gaussianModel.empty()) {
        error = "The Restormer package does not contain both approved models.";
        return false;
    }
    std::wstring command =
        QuoteArgument(servicePath.wstring()) +
        L" --pipe " + QuoteArgument(pipeName) +
        L" --token " + QuoteArgument(Utf8ToWide(impl.token)) +
        L" --real-model " + QuoteArgument(realModel.wstring()) +
        L" --gaussian-model " + QuoteArgument(gaussianModel.wstring());
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(
            servicePath.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            package.packageRoot.c_str(),
            &startup,
            &process)) {
        error = "Stack could not start StackModelService.exe.";
        return false;
    }
    CloseHandle(process.hThread);
    impl.process = process.hProcess;

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        impl.pipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (impl.pipe != INVALID_HANDLE_VALUE) {
            break;
        }
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(impl.process, &exitCode) ||
            exitCode != STILL_ACTIVE) {
            error = "StackModelService exited during startup.";
            CloseServiceHandles(impl);
            return false;
        }
        WaitNamedPipeW(pipeName.c_str(), 100);
    }
    if (impl.pipe == INVALID_HANDLE_VALUE) {
        error = "StackModelService did not open its control pipe.";
        CloseServiceHandles(impl);
        return false;
    }

    ProtocolRequest health;
    health.requestId = "health-" + std::to_string(++impl.requestSerial);
    health.operation = Operation::Health;
    ProtocolResponse healthResponse;
    if (!SendRequest(impl.pipe, health) ||
        !ReadResponse(impl.pipe, healthResponse, error) ||
        !healthResponse.ok) {
        if (error.empty()) {
            error = healthResponse.error.empty()
                ? "StackModelService failed its health check."
                : healthResponse.error;
        }
        CloseServiceHandles(impl);
        return false;
    }
    impl.runningManifestSha256 = package.manifestSha256;
    return true;
}

} // namespace
#endif

DenoiseResult Client::Denoise(
    const RawRecipe::RawRgbDenoiseRecipe& settings,
    const std::vector<float>& inputSrgbProxy,
    int width,
    int height,
    Quality quality,
    std::uint64_t generation,
    const std::function<bool()>& shouldCancel) {
    DenoiseResult result;
    const ValidationResult package = Validate(settings, false);
    if (!package.ok) {
        result.error = package.error;
        return result;
    }
    result.packageVersion = package.manifest.packageVersion;
    result.modelSha256 = package.selectedModel.sha256;

#if !defined(_WIN32)
    result.error = "Restormer V1 is currently available only on Windows x64.";
    return result;
#else
    if (m_Impl == nullptr) {
        result.error = "Restormer client state is unavailable.";
        return result;
    }
    const std::size_t expected =
        width > 0 && height > 0
            ? static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) * 3U
            : 0U;
    if (inputSrgbProxy.size() != expected) {
        result.error = "Restormer input proxy dimensions are invalid.";
        return result;
    }
    struct InferenceActivityScope {
        explicit InferenceActivityScope(std::atomic<bool>& active)
            : state(active) {
            state.store(true, std::memory_order_relaxed);
        }
        ~InferenceActivityScope() {
            state.store(false, std::memory_order_relaxed);
        }
        std::atomic<bool>& state;
    } inferenceActivity(m_InferenceActive);
    std::unique_lock<std::mutex> lock(m_Impl->mutex);
    if (!StartService(*m_Impl, package, result.error)) {
        return result;
    }

    const std::size_t byteSize = expected * sizeof(float);
    const std::string mappingBase =
        "Local\\StackRestormer-" + m_Impl->token + "-" +
        std::to_string(generation) + "-" +
        std::to_string(++m_Impl->requestSerial);
    SharedMapping inputMapping;
    SharedMapping outputMapping;
    if (!inputMapping.Create(mappingBase + "-input", byteSize) ||
        !outputMapping.Create(mappingBase + "-output", byteSize)) {
        result.error = "Stack could not allocate Restormer shared memory.";
        return result;
    }
    std::memcpy(
        inputMapping.Data(), inputSrgbProxy.data(), byteSize);

    ProtocolRequest request;
    request.requestId =
        "denoise-" + std::to_string(m_Impl->requestSerial);
    request.operation = Operation::Denoise;
    request.generation = generation;
    request.modelKind = ModelKindStableString(
        ModelKindForMethod(settings.method));
    request.quality = quality;
    request.width = width;
    request.height = height;
    request.channels = 3;
    request.byteSize = byteSize;
    request.inputMappingName = mappingBase + "-input";
    request.outputMappingName = mappingBase + "-output";
    if (!SendRequest(m_Impl->pipe, request)) {
        result.error = "Stack could not submit Restormer inference.";
        CloseServiceHandles(*m_Impl);
        return result;
    }

    bool cancelSent = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!cancelSent && shouldCancel && shouldCancel()) {
            ProtocolRequest cancel;
            cancel.requestId =
                "cancel-" + std::to_string(++m_Impl->requestSerial);
            cancel.operation = Operation::Cancel;
            cancel.generation = generation + 1U;
            SendRequest(m_Impl->pipe, cancel);
            cancelSent = true;
        }
        DWORD available = 0;
        if (!PeekNamedPipe(
                m_Impl->pipe, nullptr, 0, nullptr, &available, nullptr)) {
            result.error = "StackModelService stopped responding.";
            CloseServiceHandles(*m_Impl);
            return result;
        }
        if (available < sizeof(std::uint32_t)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        ProtocolResponse response;
        if (!ReadResponse(m_Impl->pipe, response, result.error)) {
            CloseServiceHandles(*m_Impl);
            return result;
        }
        if (response.requestId != request.requestId) {
            continue;
        }
        if (response.state == "accepted") {
            continue;
        }
        result.provider = response.provider;
        result.inferenceMilliseconds = response.inferenceMilliseconds;
        result.completedTiles = response.completedTiles;
        result.totalTiles = response.totalTiles;
        if (response.state == "cancelled" || cancelSent) {
            result.cancelled = true;
            result.error = response.error;
            return result;
        }
        if (!response.ok) {
            result.error = response.error.empty()
                ? "Restormer inference failed."
                : response.error;
            return result;
        }
        result.outputSrgbProxy.assign(
            outputMapping.Data(), outputMapping.Data() + expected);
        result.ok = true;
        return result;
    }
    result.error = "Restormer inference exceeded the safety timeout.";
    CloseServiceHandles(*m_Impl);
    return result;
#endif
}

void Client::Shutdown() {
    if (m_Impl == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
#if defined(_WIN32)
    if (m_Impl->pipe != INVALID_HANDLE_VALUE) {
        ProtocolRequest request;
        request.requestId =
            "shutdown-" + std::to_string(++m_Impl->requestSerial);
        request.operation = Operation::Shutdown;
        SendRequest(m_Impl->pipe, request);
    }
    CloseServiceHandles(*m_Impl);
#endif
}

} // namespace Stack::Restormer
