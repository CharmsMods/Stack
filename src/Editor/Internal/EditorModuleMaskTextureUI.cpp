#include "Editor/EditorModule.h"

#include <algorithm>

// Captured coverage is a texture source. Editing its pixels uses ordinary graph
// operations; there is no second painting document or private mask undo stack.
// The old CustomMask payload remains readable for existing saved projects.
void EditorModule::RenderMaskTextureControls(EditorNodeGraph::Node& node, float controlWidth) {
    int width=0,height=0;
    const auto texture=GetRawMaskTexturePreview(node.id,width,height);
    if (texture && width>0 && height>0) {
        const float scale=std::min(std::max(1.f,controlWidth)/width,260.f/height);
        ImGui::Image((ImTextureID)(intptr_t)texture,{width*scale,height*scale},{0,1},{1,0});
    }
    ImGui::TextDisabled("%d x %d coverage texture",node.customMask.width,node.customMask.height);
    ImGui::TextWrapped("Connect math nodes after this texture to change its coverage.");
}
