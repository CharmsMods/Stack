#include "Editor/EditorModule.h"
#include "App/AppPaths.h"

void EditorModule::RefreshRawViewportTimingHistory() {
    if (!IsRawWorkspaceProjectActive() || m_RawRenderSessionFullFrameWidth <= 0 ||
        m_RawRenderSessionFullFrameHeight <= 0 ||
        m_RawViewportCalibration.source != GetActiveRawWorkspacePreviewIdentity() ||
        m_RawViewportCalibration.sourceHash != RawViewportSourceHash()) return;
    static const std::string hardware = [] {
        std::string identity;
        for (GLenum field : {GL_VENDOR, GL_RENDERER, GL_VERSION}) {
            const auto* value = glGetString(field);
            if (value) identity += reinterpret_cast<const char*>(value);
            identity += '\n';
        }
        return identity;
    }();
    std::string representation=m_RawViewportTimingRepresentation;
    if (!IsMultiFrameRawProjectActive()) {
        const auto metadata=GetRawWorkspaceThumbnailMetadata(m_Project->rawSourceKey);
        const auto prefix=representation+":sensor:";
        if (metadata) {
            representation=prefix+std::to_string(static_cast<int>(metadata->pixelLayout))+":"+
                std::to_string(static_cast<int>(metadata->cfaPattern))+":"+
                std::to_string(static_cast<int>(metadata->linearSampleFormat))+":"+
                std::to_string(metadata->linearChannels);
        } else if (m_RawViewportCalibration.representation.rfind(prefix,0)==0) {
            // Retain classification if the gallery later evicts its thumbnail.
            representation=m_RawViewportCalibration.representation;
        } else {
            // Without verified sensor metadata, isolate that source instead
            // of mixing unrelated formats with the same dimensions. No new
            // file reads belong on the viewport's input thread.
            representation+=":unclassified:"+std::to_string(RawViewportSourceHash());
        }
    }
    m_RawViewportTimingHistory.Open(AppPaths::GetCacheDirectory() / "RawViewportTimings", hardware,
        m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight, representation);
    const auto epoch=m_RawViewportTimingHistory.Epoch();
    if (epoch!=m_RawViewportCalibration.historyEpoch || m_RawViewportCalibration.representation!=representation) {
        CancelRawViewportCalibration();
        m_RawViewportCalibration.historyEpoch=epoch;
        m_RawViewportCalibration.representation=representation;
        m_RawViewportCalibration.measuredWorkloads.clear();
        m_RawViewportCalibration.samples.clear();
        m_RawViewportCalibration.failed=false;
        m_RawViewportController.Reset();
        m_RawViewportController.SetContext(m_RawViewportAdaptiveWorkload);
    }
    if (m_RawViewportTimingHistory.MergeLoaded(m_RawViewportTimingBank)) {
        const int edge = GetCalibratedRawViewportEdge();
        if (edge > 0 && !IsRawWorkspaceUiInteractionActive())
            m_RawWorkspaceAdaptivePreviewScale = static_cast<float>(edge) /
                std::max(1, m_RawWorkspacePhysicalViewportMaxDimension);
    }
}
