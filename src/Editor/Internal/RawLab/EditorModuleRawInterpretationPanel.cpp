#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"

#include "Raw/Demosaic/DemosaicCatalog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui_internal.h>
#include "Library/LibraryControls.h"

namespace Stack::Editor::InterpretationUi {
const char* MethodExplanation(Raw::DemosaicMethod method) {
    using M = Raw::DemosaicMethod;
    switch (method) {
    case M::NearestNeighbor: return "Uses the closest color samples. Fast, but edges can look blocky.";
    case M::Bilinear: return "Averages nearby colors for a simple, softer result.";
    case M::MalvarHeCutler: return "Uses a small neighborhood to rebuild color and correct edge detail.";
    case M::HamiltonAdams: return "Follows horizontal or vertical edges when rebuilding missing colors.";
    case M::EdgeWeighted: return "Blends estimates along edges to keep neighboring colors separate.";
    case M::SmoothHue: return "Keeps neighboring color ratios similar for smoother color transitions.";
    case M::Lmmse: return "Combines directional estimates, giving more weight to the more reliable one.";
    case M::LmmseRefined: return "Adds color smoothing to LMMSE while keeping the recorded sensor samples.";
    case M::Dcb: return "Rebuilds color along local edges, then corrects the result in several passes.";
    case M::DcbEnhanced: return "Adds extra detail and color correction passes to DCB.";
    case M::MalvarHamilton: return "Uses MHC in smooth areas and edge-following reconstruction at stronger edges.";
    case M::HamiltonLmmse: return "Uses LMMSE in smooth areas and Hamilton-Adams where detail increases.";
    case M::DcbLmmseDetail: return "Uses LMMSE in smooth areas and refined DCB for detailed textures.";
    case M::DcbLmmseShadows: return "Uses LMMSE in shadows and refined DCB in brighter areas.";
    case M::DcbLmmseChroma: return "Takes green detail from refined DCB and color differences from LMMSE.";
    case M::DcbLmmseMalvar: return "Combines methods for dark or smooth areas, fine detail, and bright highlights.";
    case M::Ppg: return "Follows local patterns to estimate green detail and diagonal color changes.";
    case M::Vng: return "Combines color estimates from directions with similar local brightness.";
    case M::VngFourColor: return "Uses VNG while treating the sensor's two green positions separately.";
    case M::Ahd: return "Chooses the direction whose reconstructed colors agree best with their neighbors.";
    }
    return "Rebuilds full-color pixels from the sensor's color samples.";
}

bool InterpretationControls(Stack::Editor::RawWorkspaceEditContext& context, bool reconstructed, bool& active, int& request, bool sampling) {
    bool changed = false;
    active = false;
    ImGui::TextUnformatted("Demosaicing");
    ImGui::Spacing();
    const auto& methods = Raw::Demosaic::Methods;
    int selected=1;
    for(int i=0;i<static_cast<int>(methods.size());++i)
        if(methods[i].method==context.recipe.technical.demosaicMethod)selected=i;
    const int before=selected;
    ImGui::BeginDisabled(reconstructed);
    const float width=ImGui::GetContentRegionAvail().x, font=ImGui::GetFontSize();
    const float step=std::max(font*10.f,width*.46f), height=font*4.f;
    const auto origin=ImGui::GetCursorScreenPos();
    auto* storage=ImGui::GetStateStorage();
    const auto positionKey=ImGui::GetID("DemosPosition");
    const auto targetKey=ImGui::GetID("DemosTarget");
    float position=storage->GetFloat(positionKey,static_cast<float>(selected));
    float target=storage->GetFloat(targetKey,static_cast<float>(selected));
    const auto lastKey=ImGui::GetID("DemosLastSelection");
    if(storage->GetInt(lastKey,selected)!=selected)position=target=static_cast<float>(selected);
    ImGui::InvisibleButton("Demosaicing choices",{width,height},ImGuiButtonFlags_EnableNav);
    const bool enabled=context.canEdit&&!reconstructed;
    const bool held=enabled&&ImGui::IsItemActive();
    const bool hovered=enabled&&ImGui::IsItemHovered();
    auto& io=ImGui::GetIO();
    if(hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY,ImGui::GetItemID());
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelX,ImGui::GetItemID());
        target-=io.MouseWheel+io.MouseWheelH;
    }
    const bool dragging=held&&ImGui::IsMouseDragging(0);
    if(dragging) { target-=io.MouseDelta.x/step; position=target; }
    if(enabled&&ImGui::IsItemFocused()) {
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))target-=1;
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))target+=1;
    }
    if(enabled&&ImGui::IsItemDeactivated()&&ImGui::IsMouseReleased(0)&&!ImGui::IsMouseDragPastThreshold(0))
        target=std::round(position+(io.MousePos.x-origin.x-width*.5f)/step);
    target=std::clamp(target,0.f,static_cast<float>(methods.size()-1));
    selected=static_cast<int>(std::round(target));
    if(!dragging)target=static_cast<float>(selected);
    if(Stack::LibraryUi::ReducedMotion())position=target;
    else if(!dragging) {
        position+=(target-position)*std::min(1.f,io.DeltaTime*16.f);
        if(std::abs(position-target)<.001f)position=target;
    }
    position=std::clamp(position,0.f,static_cast<float>(methods.size()-1));
    storage->SetFloat(positionKey,position);storage->SetFloat(targetKey,target);
    storage->SetInt(lastKey,selected);
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(origin,{origin.x+width,origin.y+height},true);
    for(int i=std::max(0,static_cast<int>(position)-2);i<std::min(static_cast<int>(methods.size()),static_cast<int>(position)+4);++i) {
        const float center=origin.x+width*.5f+(i-position)*step;
        const auto textSize=ImGui::CalcTextSize(methods[i].label);
        const float brightness=i==selected?1.f:.48f;
        const int first=draw->VtxBuffer.Size;
        const float textLeft=center-std::min(step*.92f,textSize.x)*.5f;
        ImGui::RenderTextEllipsis(draw,{textLeft,origin.y+(height-font)*.5f},
            {center+step*.46f,origin.y+height},center+step*.46f,methods[i].label,nullptr,&textSize);
        for(int v=first;v<draw->VtxBuffer.Size;++v) {
            auto& vertex=draw->VtxBuffer[v];
            const float fade=std::clamp(std::min(vertex.pos.x-origin.x,origin.x+width-vertex.pos.x)/(font*3.f),0.f,1.f);
            const auto alpha=static_cast<unsigned int>(((vertex.col>>24)&255)*fade*brightness);
            vertex.col=(vertex.col&0x00ffffffu)|(alpha<<24);
        }
    }
    draw->PopClipRect();
    active|=held;
    if(before!=selected) {context.recipe.technical.demosaicMethod=methods[selected].method;changed=true;}
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s",reconstructed ? "Bracketing already reconstructed this image's colors." : MethodExplanation(methods[selected].method));
    ImGui::Dummy(ImVec2(0,ImGui::GetFontSize()));
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("White balance");
    auto& wb=context.recipe.whiteBalance;
    using Mode=Stack::RawRecipe::WhiteBalanceMode;
    int mode=wb.mode==Mode::AsShot?0:wb.mode==Mode::Auto?1:2;
    const char* choices[]={"Camera","Auto","Manual"};
    const float rowHeight=ImGui::GetFontSize()*2.3f;
    const float available=ImGui::GetContentRegionAvail().x;
    const float sampleWidth=rowHeight*1.4f, switchWidth=available-sampleWidth-8;

    const auto start=ImGui::GetCursorScreenPos();
    draw->AddRectFilled(start,{start.x+switchWidth,start.y+rowHeight},ImGui::GetColorU32(ImGuiCol_FrameBg,.45f),rowHeight*.5f);
    for(int i=0;i<3;++i) {
        ImGui::SetCursorScreenPos({start.x+i*switchWidth/3,start.y});
        ImGui::PushID(i);
        if(ImGui::InvisibleButton("Mode",{switchWidth/3,rowHeight},ImGuiButtonFlags_EnableNav)&&(i!=mode||i==1)) {
            if(i==1){request=1;wb.mode=Mode::Auto;changed=true;}
            else {wb.mode=i==0?Mode::AsShot:Mode::CustomMultipliers;wb.hasMultipliers=i==2;changed=true;}
        }
        const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
        if(mode==i)draw->AddRectFilled(low,high,ImGui::GetColorU32(ImGuiCol_ButtonActive,.6f),rowHeight*.5f);
        auto size=ImGui::CalcTextSize(choices[i]);
        draw->AddText({(low.x+high.x-size.x)*.5f,low.y+(rowHeight-size.y)*.5f},ImGui::GetColorU32(ImGuiCol_Text,mode==i?1.f:.55f),choices[i]);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos({start.x+switchWidth+8,start.y});
    if(ImGui::InvisibleButton("Sample neutral white balance",{sampleWidth,rowHeight},ImGuiButtonFlags_EnableNav))request=2;
    const ImVec2 mid(start.x+switchWidth+8+sampleWidth*.5f,start.y+rowHeight*.5f);
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text,sampling?1.f:.65f);
    if(sampling||ImGui::IsItemHovered())draw->AddRectFilled(ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),ImGui::GetColorU32(ImGuiCol_ButtonHovered,.6f),rowHeight*.5f);
    const float r=ImGui::GetFontSize()*.38f;
    draw->AddLine({mid.x-r,mid.y+r},{mid.x+r,mid.y-r},ink,2.f);
    draw->AddLine({mid.x,mid.y-r},{mid.x+r,mid.y},ink,2.f);
    draw->AddCircle({mid.x-r,mid.y+r},2.f,ink,12);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Sample a neutral gray or white spot in the image. Esc cancels.");
    ImGui::SetCursorScreenPos({start.x,start.y+rowHeight+ImGui::GetStyle().ItemSpacing.y});
    ImGui::Spacing();
    ImGui::BeginDisabled(mode!=2);
    const char* labels[]={"Red","Green","Blue"};
    for(int i=0;i<3;++i) {
        ImGui::PushID(i);
        const float labelX=ImGui::GetCursorPosX();
        ImGui::TextUnformatted(labels[i]);
        char value[32];std::snprintf(value,sizeof(value),"%.3f x",wb.multipliers[i]);
        ImGui::SameLine();ImGui::SetCursorPosX(labelX+available-ImGui::CalcTextSize(value).x);
        ImGui::TextUnformatted(value);
        ImGui::SetNextItemWidth(-1.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,ImVec4(0,0,0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,ImGui::GetFontSize()*.55f);
        changed|=ImGui::SliderFloat("##Gain",&wb.multipliers[i],.05f,16.f,"",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp);
        active|=mode==2&&ImGui::IsItemActive();
        const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
        const float cy=(lo.y+hi.y)*.5f, radius=ImGui::GetFontSize()*.28f;
        const float t=std::clamp(std::log(std::max(.05f,wb.multipliers[i])/.05f)/std::log(16.f/.05f),0.f,1.f);
        const float x=lo.x+radius+2+t*(hi.x-lo.x-radius*2-4);
        draw->AddLine({lo.x+2,cy},{hi.x-2,cy},ImGui::GetColorU32(ImGuiCol_Text,.22f),1.f);
        draw->AddCircleFilled({x,cy},radius,ImGui::GetColorU32(ImGuiCol_WindowBg),20);
        draw->AddCircle({x,cy},radius,ImGui::GetColorU32(ImGuiCol_Text,ImGui::IsItemActive()?1.f:.7f),20,1.3f);
        ImGui::PopStyleVar();ImGui::PopStyleColor(5);ImGui::PopID();
    }
    ImGui::EndDisabled();
    return changed;
}
}

void EditorModule::RenderRawInterpretationPanel() {
    if (!IsRawWorkspaceProjectActive() && !IsMultiFrameRawProjectActive()) {
        ImGui::TextWrapped("Open a RAW image to adjust its interpretation.");
        return;
    }

    // Source preparation is independent of the selected adjustment node.
    // Reuse its recipe/edit transaction without changing graph selection.
    const auto* source = FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
    RawWorkspaceEditContext context;
    if (!BeginRawWorkspaceEditContext(source, context, false)) {
        ImGui::TextWrapped("%s", context.error.c_str());
        return;
    }
    context.canEdit &= !Stack::Workspace::IsPreview() && !IsWorkspaceTransitionPending();
    ImGui::BeginDisabled(!context.canEdit);
    const bool reconstructed = context.multiFrameResult && m_HdrAdoptedRawResult && m_HdrAdoptedRawResult->rawData && m_HdrAdoptedRawResult->rawData->reconstructedCameraRgb;
    int request=0;
    const bool changed = Stack::Editor::InterpretationUi::InterpretationControls(context, reconstructed, m_RawWorkspaceLabUi.lightInteractionActive,request,m_WhiteBalancePickerArmed);
    ImGui::EndDisabled();
    if(request==2)m_WhiteBalancePickerArmed=!m_WhiteBalancePickerArmed;
    if(m_InterpretationWhiteBalance.Busy())ImGui::TextDisabled("Measuring white balance...");
    else if(!m_WhiteBalanceStatus.empty())ImGui::TextWrapped("%s",m_WhiteBalanceStatus.c_str());
    if (changed && !context.multiFrameResult) {
        // The single-source transaction also accepts adjustment edits. Supply
        // their current values so a source edit cannot reset those operations.
        // Assemble them only on input, never on unchanged panel frames.
        auto recipe = ReadRawControlRecipe();
        recipe.technical = context.recipe.technical;
        recipe.whiteBalance = context.recipe.whiteBalance;
        context.recipe = std::move(recipe);
    }
    CommitRawWorkspaceEditContext(context, changed,
        m_RawWorkspaceLabUi.lightInteractionActive);
    if(request==1)StartInterpretationWhiteBalance(std::nullopt);
    if (!context.error.empty()) ImGui::TextWrapped("%s", context.error.c_str());
}
