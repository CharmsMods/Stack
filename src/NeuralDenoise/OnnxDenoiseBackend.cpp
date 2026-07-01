#include "OnnxDenoiseBackend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace NeuralDenoise {
namespace {

constexpr unsigned int kOrtApiVersion = 14;

#ifdef _WIN32
#define STACK_ORT_CALL __stdcall
using OrtPathChar = wchar_t;
#else
#define STACK_ORT_CALL
using OrtPathChar = char;
#endif

struct OrtEnv;
struct OrtStatus;
struct OrtMemoryInfo;
struct OrtSession;
struct OrtValue;
struct OrtRunOptions;
struct OrtTypeInfo;
struct OrtTensorTypeAndShapeInfo;
struct OrtSessionOptions;
struct OrtAllocator;
struct OrtCustomOpDomain;
struct OrtKernelInfo;
struct OrtKernelContext;

enum OrtErrorCode {
    ORT_OK = 0
};

enum OrtLoggingLevel {
    ORT_LOGGING_LEVEL_WARNING = 2
};

enum ONNXTensorElementDataType {
    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT = 1
};

enum OrtAllocatorType {
    OrtArenaAllocator = 1
};

enum OrtMemType {
    OrtMemTypeDefault = 0
};

enum GraphOptimizationLevel {
    ORT_ENABLE_EXTENDED = 2
};

struct OrtApi {
    OrtStatus*(STACK_ORT_CALL* CreateStatus)(OrtErrorCode code, const char* msg);
    OrtErrorCode(STACK_ORT_CALL* GetErrorCode)(const OrtStatus* status);
    const char*(STACK_ORT_CALL* GetErrorMessage)(const OrtStatus* status);
    OrtStatus*(STACK_ORT_CALL* CreateEnv)(OrtLoggingLevel logSeverityLevel, const char* logId, OrtEnv** out);
    void* CreateEnvWithCustomLogger;
    void* EnableTelemetryEvents;
    void* DisableTelemetryEvents;
    OrtStatus*(STACK_ORT_CALL* CreateSession)(const OrtEnv* env, const OrtPathChar* modelPath, const OrtSessionOptions* options, OrtSession** out);
    void* CreateSessionFromArray;
    OrtStatus*(STACK_ORT_CALL* Run)(OrtSession* session, const OrtRunOptions* runOptions, const char* const* inputNames, const OrtValue* const* inputs, std::size_t inputLen, const char* const* outputNames, std::size_t outputNamesLen, OrtValue** outputs);
    OrtStatus*(STACK_ORT_CALL* CreateSessionOptions)(OrtSessionOptions** options);
    void* SetOptimizedModelFilePath;
    void* CloneSessionOptions;
    void* SetSessionExecutionMode;
    void* EnableProfiling;
    void* DisableProfiling;
    void* EnableMemPattern;
    void* DisableMemPattern;
    void* EnableCpuMemArena;
    void* DisableCpuMemArena;
    void* SetSessionLogId;
    void* SetSessionLogVerbosityLevel;
    void* SetSessionLogSeverityLevel;
    OrtStatus*(STACK_ORT_CALL* SetSessionGraphOptimizationLevel)(OrtSessionOptions* options, GraphOptimizationLevel graphOptimizationLevel);
    void* SetIntraOpNumThreads;
    void* SetInterOpNumThreads;
    void* CreateCustomOpDomain;
    void* CustomOpDomain_Add;
    void* AddCustomOpDomain;
    void* RegisterCustomOpsLibrary;
    OrtStatus*(STACK_ORT_CALL* SessionGetInputCount)(const OrtSession* session, std::size_t* out);
    OrtStatus*(STACK_ORT_CALL* SessionGetOutputCount)(const OrtSession* session, std::size_t* out);
    void* SessionGetOverridableInitializerCount;
    void* SessionGetInputTypeInfo;
    void* SessionGetOutputTypeInfo;
    void* SessionGetOverridableInitializerTypeInfo;
    OrtStatus*(STACK_ORT_CALL* SessionGetInputName)(const OrtSession* session, std::size_t index, OrtAllocator* allocator, char** value);
    OrtStatus*(STACK_ORT_CALL* SessionGetOutputName)(const OrtSession* session, std::size_t index, OrtAllocator* allocator, char** value);
    void* SessionGetOverridableInitializerName;
    void* CreateRunOptions;
    void* RunOptionsSetRunLogVerbosityLevel;
    void* RunOptionsSetRunLogSeverityLevel;
    void* RunOptionsSetRunTag;
    void* RunOptionsGetRunLogVerbosityLevel;
    void* RunOptionsGetRunLogSeverityLevel;
    void* RunOptionsGetRunTag;
    void* RunOptionsSetTerminate;
    void* RunOptionsUnsetTerminate;
    void* CreateTensorAsOrtValue;
    OrtStatus*(STACK_ORT_CALL* CreateTensorWithDataAsOrtValue)(const OrtMemoryInfo* info, void* data, std::size_t dataLength, const int64_t* shape, std::size_t shapeLength, ONNXTensorElementDataType type, OrtValue** out);
    void* IsTensor;
    OrtStatus*(STACK_ORT_CALL* GetTensorMutableData)(OrtValue* value, void** out);
    void* FillStringTensor;
    void* GetStringTensorDataLength;
    void* GetStringTensorContent;
    void* CastTypeInfoToTensorInfo;
    void* GetOnnxTypeFromTypeInfo;
    void* CreateTensorTypeAndShapeInfo;
    void* SetTensorElementType;
    void* SetDimensions;
    void* GetTensorElementType;
    void* GetDimensionsCount;
    void* GetDimensions;
    void* GetSymbolicDimensions;
    void* GetTensorShapeElementCount;
    void* GetTensorTypeAndShape;
    void* GetTypeInfo;
    void* GetValueType;
    void* CreateMemoryInfo;
    OrtStatus*(STACK_ORT_CALL* CreateCpuMemoryInfo)(OrtAllocatorType type, OrtMemType memType, OrtMemoryInfo** out);
    void* CompareMemoryInfo;
    void* MemoryInfoGetName;
    void* MemoryInfoGetId;
    void* MemoryInfoGetMemType;
    void* MemoryInfoGetType;
    void* AllocatorAlloc;
    OrtStatus*(STACK_ORT_CALL* AllocatorFree)(OrtAllocator* allocator, void* p);
    void* AllocatorGetInfo;
    OrtStatus*(STACK_ORT_CALL* GetAllocatorWithDefaultOptions)(OrtAllocator** out);
    void* AddFreeDimensionOverride;
    void* GetValue;
    void* GetValueCount;
    void* CreateValue;
    void* CreateOpaqueValue;
    void* GetOpaqueValue;
    void* KernelInfoGetAttribute_float;
    void* KernelInfoGetAttribute_int64;
    void* KernelInfoGetAttribute_string;
    void* KernelContext_GetInputCount;
    void* KernelContext_GetOutputCount;
    void* KernelContext_GetInput;
    void* KernelContext_GetOutput;
    void(STACK_ORT_CALL* ReleaseEnv)(OrtEnv* input);
    void(STACK_ORT_CALL* ReleaseStatus)(OrtStatus* input);
    void(STACK_ORT_CALL* ReleaseMemoryInfo)(OrtMemoryInfo* input);
    void(STACK_ORT_CALL* ReleaseSession)(OrtSession* input);
    void(STACK_ORT_CALL* ReleaseValue)(OrtValue* input);
    void(STACK_ORT_CALL* ReleaseRunOptions)(OrtRunOptions* input);
    void(STACK_ORT_CALL* ReleaseTypeInfo)(OrtTypeInfo* input);
    void(STACK_ORT_CALL* ReleaseTensorTypeAndShapeInfo)(OrtTensorTypeAndShapeInfo* input);
    void(STACK_ORT_CALL* ReleaseSessionOptions)(OrtSessionOptions* input);
};

struct OrtApiBase {
    const OrtApi*(STACK_ORT_CALL* GetApi)(uint32_t version);
    const char*(STACK_ORT_CALL* GetVersionString)();
};

using OrtGetApiBaseFn = const OrtApiBase*(STACK_ORT_CALL*)();
using AppendCudaProviderFn = OrtStatus*(STACK_ORT_CALL*)(OrtSessionOptions* options, int deviceId);

bool FileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

bool UsesCudaPreference(const NeuralDenoiseModelInfo& model, RuntimePreference preference) {
    return preference == RuntimePreference::Cuda ||
        (preference == RuntimePreference::Auto && model.preferredBackend == "onnx_cuda");
}

bool AllowsCpuExecution(RuntimePreference preference, bool allowCpuFallback) {
    return preference == RuntimePreference::Cpu || allowCpuFallback;
}

std::string StatusToString(const OrtApi* api, OrtStatus* status) {
    if (!status) {
        return {};
    }
    std::string result = api && api->GetErrorMessage ? api->GetErrorMessage(status) : "ONNX Runtime call failed";
    if (api && api->ReleaseStatus) {
        api->ReleaseStatus(status);
    }
    return result;
}

#ifdef _WIN32
std::string WindowsLastErrorString(DWORD errorCode = GetLastError()) {
    if (errorCode == 0) {
        return {};
    }
    LPSTR message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(
        flags,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message),
        0,
        nullptr);
    std::string result = length > 0 && message ? std::string(message, length) : ("Windows error " + std::to_string(errorCode));
    if (message) {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }
    return result;
}

HMODULE TryLoadRuntimeDll(const std::filesystem::path& path, std::string& outError) {
    outError.clear();
    HMODULE module = LoadLibraryW(path.wstring().c_str());
    if (!module) {
        outError = WindowsLastErrorString();
    }
    return module;
}
#endif

} // namespace

struct OnnxDenoiseBackend::OrtState {
#ifdef _WIN32
    HMODULE module = nullptr;
#endif
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtMemoryInfo* memoryInfo = nullptr;
    OrtSession* session = nullptr;
    OrtAllocator* allocator = nullptr;
    AppendCudaProviderFn appendCuda = nullptr;

    ~OrtState() {
        Reset();
    }

    void ResetSession() {
        if (api && session) {
            api->ReleaseSession(session);
        }
        session = nullptr;
    }

    void Reset() {
        ResetSession();
        if (api && memoryInfo) {
            api->ReleaseMemoryInfo(memoryInfo);
        }
        memoryInfo = nullptr;
        if (api && env) {
            api->ReleaseEnv(env);
        }
        env = nullptr;
        allocator = nullptr;
        api = nullptr;
#ifdef _WIN32
        if (module) {
            FreeLibrary(module);
        }
        module = nullptr;
#endif
    }
};

OnnxDenoiseBackend::OnnxDenoiseBackend(std::filesystem::path runtimeDirectory)
    : m_RuntimeDirectory(std::move(runtimeDirectory)) {}

OnnxDenoiseBackend::~OnnxDenoiseBackend() = default;

void OnnxDenoiseBackend::SetRuntimeDirectory(std::filesystem::path runtimeDirectory) {
    m_RuntimeDirectory = std::move(runtimeDirectory);
    m_Probed = false;
    m_CudaProviderAvailable = false;
    m_Status = {};
    m_CudaProviderStatus.clear();
    m_LastStatusText.clear();
    m_InputName.clear();
    m_OutputName.clear();
    m_LoadedModelId.clear();
    m_LastSessionUsedCuda = false;
    m_LastSessionUsedCpu = false;
    m_Ort.reset();
}

BackendStatus OnnxDenoiseBackend::GetStatus() const {
    Probe();
    return m_Status;
}

bool OnnxDenoiseBackend::SupportsModel(const NeuralDenoiseModelInfo& model) const {
    const std::string backend = model.preferredBackend;
    const bool backendOk = backend.empty() ||
        backend == "onnx" ||
        backend == "onnx_cuda" ||
        backend == "onnx_cpu";
    const bool typeOk = model.type == ModelType::LinearRgb || model.type == ModelType::GenericRgb;
    const bool formatOk = model.inputFormat.empty() || model.inputFormat == "nchw";
    const bool rangeOk = model.inputRange.empty() || model.inputRange == "0_1";
    return backendOk &&
        typeOk &&
        formatOk &&
        rangeOk &&
        model.inputChannels == 3 &&
        model.outputChannels == 3;
}

bool OnnxDenoiseBackend::HasCoreRuntime() const {
    Probe();
    return FileExists(m_RuntimeDirectory / "onnxruntime.dll");
}

bool OnnxDenoiseBackend::HasCudaProvider() const {
    Probe();
    return m_CudaProviderAvailable;
}

void OnnxDenoiseBackend::Probe() const {
    if (m_Probed) {
        return;
    }
    m_Probed = true;
    m_CudaProviderAvailable = false;
    m_CudaProviderStatus.clear();
    m_Status = {};
    m_Status.name = "ONNX Runtime";

    if (m_RuntimeDirectory.empty()) {
        m_Status.status = "ONNX Runtime missing";
        m_Status.warnings.push_back("No denoise runtime directory is configured.");
        return;
    }

    const std::filesystem::path core = m_RuntimeDirectory / "onnxruntime.dll";
    if (!FileExists(core)) {
        m_Status.status = "ONNX Runtime missing";
        m_Status.warnings.push_back("Expected denoise/runtimes/onnxruntime.dll.");
        return;
    }

#ifdef _WIN32
    SetDllDirectoryW(m_RuntimeDirectory.wstring().c_str());
    std::string loadError;
    HMODULE module = TryLoadRuntimeDll(core, loadError);
    if (!module) {
        m_Status.status = "ONNX Runtime failed to load";
        m_Status.warnings.push_back("The ONNX Runtime DLL exists but could not be loaded: " + loadError);
        return;
    }
#endif

    const bool cudaFiles = FileExists(m_RuntimeDirectory / "onnxruntime_providers_cuda.dll") &&
        FileExists(m_RuntimeDirectory / "onnxruntime_providers_shared.dll");
#ifdef _WIN32
    if (cudaFiles) {
        const std::filesystem::path shared = m_RuntimeDirectory / "onnxruntime_providers_shared.dll";
        std::string sharedError;
        HMODULE sharedModule = TryLoadRuntimeDll(shared, sharedError);
        static constexpr const char* kRequiredCudaDlls[] = {
            "cudart64_12.dll",
            "cublas64_12.dll",
            "cublasLt64_12.dll",
            "cudnn64_9.dll",
            "cudnn_ops64_9.dll",
            "cudnn_graph64_9.dll",
            "cufft64_11.dll",
            "curand64_10.dll",
            "nvrtc64_120_0.dll",
            "nvJitLink_120_0.dll"
        };
        std::string dependencyError;
        std::vector<HMODULE> dependencyModules;
        if (sharedModule) {
            for (const char* dllName : kRequiredCudaDlls) {
                const std::filesystem::path dllPath = m_RuntimeDirectory / dllName;
                if (!FileExists(dllPath)) {
                    dependencyError = std::string(dllName) + " is missing";
                    break;
                }
                std::string dllError;
                HMODULE dependencyModule = TryLoadRuntimeDll(dllPath, dllError);
                if (!dependencyModule) {
                    dependencyError = std::string(dllName) + ": " + dllError;
                    break;
                }
                dependencyModules.push_back(dependencyModule);
            }
        }
        if (sharedModule && dependencyError.empty()) {
            m_CudaProviderAvailable = true;
            m_CudaProviderStatus = "CUDA provider dependencies loaded";
        } else {
            m_CudaProviderStatus = "CUDA provider missing dependency";
            if (!sharedModule && !sharedError.empty()) {
                m_CudaProviderStatus += ": onnxruntime_providers_shared.dll: " + sharedError;
            } else if (!dependencyError.empty()) {
                m_CudaProviderStatus += ": " + dependencyError;
            }
            m_Status.warnings.push_back(m_CudaProviderStatus);
        }
        for (HMODULE dependencyModule : dependencyModules) {
            FreeLibrary(dependencyModule);
        }
        if (sharedModule) {
            FreeLibrary(sharedModule);
        }
    }
    if (module) {
        FreeLibrary(module);
    }
#else
    if (cudaFiles) {
        m_CudaProviderStatus = "CUDA provider probing is currently implemented for Windows DLL packs.";
    }
#endif
    m_Status.available = true;
    m_Status.status = m_CudaProviderAvailable
        ? "ONNX Runtime available; CUDA provider loaded"
        : "ONNX Runtime available; CUDA provider unavailable";
    if (!cudaFiles) {
        m_CudaProviderStatus = "CUDA provider DLLs missing";
        m_Status.warnings.push_back("CUDA provider unavailable. Stack will not silently fall back to CPU inference.");
    } else if (!m_CudaProviderAvailable) {
        m_Status.warnings.push_back("Stack will not silently fall back to CPU inference.");
    }
}

bool OnnxDenoiseBackend::LoadModel(
    const NeuralDenoiseModelInfo& model,
    RuntimePreference runtimePreference,
    bool allowCpuFallback,
    std::string& outError) {
    Probe();
    outError.clear();
    if (!m_Status.available) {
        outError = m_Status.status;
        m_LastStatusText = outError;
        return false;
    }
    if (!SupportsModel(model)) {
        outError = "Real inference not available for selected model contract. Expected linear_rgb/generic_rgb NCHW FP32 RGB 0..1.";
        m_LastStatusText = outError;
        return false;
    }
    if (!FileExists(model.resolvedPath)) {
        outError = "Model file missing";
        m_LastStatusText = outError;
        return false;
    }
    if (runtimePreference == RuntimePreference::DirectML || runtimePreference == RuntimePreference::TensorRT) {
        outError = "Selected provider is not implemented for neural denoise yet.";
        m_LastStatusText = outError;
        return false;
    }

    const bool wantsCuda = UsesCudaPreference(model, runtimePreference);
    const bool canUseCpu = AllowsCpuExecution(runtimePreference, allowCpuFallback);
    const bool cudaProviderFilesAvailable = wantsCuda && HasCudaProvider();
    if (wantsCuda && !cudaProviderFilesAvailable && !canUseCpu) {
        outError = "CUDA provider unavailable; CPU fallback disabled";
        m_LastStatusText = outError;
        return false;
    }
    if (!wantsCuda && runtimePreference != RuntimePreference::Cpu && !canUseCpu) {
        outError = "CPU fallback disabled";
        m_LastStatusText = outError;
        return false;
    }
    const bool desiredCuda = cudaProviderFilesAvailable;
    if (IsModelLoaded(model.id) &&
        ((m_LastSessionUsedCuda && desiredCuda) ||
         (m_LastSessionUsedCpu && !desiredCuda))) {
        return true;
    }

    if (!m_Ort) {
        m_Ort = std::make_unique<OrtState>();
    }

#ifdef _WIN32
    if (!m_Ort->module) {
        const std::filesystem::path core = m_RuntimeDirectory / "onnxruntime.dll";
        SetDllDirectoryW(m_RuntimeDirectory.wstring().c_str());
        m_Ort->module = LoadLibraryW(core.wstring().c_str());
        if (!m_Ort->module) {
            outError = "ONNX Runtime failed to load. A dependent DLL may be missing.";
            m_LastStatusText = outError;
            return false;
        }
        auto getApiBase = reinterpret_cast<OrtGetApiBaseFn>(GetProcAddress(m_Ort->module, "OrtGetApiBase"));
        if (!getApiBase) {
            outError = "ONNX Runtime missing OrtGetApiBase export.";
            m_LastStatusText = outError;
            return false;
        }
        const OrtApiBase* apiBase = getApiBase();
        m_Ort->api = apiBase ? apiBase->GetApi(kOrtApiVersion) : nullptr;
        if (!m_Ort->api) {
            outError = "ONNX Runtime C API version is unsupported.";
            m_LastStatusText = outError;
            return false;
        }
        m_Ort->appendCuda = reinterpret_cast<AppendCudaProviderFn>(GetProcAddress(m_Ort->module, "OrtSessionOptionsAppendExecutionProvider_CUDA"));
    }
#else
    outError = "ONNX Runtime inference is currently implemented for Windows dynamic DLL packs.";
    m_LastStatusText = outError;
    return false;
#endif

    const OrtApi* api = m_Ort->api;
    if (!m_Ort->env) {
        std::string error = StatusToString(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "StackNeuralDenoise", &m_Ort->env));
        if (!error.empty()) {
            outError = "Failed to create ONNX Runtime environment: " + error;
            m_LastStatusText = outError;
            return false;
        }
    }
    if (!m_Ort->memoryInfo) {
        std::string error = StatusToString(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &m_Ort->memoryInfo));
        if (!error.empty()) {
            outError = "Failed to create ONNX Runtime CPU memory info: " + error;
            m_LastStatusText = outError;
            return false;
        }
    }
    if (!m_Ort->allocator) {
        std::string error = StatusToString(api, api->GetAllocatorWithDefaultOptions(&m_Ort->allocator));
        if (!error.empty()) {
            outError = "Failed to get ONNX Runtime allocator: " + error;
            m_LastStatusText = outError;
            return false;
        }
    }

    auto createSession = [&](bool useCuda, std::string& error) -> bool {
        OrtSessionOptions* options = nullptr;
        error = StatusToString(api, api->CreateSessionOptions(&options));
        if (!error.empty()) {
            return false;
        }
        if (api->SetSessionGraphOptimizationLevel) {
            error = StatusToString(api, api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_EXTENDED));
            if (!error.empty()) {
                api->ReleaseSessionOptions(options);
                return false;
            }
        }
        if (useCuda) {
            if (!m_Ort->appendCuda) {
                error = "CUDA provider append function unavailable in ONNX Runtime DLL.";
                api->ReleaseSessionOptions(options);
                return false;
            }
            error = StatusToString(api, m_Ort->appendCuda(options, 0));
            if (!error.empty()) {
                api->ReleaseSessionOptions(options);
                return false;
            }
        }
        OrtSession* session = nullptr;
#ifdef _WIN32
        const std::wstring modelPath = std::filesystem::path(model.resolvedPath).wstring();
#else
        const std::string modelPath = std::filesystem::path(model.resolvedPath).string();
#endif
        error = StatusToString(api, api->CreateSession(m_Ort->env, modelPath.c_str(), options, &session));
        api->ReleaseSessionOptions(options);
        if (!error.empty()) {
            return false;
        }
        m_Ort->ResetSession();
        m_Ort->session = session;
        m_LastSessionUsedCuda = useCuda;
        m_LastSessionUsedCpu = !useCuda;
        return true;
    };

    std::string sessionError;
    bool sessionReady = false;
    if (desiredCuda) {
        sessionReady = createSession(true, sessionError);
        if (!sessionReady && !canUseCpu) {
            outError = "CUDA session creation failed; CPU fallback disabled: " + sessionError;
            m_LastStatusText = outError;
            return false;
        }
    }
    if (!sessionReady) {
        if (!canUseCpu && runtimePreference != RuntimePreference::Cpu) {
            outError = sessionError.empty() ? "CPU fallback disabled" : sessionError;
            m_LastStatusText = outError;
            return false;
        }
        sessionReady = createSession(false, sessionError);
        if (!sessionReady) {
            outError = "CPU ONNX session creation failed: " + sessionError;
            m_LastStatusText = outError;
            return false;
        }
    }

    std::size_t inputCount = 0;
    std::size_t outputCount = 0;
    std::string error = StatusToString(api, api->SessionGetInputCount(m_Ort->session, &inputCount));
    if (!error.empty() || inputCount < 1) {
        outError = error.empty() ? "ONNX model has no inputs." : error;
        ResetSession();
        m_LastStatusText = outError;
        return false;
    }
    error = StatusToString(api, api->SessionGetOutputCount(m_Ort->session, &outputCount));
    if (!error.empty() || outputCount < 1) {
        outError = error.empty() ? "ONNX model has no outputs." : error;
        ResetSession();
        m_LastStatusText = outError;
        return false;
    }

    m_InputName = model.inputName;
    m_OutputName = model.outputName;
    if (m_InputName.empty()) {
        char* inputName = nullptr;
        error = StatusToString(api, api->SessionGetInputName(m_Ort->session, 0, m_Ort->allocator, &inputName));
        if (!error.empty() || !inputName) {
            outError = error.empty() ? "Failed to read ONNX input name." : error;
            ResetSession();
            m_LastStatusText = outError;
            return false;
        }
        m_InputName = inputName;
        api->AllocatorFree(m_Ort->allocator, inputName);
    }
    if (m_OutputName.empty()) {
        char* outputName = nullptr;
        error = StatusToString(api, api->SessionGetOutputName(m_Ort->session, 0, m_Ort->allocator, &outputName));
        if (!error.empty() || !outputName) {
            outError = error.empty() ? "Failed to read ONNX output name." : error;
            ResetSession();
            m_LastStatusText = outError;
            return false;
        }
        m_OutputName = outputName;
        api->AllocatorFree(m_Ort->allocator, outputName);
    }

    m_LoadedModelId = model.id;
    m_LastStatusText = m_LastSessionUsedCuda ? "Model loaded with CUDA provider" : "Model loaded with CPU provider";
    return true;
}

bool OnnxDenoiseBackend::IsModelLoaded(const std::string& modelId) const {
    return m_Ort && m_Ort->session && !modelId.empty() && modelId == m_LoadedModelId;
}

NeuralDenoiseInferenceResult OnnxDenoiseBackend::RunRgbInference(const NeuralDenoiseInferenceRequest& request) {
    NeuralDenoiseInferenceResult result;
    result.output = request.input;
    if (!request.input.IsValid()) {
        result.status = "Invalid RGB inference input.";
        m_LastStatusText = result.status;
        return result;
    }

    std::string loadError;
    if (!LoadModel(request.model, request.settings.runtimePreference, request.settings.allowCpuFallback, loadError)) {
        result.status = loadError;
        return result;
    }
    if (!m_Ort || !m_Ort->api || !m_Ort->session || !m_Ort->memoryInfo) {
        result.status = "ONNX Runtime session is not available.";
        m_LastStatusText = result.status;
        return result;
    }

    const int width = request.input.width;
    const int height = request.input.height;
    const std::size_t plane = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> inputTensor(plane * 3u, 0.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y * width + x);
            const std::size_t rgba = pixel * 4u;
            inputTensor[pixel] = Clamp01(request.input.rgba[rgba + 0]);
            inputTensor[plane + pixel] = Clamp01(request.input.rgba[rgba + 1]);
            inputTensor[plane * 2u + pixel] = Clamp01(request.input.rgba[rgba + 2]);
        }
    }

    std::array<int64_t, 4> shape = { 1, 3, height, width };
    OrtValue* inputValue = nullptr;
    OrtValue* outputValue = nullptr;
    const OrtApi* api = m_Ort->api;
    std::string error = StatusToString(api, api->CreateTensorWithDataAsOrtValue(
        m_Ort->memoryInfo,
        inputTensor.data(),
        inputTensor.size() * sizeof(float),
        shape.data(),
        shape.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &inputValue));
    if (!error.empty()) {
        result.status = "Failed to create ONNX input tensor: " + error;
        m_LastStatusText = result.status;
        return result;
    }

    const char* inputNames[] = { m_InputName.c_str() };
    const char* outputNames[] = { m_OutputName.c_str() };
    const OrtValue* inputs[] = { inputValue };
    error = StatusToString(api, api->Run(
        m_Ort->session,
        nullptr,
        inputNames,
        inputs,
        1,
        outputNames,
        1,
        &outputValue));
    api->ReleaseValue(inputValue);
    if (!error.empty()) {
        result.status = "Inference failed; bypassing: " + error;
        m_LastStatusText = result.status;
        return result;
    }
    if (!outputValue) {
        result.status = "Inference failed; ONNX output tensor missing.";
        m_LastStatusText = result.status;
        return result;
    }

    void* outputData = nullptr;
    error = StatusToString(api, api->GetTensorMutableData(outputValue, &outputData));
    if (!error.empty() || !outputData) {
        api->ReleaseValue(outputValue);
        result.status = error.empty() ? "Inference failed; output tensor data missing." : error;
        m_LastStatusText = result.status;
        return result;
    }

    const float* outputTensor = static_cast<const float*>(outputData);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y * width + x);
            const std::size_t rgba = pixel * 4u;
            result.output.rgba[rgba + 0] = outputTensor[pixel];
            result.output.rgba[rgba + 1] = outputTensor[plane + pixel];
            result.output.rgba[rgba + 2] = outputTensor[plane * 2u + pixel];
            result.output.rgba[rgba + 3] = request.input.rgba[rgba + 3];
        }
    }
    api->ReleaseValue(outputValue);

    result.success = true;
    result.usedCuda = m_LastSessionUsedCuda;
    result.usedCpu = m_LastSessionUsedCpu;
    result.status = m_LastSessionUsedCuda ? "Inference complete with CUDA provider" : "Inference complete with CPU provider";
    m_LastStatusText = result.status;
    return result;
}

void OnnxDenoiseBackend::ResetSession() {
    if (m_Ort) {
        m_Ort->ResetSession();
    }
    m_LoadedModelId.clear();
    m_InputName.clear();
    m_OutputName.clear();
    m_LastSessionUsedCuda = false;
    m_LastSessionUsedCpu = false;
}

} // namespace NeuralDenoise
