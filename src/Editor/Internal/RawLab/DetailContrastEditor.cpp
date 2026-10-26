#include "Editor/Internal/RawLab/DetailContrastEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Editor::RawLabInternal {
bool DrawDetailContrastEditor(RawRecipe::DetailContrast& settings, DetailContrastEditorState& ui, const std::function<bool(const std::string&)>& driven,
    RawLabControlSection section) {
    using namespace RawRecipe;
    const auto slider = [&](const char* id, const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags = 0) {
        const bool connected = driven && driven(id);
        ImGui::BeginDisabled(connected);
        if (section == RawLabControlSection::Settings) {
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1);
        }
        const std::string widget = section == RawLabControlSection::Settings ? std::string("##") + id : label;
        const bool edited = ImGui::SliderFloat(widget.c_str(), value, minimum, maximum, format, flags);
        ImGui::EndDisabled();
        if (connected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Driven by a graph input. This is the stored fallback value.");
        return edited;
    };
    bool changed = false;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .55f);
    if (ShowsRawLabSettings(section)) {
        changed = ImGui::Checkbox("Enabled##DetailContrast", &settings.enabled);
        const auto macro = [&](const char* label, int first, int last) {
            float mean = 0;
            for (int i = first; i <= last; ++i) mean += settings.scaleGains[i];
            mean /= last - first + 1;
            float edited = mean;
            bool connected = false;
            for (int i = first; i <= last; ++i) connected |= driven && driven("band-" + std::to_string(i));
            ImGui::BeginDisabled(connected);
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1);
            const std::string widget = std::string("##DetailMacro") + label;
            if (ImGui::SliderFloat(widget.c_str(), &edited, 0, 3, "%.2fx")) {
                AdjustDetailMacro(settings, first, last, edited - mean);
                changed = true;
            }
            ImGui::EndDisabled();
        };
        macro("Texture", 0, 2);
        macro("Clarity", 3, 5);
        macro("Structure", 6, 7);
        LabTooltip("These controls edit groups of bands in the scale graph. A gain of 1 preserves the original detail.");
        if (ImGui::RadioButton("Scale", !ui.fieldView)) ui.fieldView = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("EV x scale", ui.fieldView)) ui.fieldView = true;
        if (ui.fieldView) {
            ImGui::TextUnformatted("Paint gain");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##DetailPaintGain", &ui.brushGain, 0, 3, "%.2fx");
            LabTooltip("Drag to paint the response. Right-click restores the selected cells to their scale-graph value.");
        }
    }
    if (ShowsRawLabGraph(section)) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = std::max(160.f, ImGui::GetContentRegionAvail().x), height = 220.f;
        ImGui::InvisibleButton("##DetailResponse", {width, height}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const ImRect box({start.x + 29, start.y + 8}, {start.x + width - 5, start.y + height - 28});
        auto* draw = ImGui::GetWindowDrawList();
        const auto text = ImGui::GetColorU32(ImGuiCol_Text), muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const auto x = [&](int band) { return box.Min.x + box.GetWidth() * (band + .5f) / kDetailBands; };
        const auto y = [&](float gain) { return box.Max.y - box.GetHeight() * gain / 3; };
        if (ui.fieldView) {
            for (int row = 0; row < kDetailEvSamples; ++row) for (int band = 0; band < kDetailBands; ++band) {
                const float gain = std::clamp(settings.scaleGains[band] + settings.evScaleResidual[row*kDetailBands+band], 0.f, 3.f);
                const float ev = kDetailMinEv + (kDetailMaxEv-kDetailMinEv)*row/(kDetailEvSamples-1);
                const float effective = EvaluateDetailGain(settings, band, ev);
                ImVec4 color = effective >= 1 ? ImVec4(.2f, .46f, .8f, .15f + .4f*(effective-1))
                    : ImVec4(.85f, .48f, .2f, .15f + .8f*(1-effective));
                const ImVec2 a(box.Min.x + band*box.GetWidth()/kDetailBands,
                    box.Max.y - (row+1)*box.GetHeight()/kDetailEvSamples);
                const ImVec2 b(a.x + box.GetWidth()/kDetailBands, a.y + box.GetHeight()/kDetailEvSamples);
                draw->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(color));
                if (band == ui.selectedBand && row == ui.selectedRow) draw->AddRect(a,b,text);
                (void)gain;
            }
            draw->AddText({start.x, box.Min.y}, muted, "+12");
            draw->AddText({start.x, box.Max.y-12}, muted, "-12");
        } else {
            for (int gain = 0; gain <= 3; ++gain) {
                char label[8]; std::snprintf(label,sizeof(label),"%d",gain);
                draw->AddText({start.x+7, y(float(gain))-7}, muted, label);
                draw->AddLine({box.Min.x,y(float(gain))},{box.Max.x,y(float(gain))},ImGui::GetColorU32(ImGuiCol_Text,gain==1?.4f:.12f));
            }
            for (int band = 0; band < kDetailBands; ++band) {
                if (band) draw->AddLine({x(band-1),y(settings.scaleGains[band-1])},{x(band),y(settings.scaleGains[band])},text,1.5f);
                draw->AddCircleFilled({x(band),y(settings.scaleGains[band])},band==ui.selectedBand?4:3,text);
            }
        }
        draw->AddRect(box.Min, box.Max, ImGui::GetColorU32(ImGuiCol_Border));
        draw->AddText({box.Min.x,box.Max.y+7},muted,"Fine");
        draw->AddText({box.Max.x-46,box.Max.y+7},muted,"Coarse");
        if (hovered || ImGui::IsItemActive()) {
            const auto mouse = ImGui::GetIO().MousePos;
            const int band = std::clamp(int((mouse.x-box.Min.x)/box.GetWidth()*kDetailBands),0,kDetailBands-1);
            const int row = std::clamp(int((box.Max.y-mouse.y)/box.GetHeight()*kDetailEvSamples),0,kDetailEvSamples-1);
            if (ImGui::IsItemActive() && (ui.fieldView || !driven || !driven("band-" + std::to_string(band))) && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1))) {
                ui.selectedBand=band; ui.selectedRow=row;
                if (ui.fieldView) settings.evScaleResidual[row*kDetailBands+band] = ImGui::IsMouseDown(1) ? 0 : ui.brushGain-settings.scaleGains[band];
                else settings.scaleGains[band] = ImGui::IsMouseDown(1) ? 1 : std::clamp((box.Max.y-mouse.y)/box.GetHeight()*3,0.f,3.f);
                changed=true;
            }
            if (hovered) ImGui::SetTooltip("Band %d: %.1f source px%s",band+1,DetailBandScale(settings,band),ui.fieldView?"; brighter tones above":"");
        }
        if (section == RawLabControlSection::Graph)
            ImGui::TextDisabled("Selected scale: %.1f source px", DetailBandScale(settings,ui.selectedBand));
    }
    if (ShowsRawLabSettings(section)) {
        ImGui::TextDisabled("Selected scale: %.1f source px", DetailBandScale(settings,ui.selectedBand));
        if (ui.fieldView) {
            const int index=ui.selectedRow*kDetailBands+ui.selectedBand;
            float gain=std::clamp(settings.scaleGains[ui.selectedBand]+settings.evScaleResidual[index],0.f,3.f);
            ImGui::TextDisabled("Input: %.1f EV",kDetailMinEv+(kDetailMaxEv-kDetailMinEv)*ui.selectedRow/(kDetailEvSamples-1));
            if (slider("cell-gain", "Cell gain", &gain, 0, 3, "%.2fx")) { settings.evScaleResidual[index]=gain-settings.scaleGains[ui.selectedBand];changed=true; }
            if (ImGui::Button("Clear EV variation")) { settings.evScaleResidual.fill(0);changed=true; }
        } else changed|=slider(("band-" + std::to_string(ui.selectedBand)).c_str(), "Band gain",&settings.scaleGains[ui.selectedBand],0,3,"%.2fx");
        changed|=ImGui::Checkbox("Target an EV range",&settings.targetEnabled);
        if(settings.targetEnabled) {
            changed|=slider("target", "Center",&settings.targetEv,-12,12,"%.2f EV");
            changed|=slider("target-width", "Half-width",&settings.targetHalfWidthEv,.1f,12,"%.2f EV");
            changed|=slider("target-feather", "Feather",&settings.targetFeatherEv,.1f,8,"%.2f EV");
        }
        changed|=slider("edge-protection", "Edge protection",&settings.edgeProtection,0,1,"%.2f");
        changed|=slider("scale", "Largest scale",&settings.maximumScale,16,1024,"%.0f source px",ImGuiSliderFlags_Logarithmic);
        LabTooltip("Scale is measured in the original image, before cropping. Preview uses the bands its resolution can represent.");
    }
    ImGui::PopItemWidth();
    if(changed) settings=SanitizeDetailContrast(settings);
    return changed;
}
}
