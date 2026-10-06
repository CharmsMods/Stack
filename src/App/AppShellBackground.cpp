#include "AppShell.h"

#include "Async/TaskSystem.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "Utils/ImGuiExtras.h"
#include "settings/AppearanceTheme.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

unsigned char* LoadImagePixelsWithExplicitFlip(
    const std::filesystem::path& path,
    const bool flipVertically,
    int* outWidth,
    int* outHeight,
    int* outChannels) {
    stbi_set_flip_vertically_on_load_thread(flipVertically ? 1 : 0);
    unsigned char* pixels = stbi_load(
        path.string().c_str(),
        outWidth,
        outHeight,
        outChannels,
        4);
    stbi_set_flip_vertically_on_load_thread(0);
    return pixels;
}

} // namespace

void AppShell::ReleaseBackgroundImageTexture() {
    if (m_BackgroundImageTexture != 0) {
        glDeleteTextures(1, &m_BackgroundImageTexture);
        m_BackgroundImageTexture = 0;
    }
    m_BackgroundImageWidth = 0;
    m_BackgroundImageHeight = 0;
    m_BackgroundImageTextureVisibleAlpha = 0.0f;
    m_BackgroundImageTexturePath.clear();
}

void AppShell::ResetBackgroundImageDecodeState() {
    ++m_BackgroundImageDecodeGeneration;
    m_BackgroundImageDecodeState = BackgroundImageDecodeState::Idle;
    m_BackgroundImageDecodeRevision = 0;
    m_BackgroundImageDecodePath.clear();
    m_BackgroundImageDecodedPixels.clear();
    m_BackgroundImageDecodedWidth = 0;
    m_BackgroundImageDecodedHeight = 0;
    m_BackgroundImageDecodeError.clear();
}

void AppShell::ReportBackgroundImageFailure(const std::string& path,
    std::uint64_t revision, const std::string& error) {
    const auto signature = path + "\n" + std::to_string(revision) + "\n" + error;
    if (signature == m_BackgroundImageReportedFailure || !m_AppNotifier.Valid()) return;
    Stack::Notifications::NoticeSpec notice;
    notice.title = "Background image unavailable";
    notice.message = "The background image could not be displayed.";
    notice.context = "Appearance";
    notice.details = error + (path.empty() ? "" : "\n" + path);
    notice.dedupeKey = "background-image-unavailable";
    notice.severity = Stack::Notifications::Severity::Error;
    notice.outcome = Stack::Notifications::Outcome::Failure;
    m_BackgroundImageFailureEvent = m_AppNotifier.Post(std::move(notice));
    if (m_BackgroundImageFailureEvent) m_BackgroundImageReportedFailure = signature;
}

void AppShell::ResolveBackgroundImageFailure() {
    if (m_BackgroundImageFailureEvent) m_AppNotifier.Resolve(m_BackgroundImageFailureEvent);
    m_BackgroundImageFailureEvent = 0;
    m_BackgroundImageReportedFailure.clear();
}

bool AppShell::LoadBackgroundImageTextureFromPath(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    // Keep the decoded texture orientation untouched so the wallpaper draws upright in ImGui.
    unsigned char* pixels = LoadImagePixelsWithExplicitFlip(path, false, &width, &height, &channels);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    if (texture == 0) {
        return false;
    }

    ReleaseBackgroundImageTexture();
    m_BackgroundImageTexture = texture;
    m_BackgroundImageWidth = width;
    m_BackgroundImageHeight = height;
    m_BackgroundImageTextureVisibleAlpha = 0.0f;
    m_BackgroundImageTexturePath = path.lexically_normal().string();
    return true;
}

void AppShell::SyncBackgroundImageTexture() {
    if (m_CloseRequested) {
        ResetBackgroundImageDecodeState();
        return;
    }

    if (m_Appearance == nullptr) {
        ReleaseBackgroundImageTexture();
        ResetBackgroundImageDecodeState();
        m_BackgroundImageTextureRevision = 0;
        return;
    }

    const bool enabled = m_Appearance->GetBackgroundImageEnabled();
    const std::filesystem::path resolvedPath = enabled ? m_Appearance->GetResolvedBackgroundImagePath() : std::filesystem::path();
    const std::string normalizedPath = resolvedPath.empty() ? std::string() : resolvedPath.lexically_normal().string();
    const std::uint64_t revision = m_Appearance->GetBackgroundImageRevision();

    if (!enabled || normalizedPath.empty()) {
        if (m_BackgroundImageTextureVisibleAlpha <= 0.001f) {
            ReleaseBackgroundImageTexture();
            ResetBackgroundImageDecodeState();
            m_BackgroundImageTextureRevision = revision;
            m_Appearance->SetBackgroundImageRuntimeStatus("");
            ResolveBackgroundImageFailure();
        }
        return;
    }

    if (m_BackgroundImageTexture != 0 &&
        m_BackgroundImageTexturePath == normalizedPath &&
        m_BackgroundImageTextureRevision == revision) {
        return;
    }

    if (m_BackgroundImageDecodeState == BackgroundImageDecodeState::Ready &&
        m_BackgroundImageDecodePath == normalizedPath &&
        m_BackgroundImageDecodeRevision == revision) {
        if (m_BackgroundImageDecodedPixels.empty() ||
            m_BackgroundImageDecodedWidth <= 0 ||
            m_BackgroundImageDecodedHeight <= 0) {
            ReleaseBackgroundImageTexture();
            m_BackgroundImageTextureRevision = revision;
            m_Appearance->SetBackgroundImageRuntimeStatus("Failed to decode or upload the background image.");
            m_BackgroundImageDecodeState = BackgroundImageDecodeState::Failed;
            m_BackgroundImageDecodeError = "Failed to decode or upload the background image.";
            m_BackgroundImageDecodedPixels.clear();
            ReportBackgroundImageFailure(normalizedPath, revision, m_BackgroundImageDecodeError);
            return;
        }

        const unsigned int texture = GLHelpers::CreateTextureFromPixels(
            m_BackgroundImageDecodedPixels.data(),
            m_BackgroundImageDecodedWidth,
            m_BackgroundImageDecodedHeight,
            4);
        if (texture == 0) {
            ReleaseBackgroundImageTexture();
            m_BackgroundImageTextureRevision = revision;
            m_Appearance->SetBackgroundImageRuntimeStatus("Failed to upload the background image.");
            m_BackgroundImageDecodeState = BackgroundImageDecodeState::Failed;
            m_BackgroundImageDecodeError = "Failed to upload the background image.";
            m_BackgroundImageDecodedPixels.clear();
            ReportBackgroundImageFailure(normalizedPath, revision, m_BackgroundImageDecodeError);
            return;
        }

        ReleaseBackgroundImageTexture();
        m_BackgroundImageTexture = texture;
        m_BackgroundImageWidth = m_BackgroundImageDecodedWidth;
        m_BackgroundImageHeight = m_BackgroundImageDecodedHeight;
        m_BackgroundImageTextureVisibleAlpha = 0.0f;
        m_BackgroundImageTexturePath = normalizedPath;
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus("");
        ResolveBackgroundImageFailure();
        ResetBackgroundImageDecodeState();
        return;
    }

    if (m_BackgroundImageDecodeState == BackgroundImageDecodeState::Failed &&
        m_BackgroundImageDecodePath == normalizedPath &&
        m_BackgroundImageDecodeRevision == revision) {
        ReleaseBackgroundImageTexture();
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus(
            m_BackgroundImageDecodeError.empty()
                ? "Failed to decode the background image."
                : m_BackgroundImageDecodeError);
        ReportBackgroundImageFailure(normalizedPath, revision,
            m_BackgroundImageDecodeError.empty() ? "Failed to decode the background image." : m_BackgroundImageDecodeError);
        return;
    }

    if ((m_BackgroundImageDecodeState == BackgroundImageDecodeState::Queued ||
         m_BackgroundImageDecodeState == BackgroundImageDecodeState::Decoding) &&
        m_BackgroundImageDecodePath == normalizedPath &&
        m_BackgroundImageDecodeRevision == revision) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec) || ec) {
        ReleaseBackgroundImageTexture();
        ResetBackgroundImageDecodeState();
        m_BackgroundImageTextureRevision = revision;
        m_Appearance->SetBackgroundImageRuntimeStatus("Managed background image file is missing.");
        ReportBackgroundImageFailure(normalizedPath, revision, "Managed background image file is missing.");
        return;
    }

    ++m_BackgroundImageDecodeGeneration;
    const std::uint64_t generation = m_BackgroundImageDecodeGeneration;
    m_BackgroundImageDecodeState = BackgroundImageDecodeState::Queued;
    m_BackgroundImageDecodeRevision = revision;
    m_BackgroundImageDecodePath = normalizedPath;
    m_BackgroundImageDecodedPixels.clear();
    m_BackgroundImageDecodedWidth = 0;
    m_BackgroundImageDecodedHeight = 0;
    m_BackgroundImageDecodeError.clear();
    m_Appearance->SetBackgroundImageRuntimeStatus("Loading background image...");

    const auto scope = m_AppNotifier;
    Async::ActivityMetadata metadata;
    metadata.ownerId = scope.GetOwner().id;
    metadata.ownerGeneration = scope.GetOwner().generation;
    metadata.label = "Loading background";
    metadata.maintenance = true;
    const bool submitted = Async::TaskSystem::Get().Submit(std::move(metadata),
        [this, scope, generation, resolvedPath, normalizedPath, revision]() {
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> decodedPixels;
        std::string errorMessage;

        try {
            const std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                LoadImagePixelsWithExplicitFlip(resolvedPath, false, &width, &height, &channels), stbi_image_free);
            if (pixels && width > 0 && height > 0) {
                decodedPixels.assign(pixels.get(), pixels.get() +
                    (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u));
            } else {
                errorMessage = "Failed to decode the background image.";
            }
        } catch (const std::exception& exception) {
            decodedPixels.clear();
            errorMessage = exception.what();
        } catch (...) {
            decodedPixels.clear();
            errorMessage = "Background image decoding failed unexpectedly.";
        }

        Async::TaskSystem::Get().PostToMain([
            this,
            scope,
            generation,
            normalizedPath,
            revision,
            decodedPixels = std::move(decodedPixels),
            width,
            height,
            errorMessage = std::move(errorMessage)]() mutable {
            if (m_CloseRequested || !scope.Valid() || generation != m_BackgroundImageDecodeGeneration ||
                normalizedPath != m_BackgroundImageDecodePath ||
                revision != m_BackgroundImageDecodeRevision) {
                return;
            }

            if (decodedPixels.empty() || width <= 0 || height <= 0) {
                m_BackgroundImageDecodeState = BackgroundImageDecodeState::Failed;
                m_BackgroundImageDecodeError = errorMessage.empty()
                    ? "Failed to decode the background image."
                    : std::move(errorMessage);
                return;
            }

            m_BackgroundImageDecodedPixels = std::move(decodedPixels);
            m_BackgroundImageDecodedWidth = width;
            m_BackgroundImageDecodedHeight = height;
            m_BackgroundImageDecodeError.clear();
            m_BackgroundImageDecodeState = BackgroundImageDecodeState::Ready;
        });
    });

    if (submitted) {
        m_BackgroundImageDecodeState = BackgroundImageDecodeState::Decoding;
    } else {
        m_BackgroundImageDecodeState = BackgroundImageDecodeState::Failed;
        m_BackgroundImageDecodeError = "Background image decoding could not start.";
        m_Appearance->SetBackgroundImageRuntimeStatus(m_BackgroundImageDecodeError);
        ReportBackgroundImageFailure(normalizedPath, revision, m_BackgroundImageDecodeError);
    }
}

void AppShell::RenderBackgroundImage(
    const ImVec2& regionMin,
    const ImVec2& regionSize,
    float alphaMultiplier,
    ImDrawList* targetDrawList) {
    if (m_Appearance == nullptr ||
        m_BackgroundImageTexture == 0 ||
        m_BackgroundImageWidth <= 0 ||
        m_BackgroundImageHeight <= 0 ||
        regionSize.x <= 0.0f ||
        regionSize.y <= 0.0f) {
        return;
    }

    const bool enabled = m_Appearance->GetBackgroundImageEnabled();
    const std::filesystem::path resolvedPath = enabled ? m_Appearance->GetResolvedBackgroundImagePath() : std::filesystem::path();
    const std::string normalizedPath = resolvedPath.empty() ? std::string() : resolvedPath.lexically_normal().string();
    const bool shouldBeVisible = enabled && !normalizedPath.empty() && m_BackgroundImageTexturePath == normalizedPath;
    const float targetAlpha = shouldBeVisible ? 1.0f : 0.0f;

    m_BackgroundImageTextureVisibleAlpha = ImGuiExtras::AnimateTowards(
        m_BackgroundImageTextureVisibleAlpha,
        targetAlpha,
        ImGui::GetIO().DeltaTime,
        6.5f);

    if (m_BackgroundImageTextureVisibleAlpha <= 0.001f) {
        return;
    }

    const float scale = std::max(
        regionSize.x / static_cast<float>(m_BackgroundImageWidth),
        regionSize.y / static_cast<float>(m_BackgroundImageHeight));
    const ImVec2 drawSize(
        static_cast<float>(m_BackgroundImageWidth) * scale,
        static_cast<float>(m_BackgroundImageHeight) * scale);
    const ImVec2 drawMin(
        regionMin.x + (regionSize.x - drawSize.x) * 0.5f,
        regionMin.y + (regionSize.y - drawSize.y) * 0.5f);
    const ImVec2 drawMax(drawMin.x + drawSize.x, drawMin.y + drawSize.y);
    const float strength = std::clamp(
        m_Appearance->GetBackgroundImageStrength() *
            std::clamp(alphaMultiplier, 0.0f, 1.0f) *
            std::clamp(m_BackgroundImageTextureVisibleAlpha, 0.0f, 1.0f),
        0.0f,
        1.0f);
    if (strength <= 0.001f) {
        return;
    }
    const ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, strength));

    ImDrawList* drawList = targetDrawList ? targetDrawList : ImGui::GetWindowDrawList();
    drawList->PushClipRect(regionMin, ImVec2(regionMin.x + regionSize.x, regionMin.y + regionSize.y), true);
    drawList->AddImage(
        (ImTextureID)(intptr_t)m_BackgroundImageTexture,
        drawMin,
        drawMax,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        tint);
    drawList->PopClipRect();
}
