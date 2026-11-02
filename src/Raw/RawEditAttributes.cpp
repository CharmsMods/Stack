#include <set>
#include "Raw/RawEditAttributes.h"

#include <algorithm>
#include <initializer_list>
#include <unordered_set>

namespace Stack::RawRecipe {
namespace {

const std::unordered_set<std::string>& KnownAttributeKeys() {
    static const std::unordered_set<std::string> keys = [] {
        std::unordered_set<std::string> result;
        for (const RawEditAttributeDescriptor& descriptor :
             RawEditAttributeDescriptors()) {
            result.insert(descriptor.key);
        }
        return result;
    }();
    return keys;
}

bool HasKey(
    const std::unordered_set<std::string>& selection,
    const char* key) {
    return selection.find(key) != selection.end();
}

void CopyJsonMembersExcept(
    const nlohmann::json& source,
    nlohmann::json& target,
    const std::unordered_set<std::string>& excluded) {
    if (!source.is_object()) {
        return;
    }
    if (!target.is_object()) {
        target = nlohmann::json::object();
    }
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (excluded.find(it.key()) == excluded.end()) {
            target[it.key()] = it.value();
        }
    }
}

void CopyJsonMembers(
    const nlohmann::json& source,
    nlohmann::json& target,
    std::initializer_list<const char*> members) {
    if (!source.is_object()) {
        return;
    }
    if (!target.is_object()) {
        target = nlohmann::json::object();
    }
    for (const char* member : members) {
        const auto value = source.find(member);
        if (value != source.end()) {
            target[member] = *value;
        }
    }
}

bool IsViewValueAttribute(const std::string& key) {
    return key == "view.range" ||
        key == "view.contrast" ||
        key == "view.color" ||
        key == "view.output" ||
        key == "view.diagnostics";
}

void CopyLocalRangeOverall(
    const RawLocalRangeRecipe& source,
    RawLocalRangeRecipe& target) {
    target.enabled = source.enabled;
    target.strength = source.strength;
    target.middleGrey = source.middleGrey;
    target.minEv = source.minEv;
    target.maxEv = source.maxEv;
    target.points = source.points;
    target.smoothness = source.smoothness;
    target.edgeProtection = source.edgeProtection;
    target.detailProtection = source.detailProtection;
    target.highlightProtection = source.highlightProtection;
}

void CopyLocalRangeMasks(
    const RawLocalRangeRecipe& source,
    RawLocalRangeRecipe& target) {
    target.regionMaskEnabled = source.regionMaskEnabled;
    target.regionMaskMode = source.regionMaskMode;
    target.regionMaskInvert = source.regionMaskInvert;
    target.regionMaskCenterX = source.regionMaskCenterX;
    target.regionMaskCenterY = source.regionMaskCenterY;
    target.regionMaskAngleDegrees = source.regionMaskAngleDegrees;
    target.regionMaskSize = source.regionMaskSize;
    target.regionMaskFeather = source.regionMaskFeather;
    target.regionMaskLowEv = source.regionMaskLowEv;
    target.regionMaskHighEv = source.regionMaskHighEv;
    target.colorMaskEnabled = source.colorMaskEnabled;
    target.colorMaskTargetR = source.colorMaskTargetR;
    target.colorMaskTargetG = source.colorMaskTargetG;
    target.colorMaskTargetB = source.colorMaskTargetB;
    target.colorMaskHueWidth = source.colorMaskHueWidth;
    target.colorMaskFeather = source.colorMaskFeather;
    target.colorMaskMinChroma = source.colorMaskMinChroma;
}

void CopyPointCurve(
    const RawDevelopmentRecipe& source,
    RawDevelopmentRecipe& target,
    RawPointCurveChannel channel) {
    StorePointCurveComponentInFinishToneJson(
        target.finishTone.layerJson,
        channel,
        PointCurveComponentFromFinishToneJson(
            source.finishTone.layerJson,
            channel));
}

} // namespace

const std::vector<RawEditAttributeDescriptor>& RawEditAttributeDescriptors() {
    static const std::vector<RawEditAttributeDescriptor> descriptors = {
        {
            "technical.interpretation", "technical", "Technical",
            "RAW Interpretation",
            "Processing version, demosaic, working space, and baseline exposure policy.",
            false, false
        },
        {
            "technical.cfa-denoise", "denoise", "Denoise",
            "CFA Denoise", "Pre-demosaic CFA denoise method and values.",
            true, false
        },
        {
            "exposure.rgb-denoise", "denoise", "Denoise",
            "RGB Denoise", "Post-demosaic RGB denoise method and values.",
            true, false
        },
        {
            "exposure.white-balance", "light", "Light",
            "White Balance", "White-balance mode and authored multipliers.",
            true, false
        },
        {
            "exposure.raw", "light", "Light",
            "RAW Exposure", "Scene-linear RAW exposure.", true, false
        },
        {
            "zones.local-range", "zones", "Zones",
            "Local Range Curve", "Overall Local Range curve and protection values.",
            true, false
        },
        {
            "zones.targets", "zones", "Zones",
            "Areas and targets",
            "Painted areas, their gain graphs, and existing target zones. Review on each target image.",
            false, true
        },
        {
            "zones.masks", "zones", "Zones",
            "Range Masks",
            "Spatial and color mask geometry. Review on each target image.",
            false, true
        },
        {"curve.luminance", "tone", "Tone", "Luminance Curve / Contrast",
            "Linked scene-luminance curve, contrast, pivot and outer tone protection.", true, false},
        {"detail.contrast", "detail", "Detail", "Detail Contrast",
            "Source-pixel scale response, EV targeting and the EV by scale field.", true, false},
        {
            "curve.settings", "tone", "Tone",
            "Curve Settings", "Curve domain, range, and shared behavior.",
            true, false
        },
        {
            "curve.composite", "tone", "Tone",
            "Composite", "Composite point curve.", true, false
        },
        {
            "curve.red", "tone", "Tone",
            "Red", "Red point curve.", true, false
        },
        {
            "curve.green", "tone", "Tone",
            "Green", "Green point curve.", true, false
        },
        {
            "curve.blue", "tone", "Tone",
            "Blue", "Blue point curve.", true, false
        },
        {
            "view.range", "view", "View",
            "Exposure and Grey", "View exposure and middle-grey values.",
            true, false
        },
        {
            "view.contrast", "view", "View",
            "Contrast and Pivot", "View contrast model, amount, and pivot.",
            true, false
        },
        {
            "view.color", "view", "View",
            "Color", "View saturation and hue-preservation values.",
            true, false
        },
        {
            "view.output", "view", "View",
            "Output Encoding", "View output encoding and color-space intent.",
            true, false
        },
        {
            "view.diagnostics", "view", "View",
            "Diagnostics", "False-color diagnostic state.", false, false
        },
        {
            "view.placement", "view", "View",
            "Transform Placement",
            "Keep display mapping inside RAW or place it at the graph output.",
            false, false
        },
        {
            "geometry.crop", "geometry", "Geometry",
            "Crop", "Normalized crop rectangle. Review on each target image.",
            false, true
        },
        {
            "geometry.orientation", "geometry", "Geometry",
            "Rotate and Flip", "Quarter-turn rotation and horizontal/vertical flips.",
            false, false
        }
    };
    return descriptors;
}

const RawEditAttributeDescriptor* FindRawEditAttributeDescriptor(
    const std::string& key) {
    const auto& descriptors = RawEditAttributeDescriptors();
    const auto found = std::find_if(
        descriptors.begin(), descriptors.end(),
        [&](const RawEditAttributeDescriptor& descriptor) {
            return key == descriptor.key;
        });
    return found == descriptors.end() ? nullptr : &(*found);
}

std::vector<std::string> DefaultRawEditAttributeSelection() {
    std::vector<std::string> result;
    for (const RawEditAttributeDescriptor& descriptor :
         RawEditAttributeDescriptors()) {
        if (descriptor.selectedByDefault) {
            result.emplace_back(descriptor.key);
        }
    }
    return result;
}

std::vector<std::string> AllRawEditAttributeKeys() {
    std::vector<std::string> result;
    result.reserve(RawEditAttributeDescriptors().size());
    for (const RawEditAttributeDescriptor& descriptor :
         RawEditAttributeDescriptors()) {
        result.emplace_back(descriptor.key);
    }
    return result;
}

RawEditAttributeSelectionPlan PlanRawEditAttributeSelectionForTarget(
    const RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const RawEditAttributeTargetCompatibility& compatibility) {
    RawEditAttributeSelectionPlan plan;
    plan.applicableKeys.reserve(selectedKeys.size());
    for (const std::string& key : selectedKeys) {
        std::string warning;
        if (key == "technical.interpretation" &&
            compatibility.postCfaMerge) {
            warning =
                "RAW Interpretation was skipped because the target is already CFA-merged.";
        } else if (key == "technical.cfa-denoise" &&
                   compatibility.hdrMerge) {
            warning =
                "CFA Denoise was skipped because the HDR target is already CFA-merged.";
        } else if (
            key == "view.placement" &&
            ((bundle.viewTransformPlacement == "graph") !=
             compatibility.graphViewTransform) &&
            !compatibility.canChangeViewTransformPlacement) {
            warning =
                "Transform Placement was skipped because changing graph topology requires an explicit open-project action.";
        } else if (
            IsViewValueAttribute(key) &&
            compatibility.graphViewTransform &&
            !compatibility.canUpdateGraphViewTransform) {
            warning =
                "View values were skipped because this target's display mapping is a graph node that cannot be changed offline.";
        }

        if (warning.empty()) {
            plan.applicableKeys.push_back(key);
        } else {
            plan.skippedKeys.push_back(key);
            if (std::find(
                    plan.warnings.begin(),
                    plan.warnings.end(),
                    warning) == plan.warnings.end()) {
                plan.warnings.push_back(std::move(warning));
            }
        }
    }
    return plan;
}

void CaptureAuthoredLayers(RawEditAttributeBundle& bundle, const nlohmann::json& stack,
    const std::string& sourceProject) {
    bundle.authoredLayers.reset();
    if (!stack.is_object()) return;
    auto edits = std::make_shared<AuthoredLayerEdits>();
    if (stack.contains("nodeGraph")) {
        if (!stack.at("nodeGraph").is_object() || !stack.at("nodeGraph").contains("nodes") ||
            !stack.at("nodeGraph").at("nodes").is_array()) return;
        auto graph=stack;
        graph.erase("rawLayerStack");
        const std::string parent="layer/@project";
        edits->choices.push_back({parent,"Project graph",{}});
        try {
            std::set<int> inbound,outbound;
            for (const auto& link:graph.at("nodeGraph").at("links")) {
                if (link.at("toSocket")=="imageIn") inbound.insert(link.at("toNodeId").get<int>());
                if (link.at("fromSocket")=="imageOut") outbound.insert(link.at("fromNodeId").get<int>());
            }
            for (auto& node : graph["nodeGraph"]["nodes"]) {
                const auto kind=node.value("kind",std::string{});
                const int id=node.at("id").get<int>();
                if (kind=="Layer" || kind=="RawOperation" || (inbound.count(id) && outbound.count(id))) {
                    const auto label=node.value("title",std::string{});
                    edits->choices.push_back({parent+"/"+node.at("instanceUuid").get<std::string>(),
                        label.empty() ? node.value("layerType",kind) : label,parent});
                }
                if (kind=="Image" || kind=="RawDevelopment" || kind=="RawSource" || kind=="RawProjectFrame" ||
                    kind=="MultiFrameDenoise" || kind=="MultiFrameHdr") {
                    // Only a source socket is reusable. Captures and input bytes stay in the source project.
                    nlohmann::json socket;
                    for (const auto* key : {"id","instanceUuid","kind","x","y"}) if (node.contains(key)) socket[key]=node.at(key);
                    node=std::move(socket);
                }
            }
        } catch (const nlohmann::json::exception&) { return; }
        if (stack.contains("rawLayerStack")) {
            CaptureAuthoredLayers(bundle,stack.at("rawLayerStack"),sourceProject);
            if (!bundle.authoredLayers) return;
            // A plain RAW source connected to Output adds no composition edits.
            if (edits->choices.size()==1 && graph["nodeGraph"]["nodes"].size()==2) return;
            graph["rawLayerStack"]=bundle.authoredLayers->stack;
            const auto& choices=bundle.authoredLayers->choices;
            edits->choices.insert(edits->choices.end(),choices.begin(),choices.end());
        }
        edits->stack=std::move(graph);
        edits->sourceProject=sourceProject;
        bundle.authoredLayers=std::move(edits);
        return;
    }
    if (!stack.contains("background") || !stack.contains("layers") || !stack.at("layers").is_array()) return;
    auto collect = [&](const nlohmann::json& layer) {
        const auto key = "layer/" + layer.at("id").get<std::string>();
        edits->choices.push_back({key, layer.at("name").get<std::string>(), {}});
        for (const auto& node : layer.at("graph").at("nodeGraph").at("nodes")) {
            if (!node.contains("rawOperation")) continue;
            const auto& operation = node.at("rawOperation");
            edits->choices.push_back({key + "/" + node.at("instanceUuid").get<std::string>(),
                operation.at("kind").get<std::string>(), key});
        }
    };
    try {
        collect(stack.at("background"));
        for (const auto& layer : stack.at("layers")) collect(layer);
    } catch (const nlohmann::json::exception&) { return; }
    edits->stack = stack;
    edits->sourceProject = sourceProject;
    bundle.authoredLayers = std::move(edits);
}

RawEditAttributeBundle CaptureRawEditAttributeBundle(
    const RawDevelopmentRecipe& recipe,
    std::string sourceLabel,
    std::string sourceKind,
    std::string viewTransformPlacement) {
    RawEditAttributeBundle bundle;
    bundle.sourceLabel = std::move(sourceLabel);
    bundle.sourceKind = sourceKind.empty() ? "single-raw" : std::move(sourceKind);
    bundle.viewTransformPlacement =
        viewTransformPlacement == "graph" ? "graph" : "internal";
    bundle.recipe = DeserializeRecipe(SerializeRecipe(recipe));
    // The clipboard is an edit description, never a source relink operation.
    bundle.recipe.source = {};
    return bundle;
}

nlohmann::json SerializeRawEditAttributeBundle(
    const RawEditAttributeBundle& bundle) {
    RawDevelopmentRecipe portableRecipe =
        DeserializeRecipe(SerializeRecipe(bundle.recipe));
    portableRecipe.source = {};
    nlohmann::json result = {
        { "schema", kRawEditAttributeBundleSchema },
        { "version", kRawEditAttributeBundleVersion },
        { "sourceLabel", bundle.sourceLabel },
        { "sourceKind", bundle.sourceKind },
        { "viewTransformPlacement",
            bundle.viewTransformPlacement == "graph" ? "graph" : "internal" },
        { "recipe", SerializeRecipe(portableRecipe) }
    };
    if (!bundle.copiedSelection.empty()) result["selection"] = bundle.copiedSelection;
    if (bundle.authoredLayers) {
        result["layerStack"] = bundle.authoredLayers->stack;
        result["assetProject"] = bundle.authoredLayers->sourceProject;
    }
    return result;
}

bool DeserializeRawEditAttributeBundle(
    const nlohmann::json& value,
    RawEditAttributeBundle& bundle,
    std::string* errorMessage) {
    const auto fail = [&](const std::string& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };
    if (!value.is_object() ||
        value.value("schema", std::string()) != kRawEditAttributeBundleSchema) {
        return fail("The clipboard does not contain Stack RAW edit attributes.");
    }
    const std::uint32_t version = value.value("version", 0u);
    if (version != kRawEditAttributeBundleVersion) {
        return fail("This RAW edit-attribute bundle version is not supported.");
    }
    const auto recipe = value.find("recipe");
    if (recipe == value.end() || !recipe->is_object()) {
        return fail("The RAW edit-attribute bundle has no recipe.");
    }

    RawEditAttributeBundle loaded;
    loaded.version = version;
    loaded.sourceLabel = value.value("sourceLabel", std::string());
    loaded.sourceKind = value.value("sourceKind", std::string("single-raw"));
    loaded.viewTransformPlacement =
        value.value("viewTransformPlacement", std::string()) == "graph"
            ? "graph"
            : "internal";
    loaded.recipe = DeserializeRecipe(*recipe);
    loaded.recipe.source = {};
    if (value.contains("layerStack"))
        CaptureAuthoredLayers(loaded, value.at("layerStack"), value.value("assetProject",std::string{}));
    if (value.contains("selection") && value.at("selection").is_array())
        for (const auto& key : value.at("selection")) if (key.is_string()) loaded.copiedSelection.push_back(key.get<std::string>());
    bundle = std::move(loaded);
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

RawEditAttributeApplyResult ApplyRawEditAttributeBundle(
    const RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    RawDevelopmentRecipe& targetRecipe,
    std::string* targetViewTransformPlacement) {
    RawEditAttributeApplyResult result;
    if (bundle.version != kRawEditAttributeBundleVersion) {
        result.errorMessage =
            "This RAW edit-attribute bundle version is not supported.";
        return result;
    }

    std::unordered_set<std::string> selection;
    for (const std::string& key : selectedKeys) {
        if (KnownAttributeKeys().find(key) == KnownAttributeKeys().end()) {
            result.errorMessage = "Unknown RAW edit attribute: " + key;
            return result;
        }
        selection.insert(key);
    }
    if (selection.empty()) {
        result.errorMessage = "Select at least one RAW edit attribute to paste.";
        return result;
    }

    const RawSourceReference targetSource = targetRecipe.source;
    const std::vector<std::string> targetStageOrder = targetRecipe.stageOrder;
    const nlohmann::json before = SerializeRecipe(targetRecipe);
    const std::string placementBefore = targetViewTransformPlacement
        ? (*targetViewTransformPlacement == "graph" ? "graph" : "internal")
        : std::string();
    const RawDevelopmentRecipe source =
        DeserializeRecipe(SerializeRecipe(bundle.recipe));

    const auto applied = [&](const char* key) {
        if (HasKey(selection, key)) {
            result.appliedKeys.emplace_back(key);
            return true;
        }
        return false;
    };

    if (applied("technical.interpretation")) {
        const auto mosaicDenoise = targetRecipe.technical.mosaicDenoise;
        const bool encodeSrgbOutput = targetRecipe.technical.encodeSrgbOutput;
        targetRecipe.technical = source.technical;
        targetRecipe.technical.mosaicDenoise = mosaicDenoise;
        targetRecipe.technical.encodeSrgbOutput = encodeSrgbOutput;
    }
    if (applied("technical.cfa-denoise")) {
        targetRecipe.technical.mosaicDenoise =
            source.technical.mosaicDenoise;
    }
    if (applied("exposure.white-balance")) {
        targetRecipe.whiteBalance = source.whiteBalance;
    }
    if (applied("exposure.rgb-denoise")) {
        targetRecipe.rgbDenoise = source.rgbDenoise;
    }
    if (applied("exposure.raw")) {
        targetRecipe.preToneExposureEv = source.preToneExposureEv;
    }
    if (applied("zones.local-range")) {
        CopyLocalRangeOverall(source.localRange, targetRecipe.localRange);
    }
    if (applied("zones.targets")) {
        targetRecipe.localRange.targetZoneCombineMode =
            source.localRange.targetZoneCombineMode;
        targetRecipe.localRange.targetZones = source.localRange.targetZones;
        targetRecipe.localRange.areasVersion = source.localRange.areasVersion;
        targetRecipe.localRange.areas = source.localRange.areas;
        result.warnings.emplace_back(
            "Targeted Areas use normalized positions; inspect every pasted image before export.");
    }
    if (applied("zones.masks")) {
        CopyLocalRangeMasks(source.localRange, targetRecipe.localRange);
        result.warnings.emplace_back(
            "Range Masks are spatially specific; inspect every pasted image before export.");
    }

    if (applied("detail.contrast")) targetRecipe.detailContrast = source.detailContrast;
    if (applied("curve.luminance")) targetRecipe.finishTone.layerJson["luminanceTone"] =
        source.finishTone.layerJson.value("luminanceTone", SerializeSceneTone({}));
    if (applied("curve.settings")) {
        CopyJsonMembersExcept(
            source.finishTone.layerJson,
            targetRecipe.finishTone.layerJson,
            { "pointCurves", "activeGraphView", "luminanceTone" });
    }
    if (applied("curve.composite")) {
        CopyPointCurve(
            source, targetRecipe, RawPointCurveChannel::Composite);
    }
    if (applied("curve.red")) {
        CopyPointCurve(source, targetRecipe, RawPointCurveChannel::Red);
    }
    if (applied("curve.green")) {
        CopyPointCurve(source, targetRecipe, RawPointCurveChannel::Green);
    }
    if (applied("curve.blue")) {
        CopyPointCurve(source, targetRecipe, RawPointCurveChannel::Blue);
    }

    if (applied("view.range")) {
        CopyJsonMembers(
            source.viewTransform.layerJson,
            targetRecipe.viewTransform.layerJson,
            { "displayCurve", "exposure", "middleGrey" });
    }
    if (applied("view.contrast")) {
        CopyJsonMembers(
            source.viewTransform.layerJson,
            targetRecipe.viewTransform.layerJson,
            { "contrast", "contrastPivotEv", "contrastModel" });
    }
    if (applied("view.color")) {
        CopyJsonMembers(
            source.viewTransform.layerJson,
            targetRecipe.viewTransform.layerJson,
            { "saturation", "preserveHue" });
    }
    if (applied("view.output")) {
        CopyJsonMembers(
            source.viewTransform.layerJson,
            targetRecipe.viewTransform.layerJson,
            { "encodeSrgbOutput" });
        targetRecipe.technical.encodeSrgbOutput =
            source.technical.encodeSrgbOutput;
    }
    if (applied("view.diagnostics")) {
        CopyJsonMembers(
            source.viewTransform.layerJson,
            targetRecipe.viewTransform.layerJson,
            { "debugFalseColor" });
    }
    if (applied("view.placement") && targetViewTransformPlacement) {
        *targetViewTransformPlacement =
            bundle.viewTransformPlacement == "graph" ? "graph" : "internal";
    }
    if (applied("geometry.crop")) {
        targetRecipe.cropRotation.cropEnabled =
            source.cropRotation.cropEnabled;
        targetRecipe.cropRotation.cropX = source.cropRotation.cropX;
        targetRecipe.cropRotation.cropY = source.cropRotation.cropY;
        targetRecipe.cropRotation.cropWidth = source.cropRotation.cropWidth;
        targetRecipe.cropRotation.cropHeight = source.cropRotation.cropHeight;
        result.warnings.emplace_back(
            "Crop is spatially specific; inspect every pasted image before export.");
    }
    if (applied("geometry.orientation")) {
        targetRecipe.cropRotation.rotationDegrees =
            source.cropRotation.rotationDegrees;
        targetRecipe.cropRotation.flipHorizontally =
            source.cropRotation.flipHorizontally;
        targetRecipe.cropRotation.flipVertically =
            source.cropRotation.flipVertically;
    }

    // Serialization is the canonical sanitizer and also keeps View's input
    // working space/output transfer synchronized with the target recipe.
    targetRecipe.source = targetSource;
    targetRecipe.stageOrder = targetStageOrder;
    targetRecipe = DeserializeRecipe(SerializeRecipe(targetRecipe));
    targetRecipe.source = targetSource;
    targetRecipe.stageOrder = targetStageOrder;
    result.success = true;
    const bool placementChanged = targetViewTransformPlacement &&
        (*targetViewTransformPlacement == "graph" ? "graph" : "internal") !=
            placementBefore;
    result.changed = SerializeRecipe(targetRecipe) != before ||
        placementChanged;
    return result;
}

} // namespace Stack::RawRecipe
