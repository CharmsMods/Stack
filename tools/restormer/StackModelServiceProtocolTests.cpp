#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Restormer/RestormerProtocol.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

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

bool Send(
    HANDLE pipe,
    const Stack::Restormer::ProtocolRequest& request) {
    const std::string bytes =
        Stack::Restormer::SerializeProtocolRequest(request).dump();
    const std::uint32_t length =
        static_cast<std::uint32_t>(bytes.size());
    return WriteExact(pipe, &length, sizeof(length)) &&
        WriteExact(pipe, bytes.data(), length);
}

bool Receive(
    HANDLE pipe,
    Stack::Restormer::ProtocolResponse& response) {
    std::uint32_t length = 0;
    if (!ReadExact(pipe, &length, sizeof(length)) ||
        length == 0 || length > 1024U * 1024U) {
        return false;
    }
    std::string bytes(length, '\0');
    if (!ReadExact(pipe, bytes.data(), length)) {
        return false;
    }
    const nlohmann::json value =
        nlohmann::json::parse(bytes, nullptr, false);
    std::string error;
    return !value.is_discarded() &&
        Stack::Restormer::ParseProtocolResponse(
            value, response, error);
}

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
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

    bool Create(const std::wstring& name, std::size_t byteSize) {
        const std::uint64_t size = static_cast<std::uint64_t>(byteSize);
        m_Handle = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            static_cast<DWORD>(size >> 32U),
            static_cast<DWORD>(size & 0xffffffffULL),
            name.c_str());
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

std::string NarrowAscii(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character < 0 || character > 127) {
            return {};
        }
        result.push_back(static_cast<char>(character));
    }
    return result;
}

bool RunModelInference(
    HANDLE pipe,
    const std::wstring& token,
    const std::string& modelKind,
    std::uint64_t generation) {
    constexpr int width = 16;
    constexpr int height = 16;
    constexpr std::size_t elementCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 3U;
    constexpr std::size_t byteSize = elementCount * sizeof(float);
    const std::wstring mappingBase =
        L"Local\\StackRestormer-" + token + L"-" +
        std::wstring(modelKind.begin(), modelKind.end());
    SharedMapping input;
    SharedMapping output;
    if (!input.Create(mappingBase + L"-input", byteSize) ||
        !output.Create(mappingBase + L"-output", byteSize)) {
        return false;
    }
    for (std::size_t index = 0; index < elementCount; ++index) {
        input.Data()[index] =
            static_cast<float>(index % 97U) / 96.0f;
        output.Data()[index] = 0.0f;
    }

    Stack::Restormer::ProtocolRequest request;
    request.requestId = "inference-" + modelKind;
    request.operation = Stack::Restormer::Operation::Denoise;
    request.generation = generation;
    request.modelKind = modelKind;
    request.quality = Stack::Restormer::Quality::InteractivePreview;
    request.width = width;
    request.height = height;
    request.channels = 3;
    request.byteSize = byteSize;
    request.inputMappingName = NarrowAscii(mappingBase + L"-input");
    request.outputMappingName = NarrowAscii(mappingBase + L"-output");
    if (!Send(pipe, request)) {
        return false;
    }

    bool accepted = false;
    while (true) {
        Stack::Restormer::ProtocolResponse response;
        if (!Receive(pipe, response) ||
            response.requestId != request.requestId) {
            return false;
        }
        if (response.state == "accepted") {
            accepted = response.ok;
            continue;
        }
        if (!accepted || !response.ok ||
            response.state != "complete" ||
            response.completedTiles != 1 ||
            response.totalTiles != 1) {
            std::cerr << modelKind << " inference failed: "
                      << response.error << "\n";
            return false;
        }
        std::cout << modelKind << " provider=" << response.provider
                  << " inference_ms=" << response.inferenceMilliseconds
                  << "\n";
        return std::all_of(
            output.Data(),
            output.Data() + elementCount,
            [](float value) { return std::isfinite(value); });
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 4) {
        std::cerr
            << "Expected StackModelService path and optional real/Gaussian models.\n";
        return 2;
    }
    const std::wstring servicePath = argv[1];
    const bool testInference = argc == 4;
    const std::wstring token =
        L"protocol-test-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring pipeName =
        L"\\\\.\\pipe\\StackRestormer-" + token;
    const std::wstring dummyModel =
        servicePath + L".protocol-test-unused-model";
    const std::wstring realModel =
        testInference ? argv[2] : dummyModel;
    const std::wstring gaussianModel =
        testInference ? argv[3] : dummyModel;
    std::wstring command =
        Quote(servicePath) +
        L" --pipe " + Quote(pipeName) +
        L" --token " + Quote(token) +
        L" --real-model " + Quote(realModel) +
        L" --gaussian-model " + Quote(gaussianModel);
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
            nullptr,
            &startup,
            &process)) {
        std::cerr << "Could not launch StackModelService.\n";
        return 3;
    }
    CloseHandle(process.hThread);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        pipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        TerminateProcess(process.hProcess, 4);
        CloseHandle(process.hProcess);
        std::cerr << "StackModelService did not create its pipe.\n";
        return 4;
    }

    Stack::Restormer::ProtocolRequest health;
    health.requestId = "health-test";
    health.operation = Stack::Restormer::Operation::Health;
    Stack::Restormer::ProtocolResponse healthResponse;
    const bool healthOk =
        Send(pipe, health) &&
        Receive(pipe, healthResponse) &&
        healthResponse.ok &&
        healthResponse.state == "healthy";
    const bool inferenceOk =
        !testInference ||
        (RunModelInference(pipe, token, "real-photo", 1) &&
         RunModelInference(pipe, token, "gaussian-blind", 2));

    Stack::Restormer::ProtocolRequest shutdown;
    shutdown.requestId = "shutdown-test";
    shutdown.operation = Stack::Restormer::Operation::Shutdown;
    Stack::Restormer::ProtocolResponse shutdownResponse;
    const bool shutdownOk =
        Send(pipe, shutdown) &&
        Receive(pipe, shutdownResponse) &&
        shutdownResponse.ok &&
        shutdownResponse.state == "shutting-down";
    CloseHandle(pipe);

    const DWORD waitResult =
        WaitForSingleObject(process.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 5);
    }
    CloseHandle(process.hProcess);

    if (!healthOk || !inferenceOk || !shutdownOk ||
        waitResult != WAIT_OBJECT_0 || exitCode != 0) {
        std::cerr << "StackModelService protocol smoke test failed.\n";
        return 5;
    }
    std::cout << "StackModelService protocol"
              << (testInference ? " and model inference" : "")
              << " smoke test passed.\n";
    return 0;
}
