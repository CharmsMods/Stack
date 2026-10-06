#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace Stack::Editor::RawLabInternal {

bool RenderRawLabColorSettings(RawLabColorSurfaceArgs& args) {
    using RawRecipe::RawColorWarpPin;
    constexpr float pi = 3.14159265358979323846f;
    args.colorWarp = RawRecipe::SanitizeColorWarpRecipe(std::move(args.colorWarp));
    auto& colorWarp = args.colorWarp;
    bool changed = false;
    if (BypassEyeButton(
            "##RawLabColorWarpBypass",
            colorWarp.enabled,
            "Color Warp is included in every output. Click to bypass it without losing any settings.",
            "Color Warp is bypassed in every output. Click to include the preserved edit again.")) {
        colorWarp.enabled = !colorWarp.enabled;
        changed = true;
    }
    if (BareTextButton(
            "Static",
            !args.ui.colorWarpLiveCloud)) {
        if (args.ui.colorWarpLiveCloud) {
            args.ui.colorWarpLiveCloud = false;
            args.saveAppState();
        }
    }
    LabTooltip(
        "Keep every dot at its original pre-Color OKLab coordinate while edits change the photograph.");
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton(
            "Live",
            args.ui.colorWarpLiveCloud)) {
        if (!args.ui.colorWarpLiveCloud) {
            args.ui.colorWarpLiveCloud = true;
            args.saveAppState();
        }
    }
    LabTooltip(
        "Continuously reproject the same retained dots through the current Color Warp edit.");
    if (colorWarp.pins.empty() || args.ui.colorWarpProvisionalPinValid) {
        ImGui::TextWrapped("Select a retained pin on the photograph or Color Warp graph to edit its settings.");
        return changed;
    }
    args.ui.selectedColorWarpPin = std::clamp(args.ui.selectedColorWarpPin, 0, int(colorWarp.pins.size()) - 1);
    ImGui::TextUnformatted("Pin");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##ColorWarpPin", colorWarp.pins[args.ui.selectedColorWarpPin].name.c_str())) {
        for (int i = 0; i < int(colorWarp.pins.size()); ++i) {
            ImGui::PushID(colorWarp.pins[i].id.c_str());
            if (ImGui::Selectable(colorWarp.pins[i].name.c_str(), args.ui.selectedColorWarpPin == i)) {
                args.ui.selectedColorWarpPin = i;
                args.ui.colorWarpEvGesture = 0;
                args.ui.colorWarpSmartSelection = {};
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    auto& selected = colorWarp.pins[args.ui.selectedColorWarpPin];
    if (BareTextButton("Draw", args.ui.colorWarpEvEditMode == 0)) {
        args.ui.colorWarpEvEditMode = 0;
        args.ui.colorWarpEvGesture = 0;
        args.ui.colorWarpSmartSelection = {};
        args.ui.colorWarpSmartHoverSelection = {};
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Smart", args.ui.colorWarpEvEditMode == 1)) {
        args.ui.colorWarpEvEditMode = 1;
        args.ui.colorWarpEvGesture = 0;
    }
    ImGui::TextDisabled(
        args.ui.colorWarpEvEditMode == 0
            ? "Brush radius %.1f EV"
            : (args.ui.colorWarpSmartSelection.active &&
                       args.ui.colorWarpSmartSelection.pinId == selected.id
                   ? "Selected %.1f to %.1f EV"
                   : "Click a shape"),
        args.ui.colorWarpEvEditMode == 0
            ? args.ui.colorWarpEvBrushWidth
            : args.ui.colorWarpSmartSelection.leftEv,
        args.ui.colorWarpSmartSelection.rightEv);
    ImGui::BeginDisabled(selected.protectColor);
    float lightnessOutput = selected.lightnessDeltaEv;
    if (BareSliderFloat(
            "Lightness output",
            "RawLabColorWarpLightnessOutput",
            &lightnessOutput,
            -4.0f,
            4.0f,
            "%+.2f EV")) {
        selected.lightnessDeltaEv = lightnessOutput;
        changed = true;
    }
    ImGui::EndDisabled();
    LabTooltip(
        "Changes output lightness after qualification. It does not reshape the EV curve.");
    auto selectedRegion = std::find_if(
        colorWarp.regions.begin(), colorWarp.regions.end(),
        [&](const auto& region) { return region.id == selected.regionId; });
    if (selectedRegion != colorWarp.regions.end()) {
        ImGui::TextDisabled("Spatial relation");
        const struct {
            const char* label;
            Stack::RawRecipe::RawColorWarpSpatialMode mode;
            const char* description;
        } spatialModes[] = {
            { "All", Stack::RawRecipe::RawColorWarpSpatialMode::AllMatches,
              "Affects every pixel matching this pin's color and EV qualifier." },
            { "Connected", Stack::RawRecipe::RawColorWarpSpatialMode::Connected,
              "Keeps only qualifying pixels connected to the sampled circle." },
            { "Cohesive", Stack::RawRecipe::RawColorWarpSpatialMode::Cohesive,
              "Keeps the sampled connected section, then closes small holes and removes isolated specks." },
            { "Reach", Stack::RawRecipe::RawColorWarpSpatialMode::EdgeAwareReach,
              "Extends from the sampled section through weak matches while stopping at strong image edges." },
            { "Assisted", Stack::RawRecipe::RawColorWarpSpatialMode::AssistedRegion,
              "Combines include circles, short gap bridging, and exclusion circles into one region." }
        };
        const char* selectedSpatialDescription = spatialModes[0].description;
        for (std::size_t modeIndex = 0; modeIndex < std::size(spatialModes); ++modeIndex) {
            if (modeIndex > 0u) ContinueRawLabControlRow(spatialModes[modeIndex].label);
            if (BareTextButton(
                    spatialModes[modeIndex].label,
                    selectedRegion->spatialMode == spatialModes[modeIndex].mode)) {
                selectedRegion->spatialMode = spatialModes[modeIndex].mode;
                changed = true;
            }
            LabTooltip(spatialModes[modeIndex].description);
            if (selectedRegion->spatialMode == spatialModes[modeIndex].mode) {
                selectedSpatialDescription = spatialModes[modeIndex].description;
            }
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", selectedSpatialDescription);
        ImGui::PopTextWrapPos();
        if (selectedRegion->spatialMode ==
                Stack::RawRecipe::RawColorWarpSpatialMode::EdgeAwareReach ||
            selectedRegion->spatialMode ==
                Stack::RawRecipe::RawColorWarpSpatialMode::AssistedRegion) {
            changed |= BareSliderFloat(
                "Reach", "RawLabColorWarpSpatialReach",
                &selectedRegion->reachPixels, 0.0f, 256.0f, "%.0f px");
            LabTooltip(
                "Maximum image-space distance that Reach or Assisted may grow "
                "outward from directly qualified pixels. The amber dashed "
                "viewport guide represents this distance.");
            changed |= BareSliderFloat(
                "Support", "RawLabColorWarpSpatialSupport",
                &selectedRegion->spatialSupport, 0.0f, 1.0f, "%.2f");
            LabTooltip(
                "Strength contributed by pixels admitted through spatial "
                "support rather than direct color-and-EV qualification. Zero "
                "disables the added spatial contribution; one uses it fully.");
        }
        changed |= BareSliderFloat(
            "Edge stop", "RawLabColorWarpEdgeStop",
            &selectedRegion->edgeStop, 0.0f, 1.0f, "%.2f");
        LabTooltip(
            "How strongly source-image color and luminance edges resist spatial "
            "growth. Higher values stop sooner at boundaries; lower values "
            "permit reach across larger changes.");
        changed |= BareSliderFloat(
            "Boundary", "RawLabColorWarpBoundaryFeather",
            &selectedRegion->featherPixels, 0.0f, 256.0f, "%.0f px");
        LabTooltip(
            "Width of the final image-space fade after spatial membership is "
            "resolved. This is separate from color-distance softness and EV "
            "feathering. The violet viewport guide represents this width.");
        const struct {
            const char* label;
            Stack::RawRecipe::RawColorWarpFeatherDirection direction;
            const char* description;
        } directions[] = {
            { "In", Stack::RawRecipe::RawColorWarpFeatherDirection::Inward,
              "Fade inward from the accepted region boundary, reducing effect near its interior edge." },
            { "Centered", Stack::RawRecipe::RawColorWarpFeatherDirection::Centered,
              "Split the boundary fade across both sides of the accepted region edge." },
            { "Out", Stack::RawRecipe::RawColorWarpFeatherDirection::Outward,
              "Preserve the accepted interior and fade outward beyond its boundary." }
        };
        for (std::size_t directionIndex = 0; directionIndex < std::size(directions); ++directionIndex) {
            if (directionIndex > 0u) ContinueRawLabControlRow(directions[directionIndex].label);
            if (BareTextButton(
                    directions[directionIndex].label,
                    selectedRegion->featherDirection == directions[directionIndex].direction)) {
                selectedRegion->featherDirection = directions[directionIndex].direction;
                changed = true;
            }
            LabTooltip(directions[directionIndex].description);
        }
    }
    const auto selectedGroup = std::find_if(
        colorWarp.linkGroups.begin(), colorWarp.linkGroups.end(),
        [&](const auto& group) {
            return group.id == args.ui.selectedColorWarpGroupId;
        });
    if (selectedGroup != colorWarp.linkGroups.end()) {
        ImGui::TextDisabled("Linked targets · %zu colors", selectedGroup->pinIds.size());
        const char* operationLabels[] = {
            "Move", "Rotate", "Spread", "Pull", "Chroma"
        };
        const char* operationDescriptions[] = {
            "Translate every linked destination by the same a/b offset.",
            "Rotate linked destinations together around their target centroid while preserving separation.",
            "Spread or compress linked destinations around their target centroid.",
            "Move linked destinations toward the chosen group target.",
            "Scale every linked destination's chroma around neutral while preserving its hue."
        };
        for (int operation = 0; operation < 5; ++operation) {
            if (operation > 0) ContinueRawLabControlRow(operationLabels[operation]);
            if (BareTextButton(
                    operationLabels[operation],
                    args.ui.colorWarpGroupOperation == operation)) {
                args.ui.colorWarpGroupOperation = operation;
            }
            LabTooltip(operationDescriptions[operation]);
        }
        const auto applyHarmony = [&](int harmony) {
            std::vector<RawColorWarpPin*> members;
            for (auto& pin : colorWarp.pins) {
                if (std::find(selectedGroup->pinIds.begin(), selectedGroup->pinIds.end(), pin.id) !=
                    selectedGroup->pinIds.end()) members.push_back(&pin);
            }
            if (members.empty()) return;
            const float baseHue = std::atan2(members.front()->targetB, members.front()->targetA);
            for (std::size_t index = 0; index < members.size(); ++index) {
                auto& pin = *members[index];
                const float chroma = std::hypot(pin.targetA, pin.targetB);
                float offset = 0.0f;
                if (harmony == 0) {
                    offset = (static_cast<float>(index) -
                        static_cast<float>(members.size() - 1u) * 0.5f) *
                        (3.14159265358979323846f / 6.0f);
                } else if (harmony == 1) {
                    offset = (index & 1u) == 0u ? 0.0f : 3.14159265358979323846f;
                } else {
                    offset = static_cast<float>(index % 3u) *
                        (2.0f * 3.14159265358979323846f / 3.0f);
                }
                const ImVec2 templateTarget(
                    std::cos(baseHue + offset) * chroma,
                    std::sin(baseHue + offset) * chroma);
                pin.targetA += (templateTarget.x - pin.targetA) * 0.35f;
                pin.targetB += (templateTarget.y - pin.targetB) * 0.35f;
            }
        };
        if (BareTextButton("Analogous")) {
            applyHarmony(0);
            changed = true;
        }
        LabTooltip(
            "Move linked destinations 35% toward an analogous hue template "
            "centered on the first group target.");
        ContinueRawLabControlRow("Complement");
        if (BareTextButton("Complement")) {
            applyHarmony(1);
            changed = true;
        }
        LabTooltip(
            "Move linked destinations 35% toward alternating complementary "
            "hues while retaining each destination's chroma.");
        ContinueRawLabControlRow("Triad");
        if (BareTextButton("Triad")) {
            applyHarmony(2);
            changed = true;
        }
        LabTooltip(
            "Move linked destinations 35% toward a three-way, 120-degree hue "
            "template while retaining each destination's chroma.");
    }
    if (BareTextButton("Enabled", selected.enabled)) {
        selected.enabled = !selected.enabled;
        changed = true;
    }
    LabTooltip(
        "Enable or bypass only the selected pin. Its stored source, target, "
        "EV curve, and sampled region remain unchanged while bypassed.");
    ContinueRawLabControlRow("Protect");
    if (BareTextButton("Protect", selected.protectColor)) {
        selected.protectColor = !selected.protectColor;
        if (selected.protectColor) {
            selected.targetA = selected.sourceA;
            selected.targetB = selected.sourceB;
            selected.lightnessDeltaEv = 0.0f;
        }
        changed = true;
    }
    LabTooltip(
        "Protect the selected color instead of warping it. Protection returns "
        "the destination to the source and resets its lightness shift.");
    ContinueRawLabControlRow("Duplicate");
    if (BareTextButton(
            "Duplicate",
            false,
            colorWarp.pins.size() < Stack::RawRecipe::kMaxRawColorWarpPins)) {
        RawColorWarpPin duplicate = selected;
        duplicate.id = "color-" + std::to_string(colorWarp.pins.size() + 1u);
        std::unordered_set<std::string> ids;
        for (const RawColorWarpPin& existing : colorWarp.pins) ids.insert(existing.id);
        while (ids.find(duplicate.id) != ids.end()) duplicate.id += "-copy";
        duplicate.name = "Color " + std::to_string(colorWarp.pins.size() + 1u);
        colorWarp.pins.push_back(std::move(duplicate));
        args.ui.selectedColorWarpPin =
            static_cast<int>(colorWarp.pins.size() - 1u);
        return true;
    }
    LabTooltip(
        "Create another pin with the selected pin's qualifier, target, EV "
        "curve, strength, and region reference. The eight-pin limit applies.",
        ImGuiHoveredFlags_AllowWhenDisabled);
    ContinueRawLabControlRow("Delete");
    if (BareTextButton("Delete")) {
        colorWarp.pins.erase(
            colorWarp.pins.begin() + args.ui.selectedColorWarpPin);
        args.ui.selectedColorWarpPin = colorWarp.pins.empty()
            ? -1
            : std::min(
                  args.ui.selectedColorWarpPin,
                  static_cast<int>(colorWarp.pins.size() - 1u));
        return true;
    }
    LabTooltip(
        "Permanently remove the selected pin. Other pins and independently "
        "referenced sampled regions remain available.");

    float directionality = selected.qualifierDirectionality;
    if (BareSliderFloat(
            "Shape",
            "RawLabColorWarpQualifierDirectionality",
            &directionality,
            0.0f,
            1.0f,
            "%.2f")) {
        selected.qualifierDirectionality = directionality;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Circle at 0; rounded directional cone at 1. "
            "Ctrl+wheel adjusts this on the surface.");
    }
    float aperture = selected.qualifierAperture;
    if (BareSliderFloat(
            "Aperture",
            "RawLabColorWarpQualifierAperture",
            &aperture,
            0.10f,
            1.0f,
            "%.2f")) {
        selected.qualifierAperture = aperture;
        selected.qualifierDirectionality = std::max(
            selected.qualifierDirectionality,
            0.5f);
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Controls the directional cone's side and rear reach. "
            "Drag either square handle to adjust aperture.");
    }
    float directionDegrees =
        selected.qualifierOrientationRadians * 180.0f / pi;
    if (BareSliderFloat(
            "Direction",
            "RawLabColorWarpQualifierDirection",
            &directionDegrees,
            -180.0f,
            180.0f,
            "%.0f deg")) {
        selected.qualifierOrientationRadians = directionDegrees * pi / 180.0f;
        selected.qualifierDirectionality = std::max(
            selected.qualifierDirectionality,
            0.5f);
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Drag the round qualifier handle or use Direction to change orientation.");
    }

    return changed;
}

} // namespace Stack::Editor::RawLabInternal
