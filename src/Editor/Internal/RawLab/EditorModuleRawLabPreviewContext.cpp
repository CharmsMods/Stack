#include "Editor/EditorModule.h"

EditorModule::RawWorkspacePreviewContext EditorModule::ResolveRawWorkspacePreviewContext(
    const Stack::RawWorkspace::SourceRecord* source) const {
    RawWorkspacePreviewContext context;
    const bool multiFrame = IsMultiFrameRawProjectActive() && m_Project->snapshot;
    context.identity = multiFrame ? GetActiveRawWorkspacePreviewIdentity()
        : source ? source->relativePathKey : std::string();
    context.projectActive = IsRawWorkspaceProjectActive() &&
        (multiFrame || (source && m_Project->rawSourceKey == context.identity));
    context.loading = !multiFrame &&
        ((m_RawWorkspacePreviewStageQueued && m_RawWorkspacePreviewStageSourceKey == context.identity) ||
         (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
          m_RawWorkspaceProjectLoadSourceKey == context.identity));
    BeginRawWorkspaceEditContext(multiFrame ? nullptr : source, context.edit);
    return context;
}
