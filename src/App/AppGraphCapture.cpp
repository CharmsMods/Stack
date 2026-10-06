#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "App/AppShell.h"

#include "Async/TaskSystem.h"
#include "Notifications/AsyncActivity.h"
#include "Editor/GraphCapture.h"
#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Utils/PixelBufferUtils.h"
#include "Renderer/GLLoader.h"
#include "Utils/ImageClipboard.h"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace GraphCapture = Stack::EditorGraphCapture;
constexpr GLenum kGlActiveTexture = 0x84E0;
constexpr GLenum kGlMaxRenderbufferSize = 0x84E8;

GraphCapture::ResolutionLimits QueryRuntimeCaptureLimits() {
    GLint textureLimit = 0;
    GLint renderbufferLimit = 0;
    GLint viewportLimits[2] = { 0, 0 };
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    glGetIntegerv(kGlMaxRenderbufferSize, &renderbufferLimit);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewportLimits);

    int edgeLimit = 16384;
    for (GLint value : { textureLimit, renderbufferLimit, viewportLimits[0], viewportLimits[1] }) {
        if (value > 0) {
            edgeLimit = std::min(edgeLimit, static_cast<int>(value));
        }
    }
    GraphCapture::ResolutionLimits limits;
    limits.maxEdge = std::max(256, edgeLimit);
    return limits;
}

void DrawCaptureBackground(
    const StackAppearance::AppearanceManager& appearance,
    GraphCapture::Background mode,
    unsigned int wallpaperTexture,
    int wallpaperWidth,
    int wallpaperHeight,
    const ImVec2& canvasSize) {
    if (mode == GraphCapture::Background::Transparent) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 baseColor = appearance.GetEffectiveWindowBackgroundColor();
    baseColor.w = 1.0f;
    drawList->AddRectFilled(
        ImVec2(0.0f, 0.0f),
        canvasSize,
        ImGui::ColorConvertFloat4ToU32(baseColor));

    if (mode != GraphCapture::Background::CurrentAppearance ||
        !appearance.GetBackgroundImageEnabled() ||
        wallpaperTexture == 0 || wallpaperWidth <= 0 || wallpaperHeight <= 0) {
        return;
    }

    const float scale = std::max(
        canvasSize.x / static_cast<float>(wallpaperWidth),
        canvasSize.y / static_cast<float>(wallpaperHeight));
    const ImVec2 drawSize(
        static_cast<float>(wallpaperWidth) * scale,
        static_cast<float>(wallpaperHeight) * scale);
    const ImVec2 drawMin(
        (canvasSize.x - drawSize.x) * 0.5f,
        (canvasSize.y - drawSize.y) * 0.5f);
    const ImVec2 drawMax(drawMin.x + drawSize.x, drawMin.y + drawSize.y);
    const float strength = std::clamp(appearance.GetBackgroundImageStrength(), 0.0f, 1.0f);
    drawList->PushClipRect(ImVec2(0.0f, 0.0f), canvasSize, true);
    drawList->AddImage(
        (ImTextureID)(intptr_t)wallpaperTexture,
        drawMin,
        drawMax,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, strength)));
    drawList->PopClipRect();
}

std::string BuildCaptureCompletionMessage(
    bool fileSaved,
    bool clipboardDib,
    bool clipboardPng,
    const std::string& saveError,
    const std::string& clipboardError) {
    const bool clipboardAny = clipboardDib || clipboardPng;
    const bool clipboardComplete = clipboardDib && clipboardPng;
    if (fileSaved && clipboardComplete) {
        return "Graph image saved and copied to the Windows clipboard.";
    }
    if (fileSaved && clipboardAny) {
        return "Graph image saved and copied to the clipboard, but one clipboard format was unavailable" +
            (clipboardError.empty() ? std::string(".") : std::string(": ") + clipboardError);
    }
    if (fileSaved) {
        return "Graph image saved, but the clipboard copy failed" +
            (clipboardError.empty() ? std::string(".") : std::string(": ") + clipboardError);
    }
    if (clipboardAny) {
        return "Graph image copied to the clipboard, but saving failed" +
            (saveError.empty() ? std::string(".") : std::string(": ") + saveError);
    }
    std::string message = "Graph capture failed.";
    if (!saveError.empty()) message += " Save: " + saveError;
    if (!clipboardError.empty()) message += " Clipboard: " + clipboardError;
    return message;
}

} // namespace

void AppShell::ProcessGraphCaptureRequest(EditorModule* captureOwner) {
    if (!captureOwner) return;
    GraphCapture::Request request;
    if (!captureOwner->ConsumeGraphCaptureRequest(request)) {
        return;
    }
    const auto notifier = captureOwner->GetNotifier();
    const auto activity = captureOwner->GetGraphCaptureActivity();
    const auto completionLease = Stack::Notifications::RetainAsyncActivity(notifier, activity);
    if (activity && !notifier.IsOperationCurrent(activity.operationId)) {
        GraphCapture::Result cancelled;
        cancelled.message = "The graph changed before capture could begin.";
        captureOwner->CompleteGraphCapture(std::move(cancelled));
        return;
    }

    GraphCapture::Result immediateFailure;
    immediateFailure.targetPath = request.targetPath;
    std::string resolutionError;
    if (!request.viewport.IsValid() ||
        !GraphCapture::ValidateResolution(
            request.settings.width,
            request.settings.height,
            QueryRuntimeCaptureLimits(),
            &resolutionError)) {
        immediateFailure.message = resolutionError.empty()
            ? "The graph capture viewport is no longer available."
            : resolutionError;
        captureOwner->CompleteGraphCapture(std::move(immediateFailure));
        return;
    }

    const int outputWidth = request.settings.width;
    const int outputHeight = request.settings.height;
    const ImVec2 logicalSize(request.viewport.logicalWidth, request.viewport.logicalHeight);
    const ImVec2 framebufferScale(
        static_cast<float>(outputWidth) / logicalSize.x,
        static_cast<float>(outputHeight) / logicalSize.y);

    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    GLint previousPackAlignment = 4;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTextureBinding = 0;
    GLfloat previousClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glGetIntegerv(kGlActiveTexture, &previousActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

    ImGuiContext* mainContext = ImGui::GetCurrentContext();
    ImGuiContext* captureContext = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    bool captureBackendInitialized = false;
    unsigned int captureTexture = 0;
    unsigned int captureFbo = 0;
    std::unique_ptr<EditorNodeGraphUI> captureRenderer;

    auto restoreMainState = [&]() {
        if (captureContext) {
            ImGui::SetCurrentContext(captureContext);
            captureRenderer.reset();
            if (captureBackendInitialized) {
                ImGui_ImplOpenGL3_Shutdown();
                captureBackendInitialized = false;
            }
            ImGui::DestroyContext(captureContext);
            captureContext = nullptr;
        }
        ImGui::SetCurrentContext(mainContext);
        if (captureFbo != 0) {
            glDeleteFramebuffers(1, &captureFbo);
            captureFbo = 0;
        }
        if (captureTexture != 0) {
            glDeleteTextures(1, &captureTexture);
            captureTexture = 0;
        }
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        glViewport(
            previousViewport[0],
            previousViewport[1],
            previousViewport[2],
            previousViewport[3]);
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        glClearColor(
            previousClearColor[0],
            previousClearColor[1],
            previousClearColor[2],
            previousClearColor[3]);
    };

    auto failAfterContext = [&](const std::string& message) {
        restoreMainState();
        GraphCapture::Result result;
        result.targetPath = request.targetPath;
        result.message = message;
        captureOwner->CompleteGraphCapture(std::move(result));
    };

    if (!captureContext) {
        failAfterContext("Stack could not create an off-screen graph UI context.");
        return;
    }

    ImGui::SetCurrentContext(captureContext);
    StackAppearance::AppearanceManager captureAppearance = *m_Appearance;
    captureAppearance.EditWorkingTheme() = request.theme;
    ImGuiIO& captureIo = ImGui::GetIO();
    captureIo.ConfigFlags = ImGuiConfigFlags_None;
    captureIo.DisplaySize = logicalSize;
    captureIo.DisplayFramebufferScale = framebufferScale;
    captureIo.DeltaTime = 1.0f / 60.0f;
    captureAppearance.SetupFonts(captureIo);
    captureAppearance.ApplyCurrentTheme(captureIo, ImGui::GetStyle());
    if (!ImGui_ImplOpenGL3_Init("#version 430 core")) {
        failAfterContext("Stack could not initialize the off-screen graph renderer.");
        return;
    }
    captureBackendInitialized = true;
    captureRenderer = std::make_unique<EditorNodeGraphUI>();
    captureRenderer->Initialize();

    EditorNodeGraph::Graph captureGraph = captureOwner->GetNodeGraph();
    captureGraph.ClearSelection();
    if (request.settings.scope == GraphCapture::Scope::EntireGraph) {
        GraphCapture::ApplyNodeStatePreset(captureGraph, request.settings.nodeState);
    }

    auto renderCaptureFrame = [&]() {
        captureIo.DisplaySize = logicalSize;
        captureIo.DisplayFramebufferScale = framebufferScale;
        captureIo.DeltaTime = 1.0f / 60.0f;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(logicalSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::Begin(
            "##GraphCaptureSurface",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoInputs);
        DrawCaptureBackground(
            captureAppearance,
            request.settings.background,
            m_BackgroundImageTexture,
            m_BackgroundImageWidth,
            m_BackgroundImageHeight,
            logicalSize);
        captureOwner->RenderGraphCaptureCanvas(
            *captureRenderer,
            captureGraph,
            &captureAppearance,
            request,
            ImVec2(0.0f, 0.0f),
            logicalSize);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        ImGui::Render();
    };

    if (request.settings.scope == GraphCapture::Scope::EntireGraph) {
        // The first pass lets rich node surfaces report their measured logical heights.
        renderCaptureFrame();
    }
    renderCaptureFrame();

    captureTexture = GLHelpers::CreateTextureFromPixels(nullptr, outputWidth, outputHeight, 4);
    if (captureTexture == 0) {
        failAfterContext("The GPU could not allocate the requested graph capture texture.");
        return;
    }
    captureFbo = GLHelpers::CreateFBO(captureTexture);
    if (captureFbo == 0) {
        failAfterContext("The GPU could not create the graph capture framebuffer.");
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
    glViewport(0, 0, outputWidth, outputHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    std::size_t rgbaBytes = 0;
    if (!Stack::PixelBuffer::TryComputePixelByteCount(
            outputWidth, outputHeight, 4, rgbaBytes)) {
        failAfterContext("The requested graph capture dimensions exceed CPU buffer limits.");
        return;
    }
    std::vector<unsigned char> pixels;
    try {
        pixels.resize(rgbaBytes);
    } catch (const std::bad_alloc&) {
        failAfterContext("Stack could not allocate memory for the graph capture.");
        return;
    } catch (const std::length_error&) {
        failAfterContext("The requested graph capture dimensions exceed CPU buffer limits.");
        return;
    }
    const Stack::Renderer::GLState::PixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, outputWidth, outputHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const GLenum readbackError = glGetError();
    savedPackState.Restore();
    const bool transparent = request.settings.background == GraphCapture::Background::Transparent;
    if (readbackError != GL_NO_ERROR ||
        !GraphCapture::NormalizeReadbackRgba(pixels, outputWidth, outputHeight, transparent)) {
        failAfterContext("The GPU graph image readback failed.");
        return;
    }

    restoreMainState();
    EditorModule* const owner = captureOwner;
    owner->SetGraphCaptureProgress("Encoding and saving graph image...");

    const bool submitted = owner->ProjectTasks().SubmitHighPriority(
        Stack::Notifications::ForAsyncActivity(notifier, activity, "Capturing graph"), [
        this,
        owner,
        notifier, activity, completionLease,
        request = std::move(request),
        pixels = std::move(pixels),
        outputWidth,
        outputHeight]() mutable {
        std::vector<unsigned char> pngBytes;
        const bool pngEncoded = GraphCapture::EncodePng(pixels, outputWidth, outputHeight, pngBytes);

        std::vector<unsigned char> diskBytes;
        bool diskEncoded = false;
        if (request.settings.format == GraphCapture::Format::Bmp) {
            diskEncoded = GraphCapture::EncodeBmp(pixels, outputWidth, outputHeight, diskBytes);
        } else if (pngEncoded) {
            diskBytes = pngBytes;
            diskEncoded = true;
        }

        std::string saveError;
        std::error_code targetError;
        const bool targetUnchanged = request.targetApproval.path.lexically_normal() ==
            std::filesystem::u8path(request.targetPath).lexically_normal() &&
            Stack::FileSave::TargetApprovalStillMatches(request.targetApproval, targetError);
        if (!targetUnchanged) {
            saveError = targetError
                ? "The graph image destination could not be checked: " + targetError.message()
                : "The destination changed after confirmation. Choose the image path again to save.";
        }
        const bool operationCurrent = !activity || notifier.IsOperationCurrent(activity.operationId);
        if (!operationCurrent) saveError = "The graph capture was cancelled before saving.";
        const bool fileSaved = diskEncoded && targetUnchanged && operationCurrent &&
            GraphCapture::WriteEncodedFile(request.targetPath, diskBytes, &saveError);
        if (!diskEncoded && saveError.empty()) {
            saveError = request.settings.format == GraphCapture::Format::Bmp
                ? "BMP encoding failed."
                : "PNG encoding failed.";
        }

        owner->ProjectTasks().PostToMain([
            this,
            owner,
            notifier, activity, completionLease,
            request = std::move(request),
            pixels = std::move(pixels),
            pngBytes = std::move(pngBytes),
            fileSaved,
            saveError = std::move(saveError),
            outputWidth,
            outputHeight]() mutable {
            if (activity && !notifier.IsOperationCurrent(activity.operationId)) {
                GraphCapture::Result result;
                result.fileSaved = fileSaved;
                result.targetPath = request.targetPath;
                result.message = fileSaved ? "Graph image saved. Clipboard copy was cancelled." : "Graph capture cancelled.";
                owner->CompleteGraphCapture(std::move(result));
                return;
            }
            owner->SetGraphCaptureProgress("Copying graph image to the Windows clipboard...");
            void* ownerWindow = nullptr;
#if defined(_WIN32)
            ownerWindow = m_Window ? static_cast<void*>(glfwGetWin32Window(m_Window)) : nullptr;
#endif
            const ImageClipboard::PublishResult clipboard = ImageClipboard::PublishRgbaImage(
                ownerWindow,
                pixels,
                outputWidth,
                outputHeight,
                pngBytes);

            GraphCapture::Result result;
            result.clipboardRequested = true;
            result.fileSaved = fileSaved;
            result.clipboardDibV5Published = clipboard.dibV5Published;
            result.clipboardPngPublished = clipboard.pngPublished;
            result.targetPath = request.targetPath;
            result.message = BuildCaptureCompletionMessage(
                fileSaved,
                clipboard.dibV5Published,
                clipboard.pngPublished,
                saveError,
                clipboard.error);
            owner->CompleteGraphCapture(std::move(result));
        });
    });
    if (!submitted) {
        GraphCapture::Result result;
        result.message = "The graph image could not be queued for saving.";
        owner->CompleteGraphCapture(std::move(result));
    }
}
