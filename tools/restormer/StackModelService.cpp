#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Restormer/RestormerProtocol.h"
#include "Restormer/RestormerTiling.h"
#include "ThirdParty/json.hpp"

#include <windows.h>

#include <dml_provider_factory.h>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using Stack::Restormer::ProtocolRequest;
using Stack::Restormer::ProtocolResponse;

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

bool ReadMessage(HANDLE pipe, nlohmann::json& value) {
    std::uint32_t length = 0;
    if (!ReadExact(pipe, &length, sizeof(length)) ||
        length == 0 || length > 1024U * 1024U) {
        return false;
    }
    std::string bytes(length, '\0');
    if (!ReadExact(pipe, bytes.data(), length)) {
        return false;
    }
    value = nlohmann::json::parse(bytes, nullptr, false);
    return !value.is_discarded();
}

bool WriteMessage(
    HANDLE pipe,
    std::mutex& writeMutex,
    const ProtocolResponse& response) {
    const std::string bytes =
        Stack::Restormer::SerializeProtocolResponse(response).dump();
    const std::uint32_t length = static_cast<std::uint32_t>(bytes.size());
    std::lock_guard<std::mutex> lock(writeMutex);
    return WriteExact(pipe, &length, sizeof(length)) &&
        WriteExact(pipe, bytes.data(), length);
}

class SharedMappingView {
public:
    ~SharedMappingView() {
        if (m_Data != nullptr) {
            UnmapViewOfFile(m_Data);
        }
        if (m_Mapping != nullptr) {
            CloseHandle(m_Mapping);
        }
    }

    bool Open(const std::string& name, std::size_t byteSize, DWORD access) {
        const std::wstring wideName = Utf8ToWide(name);
        if (wideName.empty() || byteSize == 0) {
            return false;
        }
        m_Mapping = OpenFileMappingW(access, FALSE, wideName.c_str());
        if (m_Mapping == nullptr) {
            return false;
        }
        m_Data = MapViewOfFile(m_Mapping, access, 0, 0, byteSize);
        return m_Data != nullptr;
    }

    float* Data() const {
        return static_cast<float*>(m_Data);
    }

private:
    HANDLE m_Mapping = nullptr;
    void* m_Data = nullptr;
};

class ModelRunner {
public:
    ModelRunner(
        Ort::Env& environment,
        std::filesystem::path modelPath)
        : m_ModelPath(std::move(modelPath)) {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.DisableMemPattern();
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        const char* forceCpu = std::getenv("STACK_RESTORMER_FORCE_CPU");
        if (forceCpu == nullptr || std::string(forceCpu) != "1") {
            try {
                Ort::ThrowOnError(
                    OrtSessionOptionsAppendExecutionProvider_DML(options, 0));
                m_Provider = "DirectML";
            } catch (const Ort::Exception&) {
                m_Provider = "CPU";
            }
        }
        m_Session =
            std::make_unique<Ort::Session>(
                environment, m_ModelPath.c_str(), options);
    }

    bool Run(
        const float* inputNchw,
        int width,
        int height,
        float* outputNchw,
        std::string& error) {
        try {
            const std::size_t elementCount =
                static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) * 3U;
            const std::array<std::int64_t, 4> shape {
                1, 3, height, width
            };
            Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input = Ort::Value::CreateTensor<float>(
                memory,
                const_cast<float*>(inputNchw),
                elementCount,
                shape.data(),
                shape.size());
            const char* inputNames[] = { "input" };
            const char* outputNames[] = { "output" };
            std::vector<Ort::Value> outputs = m_Session->Run(
                Ort::RunOptions { nullptr },
                inputNames,
                &input,
                1,
                outputNames,
                1);
            if (outputs.size() != 1 || !outputs[0].IsTensor()) {
                error = "Restormer returned an invalid output tensor.";
                return false;
            }
            const auto outputInfo =
                outputs[0].GetTensorTypeAndShapeInfo();
            if (outputInfo.GetElementCount() != elementCount) {
                error = "Restormer returned an unexpected output size.";
                return false;
            }
            const float* output = outputs[0].GetTensorData<float>();
            std::copy(output, output + elementCount, outputNchw);
            return true;
        } catch (const Ort::Exception& exception) {
            error = exception.what();
            return false;
        }
    }

    const std::string& Provider() const {
        return m_Provider;
    }

private:
    std::filesystem::path m_ModelPath;
    std::unique_ptr<Ort::Session> m_Session;
    std::string m_Provider = "CPU";
};

struct Arguments {
    std::wstring pipeName;
    std::string token;
    std::filesystem::path realModel;
    std::filesystem::path gaussianModel;
};

bool ParseArguments(int argc, wchar_t** argv, Arguments& arguments) {
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::wstring key = argv[index];
        const std::wstring value = argv[index + 1];
        if (key == L"--pipe") {
            arguments.pipeName = value;
        } else if (key == L"--token") {
            const int length = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                nullptr, 0, nullptr, nullptr);
            arguments.token.resize(static_cast<std::size_t>(std::max(0, length)));
            if (length > 0) {
                WideCharToMultiByte(
                    CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                    arguments.token.data(), length, nullptr, nullptr);
            }
        } else if (key == L"--real-model") {
            arguments.realModel = value;
        } else if (key == L"--gaussian-model") {
            arguments.gaussianModel = value;
        } else {
            return false;
        }
    }
    return !arguments.pipeName.empty() &&
        arguments.pipeName.rfind(L"\\\\.\\pipe\\StackRestormer-", 0) == 0 &&
        !arguments.token.empty() &&
        arguments.token.size() <= 96 &&
        !arguments.realModel.empty() &&
        !arguments.gaussianModel.empty();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Arguments arguments;
    if (!ParseArguments(argc, argv, arguments)) {
        std::cerr << "Invalid StackModelService arguments.\n";
        return 2;
    }

    HANDLE pipe = CreateNamedPipeW(
        arguments.pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1,
        1024 * 1024,
        1024 * 1024,
        0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return 3;
    }
    const BOOL connected =
        ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : GetLastError() == ERROR_PIPE_CONNECTED;
    if (!connected) {
        CloseHandle(pipe);
        return 4;
    }

    Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "StackModelService");
    std::unordered_map<std::string, std::unique_ptr<ModelRunner>> runners;
    std::mutex runnerMutex;
    std::mutex pipeWriteMutex;
    std::mutex jobMutex;
    std::condition_variable jobCv;
    std::optional<ProtocolRequest> pendingJob;
    std::mutex completedMutex;
    std::deque<ProtocolResponse> completedResponses;
    std::atomic<std::uint64_t> newestGeneration { 0 };
    std::atomic<bool> stopping { false };
    const std::string mappingPrefix =
        "Local\\StackRestormer-" + arguments.token + "-";

    auto runnerFor = [&](const std::string& kind) -> ModelRunner* {
        std::lock_guard<std::mutex> lock(runnerMutex);
        auto found = runners.find(kind);
        if (found != runners.end()) {
            return found->second.get();
        }
        const std::filesystem::path modelPath =
            kind == "gaussian-blind"
                ? arguments.gaussianModel
                : arguments.realModel;
        auto runner =
            std::make_unique<ModelRunner>(environment, modelPath);
        ModelRunner* result = runner.get();
        runners.emplace(kind, std::move(runner));
        return result;
    };

    std::thread worker([&]() {
        while (!stopping.load()) {
            ProtocolRequest job;
            {
                std::unique_lock<std::mutex> lock(jobMutex);
                jobCv.wait(lock, [&]() {
                    return stopping.load() || pendingJob.has_value();
                });
                if (stopping.load()) {
                    break;
                }
                job = *pendingJob;
                pendingJob.reset();
            }

            ProtocolResponse response;
            response.requestId = job.requestId;
            response.generation = job.generation;
            response.state = "complete";
            const auto started = std::chrono::steady_clock::now();
            try {
                if (job.inputMappingName.rfind(mappingPrefix, 0) != 0 ||
                    job.outputMappingName.rfind(mappingPrefix, 0) != 0) {
                    throw std::runtime_error(
                        "Shared-memory mapping does not belong to this service.");
                }
                SharedMappingView inputMapping;
                SharedMappingView outputMapping;
                if (!inputMapping.Open(
                        job.inputMappingName, job.byteSize, FILE_MAP_READ) ||
                    !outputMapping.Open(
                        job.outputMappingName, job.byteSize, FILE_MAP_WRITE)) {
                    throw std::runtime_error(
                        "Shared-memory image buffers could not be opened.");
                }
                const std::size_t count = job.byteSize / sizeof(float);
                std::vector<float> input(
                    inputMapping.Data(), inputMapping.Data() + count);
                std::vector<float> output;
                ModelRunner* runner = runnerFor(job.modelKind);
                response.provider = runner->Provider();
                const Stack::Restormer::TilePolicy policy =
                    job.quality ==
                        Stack::Restormer::Quality::InteractivePreview
                        ? Stack::Restormer::TilePolicy { 256, 32 }
                        : Stack::Restormer::TilePolicy { 384, 64 };
                const Stack::Restormer::TiledInferenceResult tiled =
                    Stack::Restormer::RunTiledInference(
                        input,
                        job.width,
                        job.height,
                        policy,
                        [&](const float* tileInput,
                            int tileWidth,
                            int tileHeight,
                            float* tileOutput,
                            std::string& error) {
                            return runner->Run(
                                tileInput,
                                tileWidth,
                                tileHeight,
                                tileOutput,
                                error);
                        },
                        [&]() {
                            return stopping.load() ||
                                job.generation < newestGeneration.load();
                        },
                        output);
                response.completedTiles = tiled.completedTiles;
                response.totalTiles = tiled.totalTiles;
                if (!tiled.ok) {
                    response.state =
                        tiled.cancelled ? "cancelled" : "failed";
                    response.error = tiled.error;
                } else if (job.generation < newestGeneration.load()) {
                    response.state = "cancelled";
                    response.error = "A newer Restormer generation replaced this job.";
                } else {
                    std::copy(
                        output.begin(), output.end(), outputMapping.Data());
                    response.ok = true;
                }
            } catch (const std::exception& exception) {
                response.state = "failed";
                response.error = exception.what();
            }
            response.inferenceMilliseconds =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
            {
                std::lock_guard<std::mutex> lock(completedMutex);
                completedResponses.push_back(std::move(response));
            }
        }
    });

    bool keepRunning = true;
    while (keepRunning) {
        std::deque<ProtocolResponse> readyResponses;
        {
            std::lock_guard<std::mutex> lock(completedMutex);
            readyResponses.swap(completedResponses);
        }
        for (const ProtocolResponse& response : readyResponses) {
            if (!WriteMessage(pipe, pipeWriteMutex, response)) {
                keepRunning = false;
                break;
            }
        }
        if (!keepRunning) {
            break;
        }

        DWORD available = 0;
        if (!PeekNamedPipe(
                pipe, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available < sizeof(std::uint32_t)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        nlohmann::json value;
        if (!ReadMessage(pipe, value)) {
            break;
        }
        ProtocolRequest request;
        std::string parseError;
        if (!Stack::Restormer::ParseProtocolRequest(
                value, request, parseError)) {
            ProtocolResponse response;
            response.requestId =
                value.value("requestId", std::string("invalid"));
            response.state = "rejected";
            response.error = parseError;
            WriteMessage(pipe, pipeWriteMutex, response);
            continue;
        }

        ProtocolResponse response;
        response.requestId = request.requestId;
        response.generation = request.generation;
        switch (request.operation) {
            case Stack::Restormer::Operation::Health:
                response.ok = true;
                response.state = "healthy";
                try {
                    const std::vector<std::string> providers =
                        Ort::GetAvailableProviders();
                    response.provider =
                        std::find(
                            providers.begin(),
                            providers.end(),
                            "DmlExecutionProvider") != providers.end()
                            ? "DirectML"
                            : "CPU";
                } catch (const Ort::Exception&) {
                    response.provider = "CPU";
                }
                WriteMessage(pipe, pipeWriteMutex, response);
                break;
            case Stack::Restormer::Operation::Denoise: {
                const std::uint64_t previous = newestGeneration.load();
                newestGeneration.store(std::max(previous, request.generation));
                {
                    std::lock_guard<std::mutex> lock(jobMutex);
                    pendingJob = request;
                }
                jobCv.notify_one();
                response.ok = true;
                response.state = "accepted";
                WriteMessage(pipe, pipeWriteMutex, response);
                break;
            }
            case Stack::Restormer::Operation::Cancel:
                newestGeneration.store(
                    std::max(newestGeneration.load(), request.generation));
                response.ok = true;
                response.state = "cancel-requested";
                WriteMessage(pipe, pipeWriteMutex, response);
                break;
            case Stack::Restormer::Operation::Shutdown:
                response.ok = true;
                response.state = "shutting-down";
                WriteMessage(pipe, pipeWriteMutex, response);
                keepRunning = false;
                break;
        }
    }

    stopping.store(true);
    newestGeneration.store(std::numeric_limits<std::uint64_t>::max());
    jobCv.notify_all();
    worker.join();
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    return 0;
}
