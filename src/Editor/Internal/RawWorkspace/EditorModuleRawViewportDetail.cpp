#include "Editor/EditorModule.h"
#include "Raw/RawViewportDetail.h"
#include "Editor/Internal/RawWorkspace/RawViewportDetailDrawing.h"
#include <imgui.h>

void EditorModule::RetainRawViewportDetail(const EditorRenderWorker::Result& result) {
    const auto& next = result.outputTexture;
    const auto& nextRegion = result.rawWorkspace.viewportRegion;
    const auto content = result.rawWorkspace.presentationFingerprint;
    const auto retain = [&](const auto& texture, const auto& region, std::size_t oldContent) {
        return !result.rawWorkspace.viewportDiagnostic && Raw::RetainViewportDetail(oldContent,
            content, region, texture.width, texture.height, nextRegion, next.width, next.height);
    };
    if (!retain(m_RawViewportDetailTexture, m_RawViewportDetailRegion, m_RawViewportDetailContent)) {
        QueueViewportOutputTextureRelease(m_RawViewportDetailTexture);
        m_RawViewportDetailContent = 0;
    }
    const auto& current = m_RawWorkspacePresentationTexture;
    if (!m_RawViewportLastPresentation.diagnostic &&
        retain(current, m_RawViewportPresentedRegion, m_RawViewportPresentationContent) &&
        Raw::PreferViewportDetail(m_RawViewportPresentedRegion, current.width, current.height,
            m_RawViewportDetailRegion, m_RawViewportDetailTexture.width, m_RawViewportDetailTexture.height)) {
        QueueViewportOutputTextureRelease(m_RawViewportDetailTexture);
        m_RawViewportDetailTexture = std::move(m_RawWorkspacePresentationTexture);
        m_RawViewportDetailRegion = m_RawViewportPresentedRegion;
        m_RawViewportDetailContent = content;
    }
}

void EditorModule::DrawRawViewportDetail(ImDrawList* list, ImVec2 minimum, ImVec2 maximum) {
    if (!m_RawViewportDetailContent || m_RawViewportDetailContent != m_RawViewportPresentationContent ||
        !IsViewportTextureSafeForDrawing(m_RawViewportDetailTexture.texture)) return;
    Raw::DrawViewportDetail(*list, m_RawViewportDetailTexture.texture, m_RawViewportDetailRegion, minimum, maximum);
}
