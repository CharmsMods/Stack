#include "Editor/EditorModule.h"

bool EditorModule::ApplyActiveRawWorkspaceModeDataToDocument(StackBinaryFormat::ProjectDocument& document) const {
    if (!document.rawWorkspaceData.is_object()) return false;
    document.rawWorkspaceData["rawWorkspaceMode"] = "unified-layer-graphs";
    document.rawWorkspaceData.erase("managedRawSection");
    document.rawWorkspaceData.erase("customRawSection");
    document.rawWorkspaceData.erase("readOnlyReason");
    return true;
}
