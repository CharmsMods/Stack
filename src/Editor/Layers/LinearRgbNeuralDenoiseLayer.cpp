#include "LinearRgbNeuralDenoiseLayer.h"

#include "Editor/EditorModule.h"
#include "NeuralDenoise/NeuralDenoiseManager.h"
#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <string>
#include <vector>

namespace {

const char* kCopyVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kCopyFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uInputTex;
void main() {
    FragColor = texture(uInputTex, vUV);
}
)";

template <typename Enum>
bool ComboEnum(const char* label, int* value, const char* const* labels, int count, float controlWidth) {
    ImGui::SetNextItemWidth(controlWidth);
    return ImGui::Combo(label, value, labels, count);
}

float ClampBlendStrength(float strength, float differenceAmount) {
    return std::clamp(strength, 0.0f, 1.0f) * std::clamp(differenceAmount, 0.0f, 2.0f);
}

int RoundUpToMultiple(int value, int multiple) {
    if (multiple <= 1) {
        return value;
    }
    return ((value + multiple - 1) / multiple) * multiple;
}

void HashCombine(std::uint64_t& seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

void HashCombineString(std::uint64_t& seed, const std::string& value) {
    HashCombine(seed, static_cast<std::uint64_t>(std::hash<std::string>{}(value)));
}

void HashCombineFloat(std::uint64_t& seed, float value) {
    HashCombine(seed, static_cast<std::uint64_t>(std::hash<float>{}(value)));
}

std::uint64_t FileWriteStamp(const std::string& path) {
    if (path.empty()) {
        return 0;
    }
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::uint64_t>(stamp.time_since_epoch().count());
}

constexpr double kCpuLargeRunMegapixels = 1.0;

std::filesystem::path StatusPathForNode(int nodeId) {
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::current_path(ec);
    const std::filesystem::path root = ec ? std::filesystem::path(".") : base;
    return root / ("neural_denoise_node_" + std::to_string(nodeId) + ".status");
}

} // namespace

LinearRgbNeuralDenoiseLayer::~LinearRgbNeuralDenoiseLayer() {
    if (m_CopyProgram) {
        glDeleteProgram(m_CopyProgram);
    }
    if (m_ResultTexture) {
        glDeleteTextures(1, &m_ResultTexture);
    }
}

void LinearRgbNeuralDenoiseLayer::InitializeGL() {
    if (m_CopyProgram == 0) {
        m_CopyProgram = GLHelpers::CreateShaderProgram(kCopyVert, kCopyFrag);
    }
}

void LinearRgbNeuralDenoiseLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    m_LastInputWidth = width;
    m_LastInputHeight = height;
    if (m_HasCachedResult && (m_ResultWidth != width || m_ResultHeight != height)) {
        ClearCachedResult("Cache cleared: image dimensions changed");
    }

    if (!m_Settings.enabled) {
        m_LastExecutionStatus = "Bypassed: node disabled";
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    if (!m_RunRequested) {
        if (m_Settings.runRequestRevision > m_HandledRunRequestRevision) {
            m_RunRequested = true;
        }
    }

    if (!m_RunRequested) {
        m_LastExecutionStatus = !m_LastError.empty()
            ? "Last error: " + m_LastError
            : "Ready: press Run Denoise";
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    NeuralDenoise::NeuralDenoiseManager& manager = NeuralDenoise::NeuralDenoiseManager::Instance();
    manager.EnsureInitialized();
    const NeuralDenoise::NeuralDenoiseModelInfo* selected = manager.FindModel(m_Settings.selectedModelId);
    if (!selected) {
        const std::string status = manager.OverallStatus();
        m_LastExecutionStatus = "Bypassed: " + (status.empty() ? std::string("no denoise model selected") : status);
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }
    const NeuralDenoise::ModelAvailability availability = manager.GetAvailability(selected);
    if (!availability.supported) {
        m_LastExecutionStatus = "Bypassed: " + availability.status;
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    const std::uint64_t cacheKey = BuildCacheKey(inputTexture, width, height, *selected);
    if (m_HasCachedResult && m_ResultTexture != 0 && m_CachedResultKey == cacheKey) {
        m_LastExecutionStatus = "Cached result";
        PublishRenderStatus();
        DrawCopy(m_ResultTexture, quad);
        return;
    }

    m_RunRequested = false;
    m_HandledRunRequestRevision = m_Settings.runRequestRevision;
    if (CpuRunNeedsConfirmation(width, height, *selected) && !m_AllowLargeCpuRunOnce && !m_Settings.runRequestAllowLargeCpu) {
        m_LastExecutionStatus = "CPU fallback large-image guard: press Run Large CPU Denoise to confirm";
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }
    m_AllowLargeCpuRunOnce = false;
    m_LastExecutionStatus = "Processing";

    NeuralDenoise::NeuralDenoiseImage image;
    if (!ReadTextureToImage(inputTexture, width, height, image)) {
        m_LastExecutionStatus = "Inference failed; texture readback failed, bypassing";
        m_LastError = m_LastExecutionStatus;
        m_LastFailedKey = cacheKey;
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    int tileCount = 0;
    if (!RunTiledInference(*selected, image, tileCount)) {
        m_LastError = m_LastExecutionStatus;
        m_LastFailedKey = cacheKey;
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }
    const auto stop = std::chrono::steady_clock::now();
    m_LastInferenceSeconds = std::chrono::duration<double>(stop - start).count();
    m_LastTileCount = tileCount;
    m_LastProvider = NeuralDenoise::NeuralDenoiseManager::Instance().OnnxBackend().LastSessionUsedCuda() ? "CUDA" : "CPU";
    if (!UploadAndDrawResult(image, quad)) {
        m_LastExecutionStatus = "Inference finished, but result upload failed; bypassing";
        m_LastError = m_LastExecutionStatus;
        m_LastFailedKey = cacheKey;
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }
    m_HasCachedResult = true;
    m_CachedResultKey = cacheKey;
    m_LastFailedKey = 0;
    m_LastError.clear();
    m_LastExecutionStatus = "Cached result";
    PublishRenderStatus();
}

void LinearRgbNeuralDenoiseLayer::DrawCopy(unsigned int inputTexture, FullscreenQuad& quad) {
    if (m_CopyProgram == 0) {
        InitializeGL();
    }
    glUseProgram(m_CopyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_CopyProgram, "uInputTex"), 0);
    quad.Draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

bool LinearRgbNeuralDenoiseLayer::ReadTextureToImage(
    unsigned int inputTexture,
    int width,
    int height,
    NeuralDenoise::NeuralDenoiseImage& outImage) {
    if (inputTexture == 0 || width <= 0 || height <= 0) {
        return false;
    }

    GLint prevReadFbo = 0;
    GLint prevDrawFbo = 0;
    GLint prevFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    GLuint readFbo = 0;
    glGenFramebuffers(1, &readFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, inputTexture, 0);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glDeleteFramebuffers(1, &readFbo);
        return false;
    }

    std::vector<float> bottomLeft(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0.0f);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, bottomLeft.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glDeleteFramebuffers(1, &readFbo);

    outImage.width = width;
    outImage.height = height;
    outImage.channels = 4;
    outImage.rgba.assign(bottomLeft.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        const int sourceY = height - 1 - y;
        const std::size_t dst = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        const std::size_t src = static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(width) * 4u;
        std::copy(bottomLeft.begin() + src, bottomLeft.begin() + src + static_cast<std::size_t>(width) * 4u, outImage.rgba.begin() + dst);
    }
    return outImage.IsValid();
}

bool LinearRgbNeuralDenoiseLayer::RunTiledInference(
    const NeuralDenoise::NeuralDenoiseModelInfo& model,
    NeuralDenoise::NeuralDenoiseImage& image,
    int& outTileCount) {
    if (!image.IsValid()) {
        m_LastExecutionStatus = "Inference failed; invalid image buffer";
        return false;
    }

    NeuralDenoise::TilePlan plan = m_Settings.tilePlan;
    plan.tileSize = std::clamp(plan.tileSize, 64, 4096);
    plan.overlap = std::clamp(plan.overlap, 0, std::max(0, plan.tileSize / 2 - 1));
    if (plan.tileSize <= plan.overlap * 2) {
        plan.overlap = std::max(0, plan.tileSize / 2 - 1);
    }
    const int inputMultiple = std::clamp(model.requiredInputMultiple, 1, 512);
    if (inputMultiple > 1) {
        plan.tileSize = std::clamp(RoundUpToMultiple(plan.tileSize, inputMultiple), inputMultiple, 4096);
    }

    NeuralDenoise::NeuralDenoiseImage original = image;
    NeuralDenoise::NeuralDenoiseImage modelOutput = image;
    NeuralDenoise::OnnxDenoiseBackend& backend = NeuralDenoise::NeuralDenoiseManager::Instance().OnnxBackend();
    const float blend = ClampBlendStrength(m_Settings.strength, m_Settings.differenceAmount);

    const int width = image.width;
    const int height = image.height;
    const int tile = std::max(1, plan.tileSize);
    const int overlap = std::max(0, plan.overlap);
    int tileCount = 0;

    for (int coreY = 0; coreY < height; coreY += tile) {
        const int coreH = std::min(tile, height - coreY);
        const int padY0 = std::max(0, coreY - overlap);
        const int padY1 = std::min(height, coreY + coreH + overlap);
        for (int coreX = 0; coreX < width; coreX += tile) {
            const int coreW = std::min(tile, width - coreX);
            const int padX0 = std::max(0, coreX - overlap);
            const int padX1 = std::min(width, coreX + coreW + overlap);
            const int tileW = padX1 - padX0;
            const int tileH = padY1 - padY0;
            const int inferW = inputMultiple > 1 ? RoundUpToMultiple(tileW, inputMultiple) : tileW;
            const int inferH = inputMultiple > 1 ? RoundUpToMultiple(tileH, inputMultiple) : tileH;

            NeuralDenoise::NeuralDenoiseImage tileInput;
            tileInput.width = inferW;
            tileInput.height = inferH;
            tileInput.channels = 4;
            tileInput.rgba.assign(static_cast<std::size_t>(inferW) * static_cast<std::size_t>(inferH) * 4u, 0.0f);
            for (int y = 0; y < inferH; ++y) {
                const int sampleY = std::min(padY0 + y, height - 1);
                for (int x = 0; x < inferW; ++x) {
                    const int sampleX = std::min(padX0 + x, width - 1);
                    const std::size_t src = (static_cast<std::size_t>(sampleY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sampleX)) * 4u;
                    const std::size_t dst = (static_cast<std::size_t>(y) * static_cast<std::size_t>(inferW) + static_cast<std::size_t>(x)) * 4u;
                    tileInput.rgba[dst + 0] = original.rgba[src + 0];
                    tileInput.rgba[dst + 1] = original.rgba[src + 1];
                    tileInput.rgba[dst + 2] = original.rgba[src + 2];
                    tileInput.rgba[dst + 3] = original.rgba[src + 3];
                }
            }

            NeuralDenoise::NeuralDenoiseInferenceRequest request;
            request.model = model;
            request.settings = m_Settings;
            request.input = std::move(tileInput);
            NeuralDenoise::NeuralDenoiseInferenceResult result = backend.RunRgbInference(request);
            if (!result.success || !result.output.IsValid()) {
                m_LastExecutionStatus = result.status.empty() ? "Inference failed; bypassing" : result.status;
                image = std::move(original);
                return false;
            }

            const int srcCoreX = coreX - padX0;
            const int srcCoreY = coreY - padY0;
            for (int y = 0; y < coreH; ++y) {
                for (int x = 0; x < coreW; ++x) {
                    const std::size_t dst = (static_cast<std::size_t>(coreY + y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(coreX + x)) * 4u;
                    const std::size_t src = (static_cast<std::size_t>(srcCoreY + y) * static_cast<std::size_t>(result.output.width) + static_cast<std::size_t>(srcCoreX + x)) * 4u;
                    modelOutput.rgba[dst + 0] = result.output.rgba[src + 0];
                    modelOutput.rgba[dst + 1] = result.output.rgba[src + 1];
                    modelOutput.rgba[dst + 2] = result.output.rgba[src + 2];
                    modelOutput.rgba[dst + 3] = original.rgba[dst + 3];
                }
            }
            ++tileCount;
        }
    }

    for (std::size_t i = 0; i < original.rgba.size(); i += 4u) {
        const std::size_t pixel = i / 4u;
        const int x = static_cast<int>(pixel % static_cast<std::size_t>(width));
        const float denoisedR = original.rgba[i + 0] + (modelOutput.rgba[i + 0] - original.rgba[i + 0]) * blend;
        const float denoisedG = original.rgba[i + 1] + (modelOutput.rgba[i + 1] - original.rgba[i + 1]) * blend;
        const float denoisedB = original.rgba[i + 2] + (modelOutput.rgba[i + 2] - original.rgba[i + 2]) * blend;
        if (m_Settings.previewMode == NeuralDenoise::PreviewMode::Original) {
            image.rgba[i + 0] = original.rgba[i + 0];
            image.rgba[i + 1] = original.rgba[i + 1];
            image.rgba[i + 2] = original.rgba[i + 2];
        } else if (m_Settings.previewMode == NeuralDenoise::PreviewMode::Difference) {
            image.rgba[i + 0] = std::abs(denoisedR - original.rgba[i + 0]);
            image.rgba[i + 1] = std::abs(denoisedG - original.rgba[i + 1]);
            image.rgba[i + 2] = std::abs(denoisedB - original.rgba[i + 2]);
        } else if (m_Settings.previewMode == NeuralDenoise::PreviewMode::Split && x < width / 2) {
            image.rgba[i + 0] = original.rgba[i + 0];
            image.rgba[i + 1] = original.rgba[i + 1];
            image.rgba[i + 2] = original.rgba[i + 2];
        } else {
            image.rgba[i + 0] = denoisedR;
            image.rgba[i + 1] = denoisedG;
            image.rgba[i + 2] = denoisedB;
        }
        image.rgba[i + 3] = original.rgba[i + 3];
    }

    outTileCount = tileCount;
    m_LastExecutionStatus = "Tiled inference active: " + std::to_string(tileCount) +
        (backend.LastSessionUsedCuda() ? " tile(s), CUDA provider" : " tile(s), CPU provider");
    return true;
}

std::uint64_t LinearRgbNeuralDenoiseLayer::BuildCacheKey(
    unsigned int inputTexture,
    int width,
    int height,
    const NeuralDenoise::NeuralDenoiseModelInfo& model) const {
    std::uint64_t seed = 0x4e44524c47424341ull;
    HashCombine(seed, static_cast<std::uint64_t>(inputTexture));
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, width)));
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, height)));
    HashCombineString(seed, model.id);
    HashCombineString(seed, model.resolvedPath);
    HashCombine(seed, FileWriteStamp(model.resolvedPath));
    HashCombine(seed, FileWriteStamp(model.resolvedPath + ".data"));
    HashCombineString(seed, m_Settings.selectedModelId);
    HashCombine(seed, static_cast<std::uint64_t>(m_Settings.runtimePreference));
    HashCombine(seed, static_cast<std::uint64_t>(m_Settings.qualityMode));
    HashCombine(seed, static_cast<std::uint64_t>(m_Settings.previewMode));
    HashCombine(seed, static_cast<std::uint64_t>(m_Settings.alphaMode));
    HashCombineFloat(seed, m_Settings.strength);
    HashCombineFloat(seed, m_Settings.detailPreservation);
    HashCombineFloat(seed, m_Settings.shadowsStrength);
    HashCombineFloat(seed, m_Settings.highlightProtection);
    HashCombineFloat(seed, m_Settings.differenceAmount);
    HashCombineFloat(seed, m_Settings.chromaStrength);
    HashCombineFloat(seed, m_Settings.lumaStrength);
    HashCombineFloat(seed, m_Settings.fineGrainStrength);
    HashCombineFloat(seed, m_Settings.blotchStrength);
    HashCombine(seed, m_Settings.hotDeadPixelCleanup ? 1u : 0u);
    HashCombine(seed, m_Settings.shadowBiasedDenoise ? 1u : 0u);
    HashCombine(seed, m_Settings.workInLinearRgb ? 1u : 0u);
    HashCombine(seed, m_Settings.preserveAlpha ? 1u : 0u);
    HashCombine(seed, m_Settings.allowCpuFallback ? 1u : 0u);
    HashCombine(seed, m_Settings.externalMaskInfluence ? 1u : 0u);
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, m_Settings.runRequestRevision)));
    HashCombine(seed, m_Settings.runRequestAllowLargeCpu ? 1u : 0u);
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, m_Settings.tilePlan.tileSize)));
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, m_Settings.tilePlan.overlap)));
    HashCombine(seed, m_Settings.tilePlan.featherMerge ? 1u : 0u);
    return seed;
}

bool LinearRgbNeuralDenoiseLayer::CpuRunNeedsConfirmation(
    int width,
    int height,
    const NeuralDenoise::NeuralDenoiseModelInfo& model) const {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const double megapixels = (static_cast<double>(width) * static_cast<double>(height)) / 1000000.0;
    if (megapixels <= kCpuLargeRunMegapixels) {
        return false;
    }
    const bool cudaPreferred = m_Settings.runtimePreference == NeuralDenoise::RuntimePreference::Cuda ||
        (m_Settings.runtimePreference == NeuralDenoise::RuntimePreference::Auto && model.preferredBackend == "onnx_cuda");
    const bool cpuExplicit = m_Settings.runtimePreference == NeuralDenoise::RuntimePreference::Cpu;
    const NeuralDenoise::NeuralDenoiseManager& manager = NeuralDenoise::NeuralDenoiseManager::Instance();
    if (!manager.IsInitialized()) {
        return false;
    }
    const bool cudaUnavailable = !manager.OnnxBackend().HasCudaProvider();
    return cpuExplicit || (m_Settings.allowCpuFallback && (!cudaPreferred || cudaUnavailable));
}

void LinearRgbNeuralDenoiseLayer::MarkDenoiseUiDirty(EditorModule* editor) const {
    if (!editor) {
        return;
    }
    if (m_ActiveUiNodeId >= 0) {
        editor->MarkRenderDirty(m_ActiveUiNodeId);
    } else {
        editor->MarkSelectedLayerRenderDirty();
    }
}

void LinearRgbNeuralDenoiseLayer::PublishRenderStatus() const {
    if (m_RenderNodeId < 0) {
        return;
    }
    std::ofstream output(StatusPathForNode(m_RenderNodeId), std::ios::trunc);
    if (!output) {
        return;
    }
    output << "status=" << m_LastExecutionStatus << "\n";
    output << "error=" << m_LastError << "\n";
    output << "provider=" << m_LastProvider << "\n";
    output << "tiles=" << m_LastTileCount << "\n";
    output << "seconds=" << m_LastInferenceSeconds << "\n";
    output << "width=" << m_LastInputWidth << "\n";
    output << "height=" << m_LastInputHeight << "\n";
}

void LinearRgbNeuralDenoiseLayer::RefreshPublishedRenderStatus() {
    const int nodeId = m_ActiveUiNodeId >= 0 ? m_ActiveUiNodeId : m_Settings.renderNodeId;
    if (nodeId < 0) {
        return;
    }
    std::ifstream input(StatusPathForNode(nodeId));
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        if (key == "status" && !value.empty()) {
            m_LastExecutionStatus = value;
        } else if (key == "error") {
            m_LastError = value;
        } else if (key == "provider" && !value.empty()) {
            m_LastProvider = value;
        } else if (key == "tiles") {
            try { m_LastTileCount = std::stoi(value); } catch (...) {}
        } else if (key == "seconds") {
            try { m_LastInferenceSeconds = std::stod(value); } catch (...) {}
        } else if (key == "width") {
            try { m_LastInputWidth = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "height") {
            try { m_LastInputHeight = std::max(0, std::stoi(value)); } catch (...) {}
        }
    }
}

int LinearRgbNeuralDenoiseLayer::EstimateTileCount(
    int width,
    int height,
    const NeuralDenoise::NeuralDenoiseModelInfo* model) const {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    int tileSize = std::clamp(m_Settings.tilePlan.tileSize, 64, 4096);
    if (model && model->requiredInputMultiple > 1) {
        tileSize = std::clamp(RoundUpToMultiple(tileSize, std::clamp(model->requiredInputMultiple, 1, 512)), 64, 4096);
    }
    return ((width + tileSize - 1) / tileSize) * ((height + tileSize - 1) / tileSize);
}

std::string LinearRgbNeuralDenoiseLayer::ExpectedProviderLabel(const NeuralDenoise::NeuralDenoiseModelInfo* model) const {
    using namespace NeuralDenoise;
    const bool cudaPreferred = model && (m_Settings.runtimePreference == RuntimePreference::Cuda ||
        (m_Settings.runtimePreference == RuntimePreference::Auto && model->preferredBackend == "onnx_cuda"));
    const NeuralDenoiseManager& manager = NeuralDenoiseManager::Instance();
    if (!manager.IsInitialized()) {
        return "Idle until Run Denoise";
    }
    const bool cudaAvailable = manager.OnnxBackend().HasCudaProvider();
    if (m_Settings.runtimePreference == RuntimePreference::Cpu) {
        return "CPU explicit";
    }
    if (cudaPreferred && cudaAvailable) {
        return "CUDA";
    }
    if (cudaPreferred && !cudaAvailable && m_Settings.allowCpuFallback) {
        return "CPU fallback";
    }
    if (cudaPreferred && !cudaAvailable) {
        return "Blocked: CUDA unavailable, CPU fallback off";
    }
    return m_Settings.allowCpuFallback ? "CPU fallback" : "Blocked: CPU fallback off";
}

std::string LinearRgbNeuralDenoiseLayer::ReadinessLabel(
    const NeuralDenoise::NeuralDenoiseModelInfo* model,
    const NeuralDenoise::ModelAvailability& availability) const {
    using namespace NeuralDenoise;
    const NeuralDenoiseManager& manager = NeuralDenoiseManager::Instance();
    if (!m_Settings.enabled) {
        return "Disabled";
    }
    if (!manager.IsInitialized()) {
        return "Idle until Run Denoise";
    }
    if (!manager.IsDenoiseFolderPresent()) {
        return "Not installed";
    }
    if (!manager.IsManifestLoaded()) {
        return "Manifest missing or invalid";
    }
    if (!model) {
        return "No model selected";
    }
    if (!availability.modelFileAvailable) {
        return "Model missing";
    }
    if (!availability.runtimeAvailable) {
        return "Runtime missing";
    }
    if (!availability.supported) {
        return availability.status.empty() ? "Unsupported" : availability.status;
    }
    const bool cudaPreferred = m_Settings.runtimePreference == RuntimePreference::Cuda ||
        (m_Settings.runtimePreference == RuntimePreference::Auto && model->preferredBackend == "onnx_cuda");
    if (cudaPreferred && !manager.OnnxBackend().HasCudaProvider() && !m_Settings.allowCpuFallback) {
        return "CUDA unavailable; CPU fallback disabled";
    }
    if (!m_LastError.empty() && m_LastExecutionStatus.rfind("Last error:", 0) == 0) {
        return "Failed";
    }
    if (m_HasCachedResult) {
        return "Cached result";
    }
    if (m_LastExecutionStatus == "Processing" || m_LastExecutionStatus.rfind("Queued", 0) == 0) {
        return m_LastExecutionStatus;
    }
    if (cudaPreferred && manager.OnnxBackend().HasCudaProvider()) {
        return "Ready for CUDA";
    }
    if (m_Settings.runtimePreference == RuntimePreference::Cpu) {
        return "Ready for CPU";
    }
    if (m_Settings.allowCpuFallback) {
        return "Ready for CPU fallback";
    }
    return "Ready";
}

void LinearRgbNeuralDenoiseLayer::ClearCachedResult(const char* status) {
    if (m_ResultTexture) {
        glDeleteTextures(1, &m_ResultTexture);
        m_ResultTexture = 0;
    }
    m_ResultWidth = 0;
    m_ResultHeight = 0;
    m_CachedResultKey = 0;
    m_LastFailedKey = 0;
    m_HasCachedResult = false;
    m_RunRequested = false;
    m_AllowLargeCpuRunOnce = false;
    m_LastTileCount = 0;
    m_LastInferenceSeconds = 0.0;
    m_LastProvider = "None";
    m_HandledRunRequestRevision = 0;
    if (status) {
        m_LastExecutionStatus = status;
    }
}

bool LinearRgbNeuralDenoiseLayer::UploadAndDrawResult(
    const NeuralDenoise::NeuralDenoiseImage& image,
    FullscreenQuad& quad) {
    if (!image.IsValid()) {
        return false;
    }
    if (m_ResultTexture == 0 || m_ResultWidth != image.width || m_ResultHeight != image.height) {
        if (m_ResultTexture) {
            glDeleteTextures(1, &m_ResultTexture);
            m_ResultTexture = 0;
        }
        glGenTextures(1, &m_ResultTexture);
        glBindTexture(GL_TEXTURE_2D, m_ResultTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, image.width, image.height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_ResultWidth = image.width;
        m_ResultHeight = image.height;
    } else {
        glBindTexture(GL_TEXTURE_2D, m_ResultTexture);
    }

    std::vector<float> bottomLeft(image.rgba.size(), 0.0f);
    for (int y = 0; y < image.height; ++y) {
        const int dstY = image.height - 1 - y;
        const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4u;
        const std::size_t dst = static_cast<std::size_t>(dstY) * static_cast<std::size_t>(image.width) * 4u;
        std::copy(image.rgba.begin() + src, image.rgba.begin() + src + static_cast<std::size_t>(image.width) * 4u, bottomLeft.begin() + dst);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height, GL_RGBA, GL_FLOAT, bottomLeft.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    DrawCopy(m_ResultTexture, quad);
    return true;
}

void LinearRgbNeuralDenoiseLayer::RenderUI() {
    RenderUI(nullptr);
}

void LinearRgbNeuralDenoiseLayer::RenderUI(EditorModule* editor) {
    RefreshPublishedRenderStatus();
    const float controlWidth = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    if (editor && editor->SelectedLayerInputContainsViewTransform()) {
        ImGui::TextWrapped("Warning: this node appears after View Transform. Neural denoise is intended for scene-linear or normal RGB before display compression.");
    }

    RenderModelSection(controlWidth, editor);
    RenderExecutionSection(controlWidth, editor);
    RenderBlendSection(controlWidth);
    RenderNoiseSection(controlWidth);
    RenderLinearRgbSection(controlWidth);
    RenderPreviewSection(controlWidth);
    RenderTilingSection(controlWidth);
    ImGui::TextWrapped("Execution: %s", m_LastExecutionStatus.c_str());
}

NodeSurfaceSpec LinearRgbNeuralDenoiseLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 420.0f;
    spec.maxWidth = 520.0f;
    return spec;
}

void LinearRgbNeuralDenoiseLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    m_ActiveUiNodeId = context.nodeId;
    m_Settings.renderNodeId = context.nodeId;
    RenderUI(editor);
    m_ActiveUiNodeId = -1;
}

json LinearRgbNeuralDenoiseLayer::Serialize() const {
    json j = NeuralDenoise::SerializeSettings(m_Settings);
    j["type"] = "LinearRgbNeuralDenoise";
    return j;
}

void LinearRgbNeuralDenoiseLayer::Deserialize(const json& j) {
    m_Settings = NeuralDenoise::DeserializeSettings(j);
    m_RenderNodeId = m_Settings.renderNodeId;
    ClearCachedResult("Cache cleared after load");
}

void LinearRgbNeuralDenoiseLayer::RenderModelSection(float controlWidth, EditorModule* editor) {
    using namespace NeuralDenoise;
    NeuralDenoiseManager& manager = NeuralDenoiseManager::Instance();

    ImGuiExtras::RichSectionLabel("MODEL", 4.0f);
    if (ImGuiExtras::NodeCheckbox("Enable", "##NeuralDenoiseEnabled", &m_Settings.enabled, controlWidth)) {
    }
    if (ImGui::Button("Refresh Model Pack")) {
        manager.Refresh();
        ClearCachedResult("Model pack refreshed");
        MarkDenoiseUiDirty(editor);
        ImGui::TextDisabled("Model pack refreshed.");
        return;
    }
    if (!manager.IsInitialized()) {
        ImGui::TextDisabled("Readiness: idle until Run Denoise");
        ImGui::TextWrapped("This node stays cold on project load. Press Run Denoise or Refresh Model Pack before models and runtimes are scanned.");
        return;
    }

    const std::vector<const NeuralDenoiseModelInfo*> models = [&]() {
        std::vector<const NeuralDenoiseModelInfo*> result = manager.ModelsOfType(ModelType::LinearRgb);
        const std::vector<const NeuralDenoiseModelInfo*> generic = manager.ModelsOfType(ModelType::GenericRgb);
        result.insert(result.end(), generic.begin(), generic.end());
        return result;
    }();

    if (m_Settings.selectedModelId.empty() && !models.empty()) {
        m_Settings.selectedModelId = models.front()->id;
    }

    const NeuralDenoiseModelInfo* selected = manager.FindModel(m_Settings.selectedModelId);
    const std::string currentLabel = selected ? selected->displayName : std::string("No model selected");
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::BeginCombo("Selected Model", currentLabel.c_str())) {
        for (const NeuralDenoiseModelInfo* model : models) {
            const bool isSelected = model && model->id == m_Settings.selectedModelId;
            if (model && ImGui::Selectable(model->displayName.c_str(), isSelected)) {
                if (m_Settings.selectedModelId != model->id) {
                    m_Settings.selectedModelId = model->id;
                    ClearCachedResult("Cache cleared: model changed");
                }
                if (model->tileHints.tileSize > 0) {
                    m_Settings.tilePlan = model->tileHints;
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        if (models.empty()) {
            ImGui::TextDisabled("No RGB neural denoise models in manifest.");
        }
        ImGui::EndCombo();
    }
    selected = manager.FindModel(m_Settings.selectedModelId);

    const char* runtimeLabels[] = { "Auto", "CUDA", "CPU explicit", "DirectML future", "TensorRT future" };
    int runtime = static_cast<int>(m_Settings.runtimePreference);
    if (ComboEnum<RuntimePreference>("Runtime / Provider", &runtime, runtimeLabels, 5, controlWidth)) {
        m_Settings.runtimePreference = static_cast<RuntimePreference>(std::clamp(runtime, 0, 4));
    }

    const char* qualityLabels[] = { "Quality", "Balanced", "Fast" };
    int quality = static_cast<int>(m_Settings.qualityMode);
    if (ComboEnum<QualityMode>("Quality Mode", &quality, qualityLabels, 3, controlWidth)) {
        m_Settings.qualityMode = static_cast<QualityMode>(std::clamp(quality, 0, 2));
    }

    const ModelAvailability currentAvailability = manager.GetAvailability(selected);
    ImGui::TextDisabled("Readiness: %s", ReadinessLabel(selected, currentAvailability).c_str());
    ImGui::TextDisabled("Folder: %s", manager.RootDirectory().string().c_str());
    ImGui::TextDisabled("Status: %s", currentAvailability.status.c_str());
    const BackendStatus backendStatus = manager.OnnxBackend().GetStatus();
    ImGui::TextDisabled("Backend: %s", backendStatus.status.empty() ? "unknown" : backendStatus.status.c_str());
    ImGui::TextDisabled("CUDA: %s", manager.OnnxBackend().HasCudaProvider() ? "available" : "unavailable");
    const std::string& cudaProviderStatus = manager.OnnxBackend().CudaProviderStatus();
    if (!cudaProviderStatus.empty()) {
        ImGui::TextWrapped("%s", cudaProviderStatus.c_str());
    }
    if (selected) {
        ImGui::TextDisabled("Model type: %s", ModelTypeLabel(selected->type));
        ImGui::TextDisabled("Architecture: %s", selected->architecture.empty() ? "unspecified" : selected->architecture.c_str());
        ImGui::TextDisabled("Input: %s / %s", selected->inputFormat.empty() ? "nchw" : selected->inputFormat.c_str(), selected->inputRange.empty() ? "0_1" : selected->inputRange.c_str());
        if (selected->requiredInputMultiple > 1) {
            ImGui::TextDisabled("Input multiple: %d px", selected->requiredInputMultiple);
        }
        ImGui::TextDisabled("File: %s", selected->relativeFile.c_str());
        ImGui::TextDisabled("Resolved: %s", selected->resolvedPath.c_str());
        if (!selected->license.empty()) {
            ImGui::TextWrapped("License: %s", selected->license.c_str());
        }
    }
    for (const std::string& warning : currentAvailability.warnings) {
        ImGui::TextWrapped("%s", warning.c_str());
    }
}

void LinearRgbNeuralDenoiseLayer::RenderExecutionSection(float controlWidth, EditorModule* editor) {
    ImGuiExtras::RichSectionLabel("EXECUTION", 4.0f);
    NeuralDenoise::NeuralDenoiseManager& manager = NeuralDenoise::NeuralDenoiseManager::Instance();
    const bool managerInitialized = manager.IsInitialized();
    const NeuralDenoise::NeuralDenoiseModelInfo* selected = managerInitialized ? manager.FindModel(m_Settings.selectedModelId) : nullptr;
    const NeuralDenoise::ModelAvailability availability = managerInitialized
        ? manager.GetAvailability(selected)
        : NeuralDenoise::ModelAvailability{};
    ImGuiExtras::NodeCheckbox("Allow CPU Fallback", "##NeuralAllowCpuFallback", &m_Settings.allowCpuFallback, controlWidth);
    const double megapixels = (static_cast<double>(std::max(0, m_LastInputWidth)) * static_cast<double>(std::max(0, m_LastInputHeight))) / 1000000.0;
    const bool largeCpuCandidate = managerInitialized && m_Settings.allowCpuFallback && megapixels > kCpuLargeRunMegapixels &&
        !manager.OnnxBackend().HasCudaProvider();
    const bool renderRequestedLargeCpuConfirmation =
        m_LastExecutionStatus.find("Run Large CPU Denoise") != std::string::npos ||
        m_LastExecutionStatus.find("large-image guard") != std::string::npos;
    ImGui::TextDisabled("Readiness: %s", ReadinessLabel(selected, availability).c_str());
    ImGui::TextDisabled("Image: %d x %d (%.2f MP)", std::max(0, m_LastInputWidth), std::max(0, m_LastInputHeight), megapixels);
    ImGui::TextDisabled("Estimated tiles: %d", EstimateTileCount(m_LastInputWidth, m_LastInputHeight, selected));
    ImGui::TextDisabled("Expected provider: %s", ExpectedProviderLabel(selected).c_str());
    if (ImGui::Button(m_HasCachedResult ? "Refresh Denoise" : "Run Denoise")) {
        ++m_Settings.runRequestRevision;
        m_Settings.runRequestAllowLargeCpu = false;
        m_RunRequested = true;
        m_AllowLargeCpuRunOnce = false;
        m_LastExecutionStatus = "Queued";
        MarkDenoiseUiDirty(editor);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Cached Result")) {
        ClearCachedResult("Cached result cleared");
        MarkDenoiseUiDirty(editor);
    }
    if (largeCpuCandidate || renderRequestedLargeCpuConfirmation) {
        ImGui::TextWrapped("CPU fallback is enabled for a %.2f MP image. This can stall the editor during development.", megapixels);
        if (ImGui::Button("Run Large CPU Denoise")) {
            ++m_Settings.runRequestRevision;
            m_Settings.runRequestAllowLargeCpu = true;
            m_RunRequested = true;
            m_AllowLargeCpuRunOnce = true;
            m_LastExecutionStatus = "Queued large CPU run";
            MarkDenoiseUiDirty(editor);
        }
    }
    ImGui::TextDisabled("Precision: FP32");
    ImGui::TextDisabled("Quantization: off");
    ImGui::TextDisabled("Cache: %s", m_HasCachedResult ? "cached result available" : "empty");
    ImGui::TextDisabled("Last inference: %.2f s", m_LastInferenceSeconds);
    ImGui::TextDisabled("Last tile count: %d", m_LastTileCount);
    ImGui::TextDisabled("Last provider: %s", m_LastProvider.c_str());
    if (!m_LastError.empty()) {
        ImGui::TextWrapped("Last error: %s", m_LastError.c_str());
    }
    ImGui::TextWrapped("0..1 model input mode clamps only the tensor sent to ONNX. The original RGB values remain the blend reference.");
    ImGui::TextWrapped("Runtime: %s", m_LastExecutionStatus.c_str());
}

void LinearRgbNeuralDenoiseLayer::RenderBlendSection(float controlWidth) {
    ImGuiExtras::RichSectionLabel("BLEND", 4.0f);
    ImGuiExtras::NodeSliderFloat("Overall Strength", "##NeuralStrength", &m_Settings.strength, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Difference Amount", "##NeuralDifference", &m_Settings.differenceAmount, 0.0f, 2.0f, "%.2f", controlWidth);
    ImGui::TextDisabled("Future blend controls are stored but not applied by the current Restormer path.");
    ImGui::BeginDisabled();
    ImGuiExtras::NodeSliderFloat("Detail Preservation", "##NeuralDetail", &m_Settings.detailPreservation, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Shadows Strength", "##NeuralShadows", &m_Settings.shadowsStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Highlight Protection", "##NeuralHighlights", &m_Settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeCheckbox("External Mask Influence", "##NeuralMaskInfluence", &m_Settings.externalMaskInfluence, controlWidth);
    ImGui::EndDisabled();
}

void LinearRgbNeuralDenoiseLayer::RenderNoiseSection(float controlWidth) {
    ImGuiExtras::RichSectionLabel("NOISE TARGETING", 4.0f);
    ImGui::TextDisabled("Future controls; current Restormer inference uses model output blended by strength.");
    ImGui::BeginDisabled();
    ImGuiExtras::NodeSliderFloat("Chroma Noise", "##NeuralChroma", &m_Settings.chromaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Luma Noise", "##NeuralLuma", &m_Settings.lumaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Fine Grain", "##NeuralFineGrain", &m_Settings.fineGrainStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Blotch / Splotch", "##NeuralBlotch", &m_Settings.blotchStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeCheckbox("Hot / Dead Pixel Cleanup", "##NeuralHotDead", &m_Settings.hotDeadPixelCleanup, controlWidth);
    ImGuiExtras::NodeCheckbox("Shadow-Biased Denoise", "##NeuralShadowBias", &m_Settings.shadowBiasedDenoise, controlWidth);
    ImGui::EndDisabled();
}

void LinearRgbNeuralDenoiseLayer::RenderLinearRgbSection(float controlWidth) {
    using namespace NeuralDenoise;
    ImGuiExtras::RichSectionLabel("LINEAR RGB", 4.0f);
    ImGui::TextDisabled("Current contract: NCHW FP32 RGB 0..1 model input; alpha is always preserved.");
    ImGui::BeginDisabled();
    ImGuiExtras::NodeCheckbox("Work In Linear RGB", "##NeuralLinear", &m_Settings.workInLinearRgb, controlWidth);
    m_Settings.preserveAlpha = true;
    ImGuiExtras::NodeCheckbox("Preserve Alpha", "##NeuralPreserveAlpha", &m_Settings.preserveAlpha, controlWidth);
    const char* alphaLabels[] = { "Preserve", "Ignore", "Use as mask" };
    m_Settings.alphaMode = AlphaMode::Preserve;
    int alpha = static_cast<int>(m_Settings.alphaMode);
    if (ComboEnum<AlphaMode>("Process Alpha", &alpha, alphaLabels, 3, controlWidth)) {
        m_Settings.alphaMode = static_cast<AlphaMode>(std::clamp(alpha, 0, 2));
    }
    ImGui::EndDisabled();
}

void LinearRgbNeuralDenoiseLayer::RenderPreviewSection(float controlWidth) {
    using namespace NeuralDenoise;
    ImGuiExtras::RichSectionLabel("PREVIEW", 4.0f);
    const char* previewLabels[] = {
        "Denoised",
        "Original",
        "Difference",
        "Split view"
    };
    int preview = std::clamp(static_cast<int>(m_Settings.previewMode), 0, 3);
    if (ComboEnum<PreviewMode>("Mode", &preview, previewLabels, 4, controlWidth)) {
        m_Settings.previewMode = static_cast<PreviewMode>(std::clamp(preview, 0, 3));
    }
    if (static_cast<int>(m_Settings.previewMode) > 3) {
        ImGui::TextDisabled("Chroma/luma difference previews are future work; using Denoised.");
        m_Settings.previewMode = PreviewMode::Denoised;
    }
}

void LinearRgbNeuralDenoiseLayer::RenderTilingSection(float controlWidth) {
    ImGuiExtras::RichSectionLabel("TILING", 4.0f);
    ImGuiExtras::NodeSliderInt("Tile Size", "##NeuralTileSize", &m_Settings.tilePlan.tileSize, 128, 2048, "%d px", controlWidth);
    ImGuiExtras::NodeSliderInt("Overlap", "##NeuralTileOverlap", &m_Settings.tilePlan.overlap, 0, 512, "%d px", controlWidth);
    ImGuiExtras::NodeCheckbox("Feather Merge", "##NeuralTileFeather", &m_Settings.tilePlan.featherMerge, controlWidth);
    m_Settings.tilePlan.tileSize = std::clamp(m_Settings.tilePlan.tileSize, 64, 4096);
    m_Settings.tilePlan.overlap = std::clamp(m_Settings.tilePlan.overlap, 0, std::max(0, m_Settings.tilePlan.tileSize / 2 - 1));
    ImGui::TextDisabled("Safe tiled path copies only each tile core into the final image.");
    if (m_Settings.tilePlan.featherMerge) {
        ImGui::TextDisabled("Feather merge is reserved for a future refinement; core crop merge is used now.");
    }
}
