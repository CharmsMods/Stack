#pragma once
#include "Raw/Demosaic/DemosaicCatalog.h"
#include <imgui.h>

namespace Stack::Editor {
// One method catalog for source preparation and retained RAW settings views.
inline bool DrawDemosaicCombo(const char* id,Raw::DemosaicMethod& method,float width,
    bool keepOpen=false,bool* menuOpen=nullptr) {
    bool changed=false;
    ImGui::SetNextItemWidth(width);
    const bool open=ImGui::BeginCombo(id,Raw::DemosaicMethodName(method));
    if(menuOpen)*menuOpen=open;
    if(open) {
        for(const auto& choice:Raw::Demosaic::Methods) {
            const bool selected=method==choice.method;
            if(ImGui::Selectable(choice.label,selected,keepOpen ? ImGuiSelectableFlags_NoAutoClosePopups : 0)) {
                if(!selected) {method=choice.method;changed=true;}
            }
            if(selected) ImGui::SetItemDefaultFocus();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",choice.description);
        }
        ImGui::EndCombo();
    }
    return changed;
}
} // namespace Stack::Editor
