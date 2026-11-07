#include "Editor/EditorModule.h"
#include "Hdr/Presentation.h"

bool EditorModule::IsHdrOutputPreview() const {
    return !m_SdrOutputProof && Stack::Hdr::PreviewActiveForCurrentWindow();
}
