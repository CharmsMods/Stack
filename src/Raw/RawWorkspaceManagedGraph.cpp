#include "Raw/RawWorkspaceManagedGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace Stack::RawWorkspace {
namespace {

constexpr float kFloatEpsilon = 0.0001f;

bool AlmostEqual(float a, float b) {
    return std::fabs(a - b) <= kFloatEpsilon;
}

bool IsDefaultPreviewOutput(const Stack::RawRecipe::RawPreviewOutputRecipe& previewOutput) {
    return previewOutput.previewIntent == "developed-preview" &&
        previewOutput.internalViewTransform == "scene-linear-to-display" &&
        previewOutput.outputColorSpace == "sRGB";
}

bool IsDefaultStageOrder(const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    return recipe.stageOrder.empty() ||
        recipe.stageOrder == Stack::RawRecipe::DefaultStageOrder();
}

bool IsSupportedRotation(int degrees) {
    const int normalized = ((degrees % 360) + 360) % 360;
    return normalized == 0 || normalized == 90 || normalized == 180 || normalized == 270;
}

bool RawMosaicDenoiseMatchesDefault(const Raw::RawMosaicDenoiseSettings& settings) {
    const Raw::RawMosaicDenoiseSettings defaults;
    return settings.enabled == defaults.enabled &&
        settings.hotPixelSuppression == defaults.hotPixelSuppression &&
        AlmostEqual(settings.hotPixelThreshold, defaults.hotPixelThreshold) &&
        AlmostEqual(settings.lumaStrength, defaults.lumaStrength) &&
        AlmostEqual(settings.chromaStrength, defaults.chromaStrength) &&
        settings.radius == defaults.radius &&
        AlmostEqual(settings.edgeProtection, defaults.edgeProtection) &&
        settings.iterations == defaults.iterations;
}

bool RawDecodeSettingsUseOnlyManagedFields(
    const Raw::RawDevelopSettings& settings,
    std::string* outReason) {
    const Raw::RawDevelopSettings defaults;
    auto fail = [&](const std::string& reason) {
        if (outReason) {
            *outReason = reason;
        }
        return false;
    };

    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Neutral) {
        return fail("Neutral RAW Decode white balance is not part of the managed RAW recipe mapping.");
    }
    if (!IsSupportedRotation(settings.rotationDegrees)) {
        return fail("RAW Decode rotation must remain 0, 90, 180, or 270 degrees in managed mode.");
    }
    if (settings.overrideBlackLevel != defaults.overrideBlackLevel ||
        !AlmostEqual(settings.blackLevelOverride, defaults.blackLevelOverride) ||
        settings.overrideWhiteLevel != defaults.overrideWhiteLevel ||
        !AlmostEqual(settings.whiteLevelOverride, defaults.whiteLevelOverride)) {
        return fail("RAW Decode black/white level overrides are outside the managed RAW recipe mapping.");
    }
    if (settings.highlightMode != defaults.highlightMode ||
        !AlmostEqual(settings.highlightStrength, defaults.highlightStrength) ||
        !AlmostEqual(settings.highlightThreshold, defaults.highlightThreshold)) {
        return fail("RAW Decode highlight reconstruction settings are outside the managed RAW recipe mapping.");
    }
    if (settings.demosaicMethod != defaults.demosaicMethod ||
        settings.cameraTransformEnabled != defaults.cameraTransformEnabled ||
        settings.cameraTransformSource != defaults.cameraTransformSource ||
        settings.debugBypassCameraTransform != defaults.debugBypassCameraTransform ||
        settings.debugTransposeCameraMatrix != defaults.debugTransposeCameraMatrix ||
        settings.debugView != defaults.debugView) {
        return fail("RAW Decode camera transform/debug settings are outside the managed RAW recipe mapping.");
    }
    if (settings.rotateToFitFrame != defaults.rotateToFitFrame ||
        settings.flipHorizontally != defaults.flipHorizontally ||
        settings.flipVertically != defaults.flipVertically) {
        return fail("RAW Decode orientation controls beyond recipe rotation are outside the managed RAW recipe mapping.");
    }
    if (!AlmostEqual(settings.falseColorSuppression, defaults.falseColorSuppression) ||
        !AlmostEqual(settings.defringeStrength, defaults.defringeStrength) ||
        !AlmostEqual(settings.highlightEdgeCleanup, defaults.highlightEdgeCleanup) ||
        settings.chromaRadius != defaults.chromaRadius ||
        !AlmostEqual(settings.preserveRealColor, defaults.preserveRealColor) ||
        !AlmostEqual(settings.lateralRedCyan, defaults.lateralRedCyan) ||
        !AlmostEqual(settings.lateralBlueYellow, defaults.lateralBlueYellow) ||
        !RawMosaicDenoiseMatchesDefault(settings.mosaicDenoise)) {
        return fail("RAW Decode detail/color cleanup settings are outside the managed RAW recipe mapping.");
    }
    if (outReason) {
        outReason->clear();
    }
    return true;
}

std::vector<int> JsonIntVector(const nlohmann::json& value) {
    std::vector<int> result;
    if (!value.is_array()) {
        return result;
    }
    result.reserve(value.size());
    for (const nlohmann::json& item : value) {
        if (item.is_number_integer()) {
            result.push_back(item.get<int>());
        }
    }
    return result;
}

bool ContainsNodeId(const std::vector<int>& ids, int nodeId) {
    return std::find(ids.begin(), ids.end(), nodeId) != ids.end();
}

std::string NormalizeSourceIdentity(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string ResolvedRawSourcePath(const EditorNodeGraph::RawSourcePayload& rawSource) {
    return rawSource.sourcePath.empty()
        ? rawSource.metadata.sourcePath
        : rawSource.sourcePath;
}

bool NonEmptySourceIdentityMatches(const std::string& expected, const std::string& actual) {
    if (expected.empty() || actual.empty()) {
        return true;
    }
    return NormalizeSourceIdentity(expected) == NormalizeSourceIdentity(actual);
}

bool IsRequiredManagedLink(
    const EditorNodeGraph::Link& link,
    const ManagedRawSection& section) {
    return
        (link.fromNodeId == section.rawSourceNodeId &&
         link.fromSocketId == EditorNodeGraph::kRawOutputSocketId &&
         link.toNodeId == section.rawDecodeNodeId &&
         link.toSocketId == EditorNodeGraph::kRawInputSocketId) ||
        (link.fromNodeId == section.rawDecodeNodeId &&
         link.fromSocketId == EditorNodeGraph::kImageOutputSocketId &&
         link.toNodeId == section.toneCurveNodeId &&
         link.toSocketId == EditorNodeGraph::kImageInputSocketId) ||
        (link.fromNodeId == section.toneCurveNodeId &&
         link.fromSocketId == EditorNodeGraph::kImageOutputSocketId &&
         link.toNodeId == section.viewTransformNodeId &&
         link.toSocketId == EditorNodeGraph::kImageInputSocketId);
}

bool HasRequiredLink(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    return graph.HasLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
}

bool IsManagedRawNode(const ManagedRawSection& section, int nodeId) {
    return nodeId == section.rawSourceNodeId ||
        nodeId == section.rawDecodeNodeId ||
        nodeId == section.toneCurveNodeId ||
        nodeId == section.viewTransformNodeId;
}

bool IsManagedRawInternalStageOutput(
    const ManagedRawSection& section,
    int nodeId,
    const std::string& socketId) {
    return
        (nodeId == section.rawSourceNodeId &&
         socketId == EditorNodeGraph::kRawOutputSocketId) ||
        (nodeId == section.rawDecodeNodeId &&
         socketId == EditorNodeGraph::kImageOutputSocketId) ||
        (nodeId == section.toneCurveNodeId &&
         socketId == EditorNodeGraph::kImageOutputSocketId);
}

bool IsManagedRawInternalStageInput(
    const ManagedRawSection& section,
    int nodeId,
    const std::string& socketId) {
    return
        (nodeId == section.rawDecodeNodeId &&
         socketId == EditorNodeGraph::kRawInputSocketId) ||
        (nodeId == section.toneCurveNodeId &&
         socketId == EditorNodeGraph::kImageInputSocketId) ||
        (nodeId == section.viewTransformNodeId &&
         socketId == EditorNodeGraph::kImageInputSocketId);
}

ManagedRawGraphMutationWarning MakeManagedRawGraphMutationWarning(
    std::string summary,
    std::string detail) {
    ManagedRawGraphMutationWarning warning;
    warning.requiresConfirmation = true;
    warning.summary = std::move(summary);
    warning.detail = std::move(detail);
    return warning;
}

std::string SectionIdForChain(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    int rawSourceNodeId,
    int rawDecodeNodeId) {
    std::ostringstream out;
    out << "managed-raw:";
    if (!recipe.source.relativePathKey.empty()) {
        out << recipe.source.relativePathKey;
    } else if (!recipe.source.sourcePath.empty()) {
        out << recipe.source.sourcePath;
    } else {
        out << "source";
    }
    out << ":" << rawSourceNodeId << "-" << rawDecodeNodeId;
    return out.str();
}

Stack::RawRecipe::RawDevelopmentRecipe RecipeFromDecodeSettings(
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe,
    const Raw::RawDevelopSettings& settings) {
    Stack::RawRecipe::RawDevelopmentRecipe recipe = baseRecipe;
    recipe.preToneExposureEv = settings.exposureStops;
    recipe.cropRotation.rotationDegrees = ((settings.rotationDegrees % 360) + 360) % 360;

    recipe.whiteBalance.hasTemperatureKelvin = false;
    recipe.whiteBalance.temperatureKelvin = 0.0f;
    recipe.whiteBalance.hasTint = false;
    recipe.whiteBalance.tint = 0.0f;
    recipe.whiteBalance.hasSamplePoint = false;
    recipe.whiteBalance.sampleX = 0.5f;
    recipe.whiteBalance.sampleY = 0.5f;
    recipe.whiteBalance.hasMultipliers = false;
    recipe.whiteBalance.multipliers = { 1.0f, 1.0f, 1.0f };

    switch (settings.whiteBalanceMode) {
        case Raw::WhiteBalanceMode::Auto:
            recipe.whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::Auto;
            break;
        case Raw::WhiteBalanceMode::Manual:
            recipe.whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
            recipe.whiteBalance.hasMultipliers = true;
            recipe.whiteBalance.multipliers = settings.manualWhiteBalance;
            break;
        case Raw::WhiteBalanceMode::AsShot:
        case Raw::WhiteBalanceMode::Neutral:
        default:
            recipe.whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::AsShot;
            break;
    }

    recipe.toneCurve.mode = Stack::RawRecipe::ToneCurveMode::Default;
    recipe.toneCurve.points = {
        Stack::RawRecipe::RawToneCurvePoint{ 0.0f, 0.0f },
        Stack::RawRecipe::RawToneCurvePoint{ 1.0f, 1.0f }
    };
    recipe.localExposure = Stack::RawRecipe::RawLocalExposureRecipe {};
    recipe.stageOrder = Stack::RawRecipe::DefaultStageOrder();
    recipe.previewOutput.previewIntent = "developed-preview";
    recipe.previewOutput.internalViewTransform = "scene-linear-to-display";
    recipe.previewOutput.outputColorSpace = "sRGB";
    return recipe;
}

} // namespace

bool IsRecipeRepresentableAsManagedGraph(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    std::string* outReason) {
    auto fail = [&](const std::string& reason) {
        if (outReason) {
            *outReason = reason;
        }
        return false;
    };

    if (!IsDefaultStageOrder(recipe)) {
        return fail("This recipe uses a custom RAW stage order that cannot be represented by the managed graph contract yet.");
    }
    if (recipe.whiteBalance.hasTemperatureKelvin || recipe.whiteBalance.hasTint) {
        return fail("Temperature/tint white balance values cannot round-trip through the managed RAW Decode node yet.");
    }
    if (recipe.whiteBalance.hasSamplePoint ||
        recipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::SampledGrayPoint) {
        return fail("Sampled gray-point white balance cannot round-trip through the managed graph contract yet.");
    }
    if ((recipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers) &&
        !recipe.whiteBalance.hasMultipliers) {
        return fail("Custom white balance needs RGB multipliers before it can be decomposed.");
    }
    if (Stack::RawRecipe::IsLocalExposureEnabled(recipe)) {
        return fail("Local exposure cannot round-trip through the managed graph contract yet.");
    }
    if (Stack::RawRecipe::IsLocalRangeEnabled(recipe)) {
        return fail("Local range cannot round-trip through the managed graph contract yet.");
    }
    if (recipe.cropRotation.cropEnabled) {
        return fail("Crop edits cannot be represented by the managed graph contract yet.");
    }
    if (!IsSupportedRotation(recipe.cropRotation.rotationDegrees)) {
        return fail("RAW rotation must be 0, 90, 180, or 270 degrees to decompose safely.");
    }
    if (!IsDefaultPreviewOutput(recipe.previewOutput)) {
        return fail("Preview/output settings cannot round-trip through the managed View Transform node yet.");
    }

    if (outReason) {
        outReason->clear();
    }
    return true;
}

ManagedRawSection BuildManagedRawSection(
    std::string sectionId,
    std::string projectLocalId,
    std::string sourceRelativePathKey,
    std::string sourceFingerprint,
    int groupId,
    int rawSourceNodeId,
    int rawDecodeNodeId,
    int toneCurveNodeId,
    int viewTransformNodeId) {
    ManagedRawSection section;
    section.sectionId = std::move(sectionId);
    section.projectLocalId = std::move(projectLocalId);
    section.sourceRelativePathKey = std::move(sourceRelativePathKey);
    section.sourceFingerprint = std::move(sourceFingerprint);
    section.groupId = groupId;
    section.rawSourceNodeId = rawSourceNodeId;
    section.rawDecodeNodeId = rawDecodeNodeId;
    section.toneCurveNodeId = toneCurveNodeId;
    section.viewTransformNodeId = viewTransformNodeId;
    section.orderedNodeIds = {
        rawSourceNodeId,
        rawDecodeNodeId,
        toneCurveNodeId,
        viewTransformNodeId
    };
    section.lockedFoundationNodeIds = {
        rawSourceNodeId,
        rawDecodeNodeId
    };
    return section;
}

nlohmann::json SerializeManagedRawSection(const ManagedRawSection& section) {
    return {
        { "schema", "stack.rawWorkspace.managedRawSection" },
        { "schemaVersion", section.schemaVersion },
        { "sectionId", section.sectionId },
        { "projectLocalId", section.projectLocalId },
        { "sourceRawRef", {
            { "relativePathKey", section.sourceRelativePathKey },
            { "fingerprint", section.sourceFingerprint.empty() ? nlohmann::json() : nlohmann::json(section.sourceFingerprint) }
        } },
        { "groupId", section.groupId },
        { "managedNodeIds", {
            { "rawSource", section.rawSourceNodeId },
            { "rawDecode", section.rawDecodeNodeId },
            { "toneCurve", section.toneCurveNodeId },
            { "viewTransform", section.viewTransformNodeId }
        } },
        { "orderedNodeIds", section.orderedNodeIds },
        { "lockedFoundationNodeIds", section.lockedFoundationNodeIds },
        { "flexibleStageNodeIds", nlohmann::json::array() },
        { "stageSlots", {
            { "lockedFoundation", nlohmann::json::array({ "raw-source", "raw-decode" }) },
            { "tone", "tone-curve" },
            { "previewOutput", "view-transform" }
        } },
        { "sectionOutput", {
            { "nodeId", section.viewTransformNodeId },
            { "socketId", section.sectionOutputSocketId }
        } },
        { "recipeFieldMappings", {
            { "rawSourceRef", "rawSource.metadata" },
            { "preToneExposureEv", "rawDecode.settings.exposureStops" },
            { "whiteBalance", "rawDecode.settings.whiteBalance" },
            { "cropRotation.rotationDegrees", "rawDecode.settings.rotationDegrees" },
            { "finishTone", "toneCurve.layer" },
            { "viewTransform", "viewTransform.layer" }
        } },
        { "modeState", "managed-decomposed" },
        { "validatorVersion", section.validatorVersion }
    };
}

ManagedRawSection DeserializeManagedRawSection(const nlohmann::json& value) {
    ManagedRawSection section;
    if (!value.is_object()) {
        return section;
    }

    section.schemaVersion = value.value("schemaVersion", section.schemaVersion);
    section.sectionId = value.value("sectionId", section.sectionId);
    section.projectLocalId = value.value("projectLocalId", section.projectLocalId);
    const nlohmann::json sourceRawRef = value.value("sourceRawRef", nlohmann::json::object());
    if (sourceRawRef.is_object()) {
        section.sourceRelativePathKey = sourceRawRef.value("relativePathKey", section.sourceRelativePathKey);
        if (sourceRawRef.contains("fingerprint") && sourceRawRef["fingerprint"].is_string()) {
            section.sourceFingerprint = sourceRawRef["fingerprint"].get<std::string>();
        }
    } else {
        section.sourceRelativePathKey = value.value("sourceRelativePathKey", section.sourceRelativePathKey);
    }
    section.groupId = value.value("groupId", section.groupId);

    const nlohmann::json managedNodeIds = value.value("managedNodeIds", nlohmann::json::object());
    if (managedNodeIds.is_object()) {
        section.rawSourceNodeId = managedNodeIds.value("rawSource", section.rawSourceNodeId);
        section.rawDecodeNodeId = managedNodeIds.value("rawDecode", section.rawDecodeNodeId);
        section.toneCurveNodeId = managedNodeIds.value("toneCurve", section.toneCurveNodeId);
        section.viewTransformNodeId = managedNodeIds.value("viewTransform", section.viewTransformNodeId);
    } else {
        section.rawSourceNodeId = value.value("rawSourceNodeId", section.rawSourceNodeId);
        section.rawDecodeNodeId = value.value("rawDecodeNodeId", section.rawDecodeNodeId);
        section.toneCurveNodeId = value.value("toneCurveNodeId", section.toneCurveNodeId);
        section.viewTransformNodeId = value.value("viewTransformNodeId", section.viewTransformNodeId);
    }

    section.orderedNodeIds = JsonIntVector(value.value("orderedNodeIds", nlohmann::json::array()));
    section.lockedFoundationNodeIds = JsonIntVector(value.value("lockedFoundationNodeIds", nlohmann::json::array()));
    const nlohmann::json sectionOutput = value.value("sectionOutput", nlohmann::json::object());
    if (sectionOutput.is_object()) {
        section.sectionOutputSocketId = sectionOutput.value("socketId", section.sectionOutputSocketId);
    }
    section.validatorVersion = value.value("validatorVersion", section.validatorVersion);
    return section;
}

Stack::RawRecipe::RawDevelopmentRecipe BuildRecipeFromManagedRawGraph(
    const EditorNodeGraph::Graph& graph,
    const ManagedRawSection& section,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe) {
    const EditorNodeGraph::Node* decodeNode = graph.FindNode(section.rawDecodeNodeId);
    if (!decodeNode || decodeNode->kind != EditorNodeGraph::NodeKind::RawDecode) {
        return baseRecipe;
    }
    return RecipeFromDecodeSettings(baseRecipe, decodeNode->rawDecode.settings);
}

ManagedRawValidationResult ValidateManagedRawSection(
    const EditorNodeGraph::Graph& graph,
    const ManagedRawSection& section,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe) {
    ManagedRawValidationResult result;
    result.recipe = baseRecipe;

    auto fail = [&](std::string message, bool repairable = false) {
        result.valid = false;
        result.repairable = repairable;
        result.message = std::move(message);
        return result;
    };

    if (section.schemaVersion != kManagedRawSectionSchemaVersion ||
        section.validatorVersion != kManagedRawValidatorVersion) {
        return fail("The managed RAW section uses an unsupported validator/schema version.");
    }

    const std::vector<int> requiredOrder = {
        section.rawSourceNodeId,
        section.rawDecodeNodeId,
        section.toneCurveNodeId,
        section.viewTransformNodeId
    };
    if (std::any_of(requiredOrder.begin(), requiredOrder.end(), [](int id) { return id <= 0; })) {
        return fail("The managed RAW section metadata is missing required node ids.", true);
    }
    if (section.orderedNodeIds != requiredOrder) {
        return fail("The managed RAW section order no longer matches the RAW contract.");
    }
    if (!ContainsNodeId(section.lockedFoundationNodeIds, section.rawSourceNodeId) ||
        !ContainsNodeId(section.lockedFoundationNodeIds, section.rawDecodeNodeId)) {
        return fail("The managed RAW locked foundation metadata is incomplete.", true);
    }

    const EditorNodeGraph::Node* rawSource = graph.FindNode(section.rawSourceNodeId);
    const EditorNodeGraph::Node* rawDecode = graph.FindNode(section.rawDecodeNodeId);
    const EditorNodeGraph::Node* toneCurve = graph.FindNode(section.toneCurveNodeId);
    const EditorNodeGraph::Node* viewTransform = graph.FindNode(section.viewTransformNodeId);
    if (!rawSource || !rawDecode || !toneCurve || !viewTransform) {
        return fail("A required managed RAW node is missing.", true);
    }
    if (rawSource->kind != EditorNodeGraph::NodeKind::RawSource ||
        rawDecode->kind != EditorNodeGraph::NodeKind::RawDecode ||
        toneCurve->kind != EditorNodeGraph::NodeKind::Layer ||
        toneCurve->layerType != LayerType::ToneCurve ||
        viewTransform->kind != EditorNodeGraph::NodeKind::Layer ||
        viewTransform->layerType != LayerType::ViewTransform) {
        return fail("A managed RAW node no longer has the required type.");
    }
    if (!NonEmptySourceIdentityMatches(baseRecipe.source.sourcePath, ResolvedRawSourcePath(rawSource->rawSource)) ||
        !NonEmptySourceIdentityMatches(baseRecipe.source.sourcePath, rawSource->rawSource.metadata.sourcePath)) {
        return fail("The managed RAW source no longer matches the recipe source.");
    }
    if (!section.sourceRelativePathKey.empty() &&
        !baseRecipe.source.relativePathKey.empty() &&
        section.sourceRelativePathKey != baseRecipe.source.relativePathKey) {
        return fail("The managed RAW source relative key no longer matches the recipe source.");
    }
    if (!section.sourceFingerprint.empty() &&
        !baseRecipe.source.fingerprint.empty() &&
        section.sourceFingerprint != baseRecipe.source.fingerprint) {
        return fail("The managed RAW source fingerprint no longer matches the recipe source.");
    }

    if (!HasRequiredLink(graph, section.rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId,
            section.rawDecodeNodeId, EditorNodeGraph::kRawInputSocketId) ||
        !HasRequiredLink(graph, section.rawDecodeNodeId, EditorNodeGraph::kImageOutputSocketId,
            section.toneCurveNodeId, EditorNodeGraph::kImageInputSocketId) ||
        !HasRequiredLink(graph, section.toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId,
            section.viewTransformNodeId, EditorNodeGraph::kImageInputSocketId)) {
        return fail("The managed RAW chain no longer has the required source/decode/tone/view links.", true);
    }

    const std::unordered_set<int> managedIds(requiredOrder.begin(), requiredOrder.end());
    bool hasDownstreamOutput = false;
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (!graph.IsRenderLink(link)) {
            continue;
        }
        const bool fromManaged = managedIds.find(link.fromNodeId) != managedIds.end();
        const bool toManaged = managedIds.find(link.toNodeId) != managedIds.end();
        if (IsRequiredManagedLink(link, section)) {
            continue;
        }
        if (toManaged) {
            return fail("An unsupported graph connection now enters the managed RAW section.");
        }
        if (fromManaged) {
            if (link.fromNodeId == section.viewTransformNodeId &&
                link.fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
                hasDownstreamOutput = true;
                continue;
            }
            return fail("An unsupported graph connection now leaves a managed RAW internal stage.");
        }
    }

    if (!hasDownstreamOutput) {
        result.message = "The managed RAW section is structurally valid but has no downstream render consumer.";
    }

    std::string settingsReason;
    if (!RawDecodeSettingsUseOnlyManagedFields(rawDecode->rawDecode.settings, &settingsReason)) {
        return fail(settingsReason.empty()
            ? "RAW Decode settings cannot round-trip through the managed RAW recipe."
            : settingsReason);
    }

    result.recipe = BuildRecipeFromManagedRawGraph(graph, section, baseRecipe);
    std::string reason;
    if (!IsRecipeRepresentableAsManagedGraph(result.recipe, &reason)) {
        return fail(reason.empty()
            ? "Managed RAW graph parameters cannot round-trip through the RAW recipe."
            : reason);
    }

    result.valid = true;
    result.repairable = false;
    if (result.message.empty()) {
        result.message = "Managed RAW section is valid.";
    }
    return result;
}

ManagedRawRepairResult RepairManagedRawSectionGraph(
    EditorNodeGraph::Graph& graph,
    const ManagedRawSection& section,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe) {
    ManagedRawRepairResult result;
    result.validation = ValidateManagedRawSection(graph, section, baseRecipe);
    if (result.validation.valid) {
        result.repaired = true;
        result.message = "Managed RAW section is already valid.";
        return result;
    }
    if (!result.validation.repairable) {
        result.message = result.validation.message.empty()
            ? "The managed RAW section cannot be mechanically repaired."
            : result.validation.message;
        return result;
    }

    auto fail = [&](std::string message) {
        result.repaired = false;
        result.message = std::move(message);
        result.validation = ValidateManagedRawSection(graph, section, baseRecipe);
        return result;
    };

    if (section.schemaVersion != kManagedRawSectionSchemaVersion ||
        section.validatorVersion != kManagedRawValidatorVersion) {
        return fail("Repair cannot continue because the managed RAW metadata version is unsupported.");
    }

    const std::vector<int> requiredOrder = {
        section.rawSourceNodeId,
        section.rawDecodeNodeId,
        section.toneCurveNodeId,
        section.viewTransformNodeId
    };
    if (std::any_of(requiredOrder.begin(), requiredOrder.end(), [](int id) { return id <= 0; })) {
        return fail("Repair cannot continue because required managed RAW node ids are missing.");
    }
    if (section.orderedNodeIds != requiredOrder) {
        return fail("Repair cannot continue because the managed RAW node order metadata has changed.");
    }
    if (!ContainsNodeId(section.lockedFoundationNodeIds, section.rawSourceNodeId) ||
        !ContainsNodeId(section.lockedFoundationNodeIds, section.rawDecodeNodeId)) {
        return fail("Repair cannot continue because locked managed RAW foundation metadata is incomplete.");
    }

    const EditorNodeGraph::Node* rawSource = graph.FindNode(section.rawSourceNodeId);
    const EditorNodeGraph::Node* rawDecode = graph.FindNode(section.rawDecodeNodeId);
    const EditorNodeGraph::Node* toneCurve = graph.FindNode(section.toneCurveNodeId);
    const EditorNodeGraph::Node* viewTransform = graph.FindNode(section.viewTransformNodeId);
    if (!rawSource || !rawDecode || !toneCurve || !viewTransform) {
        return fail("Repair cannot continue because a required managed RAW node is missing.");
    }
    if (rawSource->kind != EditorNodeGraph::NodeKind::RawSource ||
        rawDecode->kind != EditorNodeGraph::NodeKind::RawDecode ||
        toneCurve->kind != EditorNodeGraph::NodeKind::Layer ||
        toneCurve->layerType != LayerType::ToneCurve ||
        viewTransform->kind != EditorNodeGraph::NodeKind::Layer ||
        viewTransform->layerType != LayerType::ViewTransform) {
        return fail("Repair cannot continue because a managed RAW node no longer has the required type.");
    }
    if (!NonEmptySourceIdentityMatches(baseRecipe.source.sourcePath, ResolvedRawSourcePath(rawSource->rawSource)) ||
        !NonEmptySourceIdentityMatches(baseRecipe.source.sourcePath, rawSource->rawSource.metadata.sourcePath)) {
        return fail("Repair cannot continue because the managed RAW source no longer matches the recipe source.");
    }
    if (!section.sourceRelativePathKey.empty() &&
        !baseRecipe.source.relativePathKey.empty() &&
        section.sourceRelativePathKey != baseRecipe.source.relativePathKey) {
        return fail("Repair cannot continue because the managed RAW relative source key changed.");
    }
    if (!section.sourceFingerprint.empty() &&
        !baseRecipe.source.fingerprint.empty() &&
        section.sourceFingerprint != baseRecipe.source.fingerprint) {
        return fail("Repair cannot continue because the managed RAW source fingerprint changed.");
    }

    const std::unordered_set<int> managedIds(requiredOrder.begin(), requiredOrder.end());
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (!graph.IsRenderLink(link) || IsRequiredManagedLink(link, section)) {
            continue;
        }
        const bool fromManaged = managedIds.find(link.fromNodeId) != managedIds.end();
        const bool toManaged = managedIds.find(link.toNodeId) != managedIds.end();
        if (toManaged) {
            return fail("Repair refused because an unsupported graph connection enters the managed RAW section.");
        }
        if (fromManaged &&
            !(link.fromNodeId == section.viewTransformNodeId &&
              link.fromSocketId == EditorNodeGraph::kImageOutputSocketId)) {
            return fail("Repair refused because a custom graph connection leaves an internal managed RAW stage.");
        }
    }

    std::string settingsReason;
    if (!RawDecodeSettingsUseOnlyManagedFields(rawDecode->rawDecode.settings, &settingsReason)) {
        return fail(settingsReason.empty()
            ? "Repair cannot continue because RAW Decode settings cannot round-trip through the managed recipe."
            : settingsReason);
    }
    const Stack::RawRecipe::RawDevelopmentRecipe recipeFromGraph =
        BuildRecipeFromManagedRawGraph(graph, section, baseRecipe);
    std::string recipeReason;
    if (!IsRecipeRepresentableAsManagedGraph(recipeFromGraph, &recipeReason)) {
        return fail(recipeReason.empty()
            ? "Repair cannot continue because managed RAW graph parameters cannot round-trip through the recipe."
            : recipeReason);
    }

    auto ensureLink = [&](int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
        if (graph.HasLink(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            return true;
        }
        std::string errorMessage;
        if (!graph.TryConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, &errorMessage)) {
            result.message = errorMessage.empty()
                ? "Repair failed while reconnecting the managed RAW chain."
                : errorMessage;
            return false;
        }
        result.changed = true;
        return true;
    };

    if (!ensureLink(section.rawSourceNodeId, EditorNodeGraph::kRawOutputSocketId,
            section.rawDecodeNodeId, EditorNodeGraph::kRawInputSocketId) ||
        !ensureLink(section.rawDecodeNodeId, EditorNodeGraph::kImageOutputSocketId,
            section.toneCurveNodeId, EditorNodeGraph::kImageInputSocketId) ||
        !ensureLink(section.toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId,
            section.viewTransformNodeId, EditorNodeGraph::kImageInputSocketId)) {
        result.validation = ValidateManagedRawSection(graph, section, baseRecipe);
        return result;
    }

    result.validation = ValidateManagedRawSection(graph, section, baseRecipe);
    result.repaired = result.validation.valid;
    if (result.repaired) {
        result.message = result.changed
            ? "Managed RAW chain links were repaired."
            : "Managed RAW section is valid.";
    } else if (result.message.empty()) {
        result.message = result.validation.message.empty()
            ? "Repair did not produce a valid managed RAW section."
            : result.validation.message;
    }
    return result;
}

ManagedRawGraphMutationWarning BuildManagedRawGraphConnectionWarning(
    const ManagedRawSection& section,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    if (IsRequiredManagedLink(
            EditorNodeGraph::Link{ fromNodeId, fromSocketId, toNodeId, toSocketId },
            section)) {
        return {};
    }

    if (IsManagedRawInternalStageInput(section, toNodeId, toSocketId)) {
        return MakeManagedRawGraphMutationWarning(
            "This connection changes the managed RAW chain.",
            "Continuing will replace an internal managed RAW input and switch this image to Custom Graph Mode. RAW tab editing will become read-only until the chain is repaired or re-adopted.");
    }

    if (IsManagedRawInternalStageOutput(section, fromNodeId, fromSocketId)) {
        return MakeManagedRawGraphMutationWarning(
            "This connection branches from an internal managed RAW stage.",
            "Continuing will customize the managed RAW chain and switch this image to Custom Graph Mode. RAW tab editing will become read-only until the chain is repaired or re-adopted.");
    }

    return {};
}

ManagedRawGraphMutationWarning BuildManagedRawGraphLinkRemovalWarning(
    const ManagedRawSection& section,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    if (IsRequiredManagedLink(
            EditorNodeGraph::Link{ fromNodeId, fromSocketId, toNodeId, toSocketId },
            section)) {
        return MakeManagedRawGraphMutationWarning(
            "This link is part of the managed RAW chain.",
            "Deleting it will break RAW tab editing for this image until the chain is repaired or re-adopted.");
    }

    if (IsManagedRawInternalStageInput(section, toNodeId, toSocketId) ||
        IsManagedRawInternalStageOutput(section, fromNodeId, fromSocketId)) {
        return MakeManagedRawGraphMutationWarning(
            "This link touches the managed RAW chain.",
            "Deleting it may change RAW graph ownership and switch this image to Custom Graph Mode. RAW tab editing will become read-only until the chain is repaired or re-adopted.");
    }

    return {};
}

ManagedRawGraphMutationWarning BuildManagedRawGraphNodeRemovalWarning(
    const ManagedRawSection& section,
    int nodeId) {
    if (!IsManagedRawNode(section, nodeId)) {
        return {};
    }

    return MakeManagedRawGraphMutationWarning(
        "This node is part of the managed RAW chain.",
        "Removing it will switch this image to Custom Graph Mode and make RAW tab editing read-only until the chain is repaired or re-adopted.");
}

bool TryBuildManagedRawSectionFromGraph(
    const EditorNodeGraph::Graph& graph,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe,
    ManagedRawSection& outSection,
    Stack::RawRecipe::RawDevelopmentRecipe& outRecipe,
    std::string* outReason) {
    for (const EditorNodeGraph::Node& rawSource : graph.GetNodes()) {
        if (rawSource.kind != EditorNodeGraph::NodeKind::RawSource) {
            continue;
        }
        for (const EditorNodeGraph::Link& rawLink : graph.GetLinks()) {
            if (rawLink.fromNodeId != rawSource.id ||
                rawLink.fromSocketId != EditorNodeGraph::kRawOutputSocketId ||
                rawLink.toSocketId != EditorNodeGraph::kRawInputSocketId) {
                continue;
            }
            const EditorNodeGraph::Node* rawDecode = graph.FindNode(rawLink.toNodeId);
            if (!rawDecode || rawDecode->kind != EditorNodeGraph::NodeKind::RawDecode) {
                continue;
            }
            const EditorNodeGraph::Link* toneLink =
                graph.FindOutputLink(rawDecode->id, EditorNodeGraph::kImageOutputSocketId);
            if (!toneLink || toneLink->toSocketId != EditorNodeGraph::kImageInputSocketId) {
                continue;
            }
            const EditorNodeGraph::Node* toneCurve = graph.FindNode(toneLink->toNodeId);
            if (!toneCurve ||
                toneCurve->kind != EditorNodeGraph::NodeKind::Layer ||
                toneCurve->layerType != LayerType::ToneCurve) {
                continue;
            }
            const EditorNodeGraph::Link* viewLink =
                graph.FindOutputLink(toneCurve->id, EditorNodeGraph::kImageOutputSocketId);
            if (!viewLink || viewLink->toSocketId != EditorNodeGraph::kImageInputSocketId) {
                continue;
            }
            const EditorNodeGraph::Node* viewTransform = graph.FindNode(viewLink->toNodeId);
            if (!viewTransform ||
                viewTransform->kind != EditorNodeGraph::NodeKind::Layer ||
                viewTransform->layerType != LayerType::ViewTransform) {
                continue;
            }

            ManagedRawSection section = BuildManagedRawSection(
                SectionIdForChain(baseRecipe, rawSource.id, rawDecode->id),
                baseRecipe.source.relativePathKey,
                baseRecipe.source.relativePathKey,
                baseRecipe.source.fingerprint,
                -1,
                rawSource.id,
                rawDecode->id,
                toneCurve->id,
                viewTransform->id);
            ManagedRawValidationResult validation =
                ValidateManagedRawSection(graph, section, baseRecipe);
            if (!validation.valid) {
                if (outReason) {
                    *outReason = validation.message;
                }
                continue;
            }
            outSection = std::move(section);
            outRecipe = std::move(validation.recipe);
            if (outReason) {
                outReason->clear();
            }
            return true;
        }
    }

    if (outReason && outReason->empty()) {
        *outReason = "No valid RAW Source -> RAW Decode -> Tone Curve -> View Transform chain was found.";
    }
    return false;
}

} // namespace Stack::RawWorkspace
