#include "Raw/RawGallerySimilarity.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Raw gallery similarity test failed: " << message << '\n';
        return false;
    }
    return true;
}

std::vector<unsigned char> MakeScene(
    int width,
    int height,
    bool horizontal,
    float scale,
    float offset,
    bool tint) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) * 4u,
        255u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool first = horizontal ? y < height / 2 : x < width / 2;
            const bool inset =
                x > width / 4 && x < width * 3 / 4 &&
                y > height / 4 && y < height * 3 / 4;
            const float base = first ? 0.18f : 0.72f;
            const float value = std::clamp(
                (base + (inset ? 0.16f : 0.0f)) * scale + offset,
                0.0f,
                1.0f);
            const std::size_t index =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            pixels[index] = static_cast<unsigned char>(
                std::lround(value * (tint ? 230.0f : 255.0f)));
            pixels[index + 1u] = static_cast<unsigned char>(
                std::lround(value * 255.0f));
            pixels[index + 2u] = static_cast<unsigned char>(
                std::lround(value * (tint ? 205.0f : 255.0f)));
        }
    }
    return pixels;
}

Stack::RawWorkspace::RawGallerySimilarityDescriptor Describe(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    return Stack::RawWorkspace::BuildRawGallerySimilarityDescriptor(
        pixels.data(), width, height);
}

Stack::RawWorkspace::RawGallerySimilarityInput Input(
    std::string key,
    std::string folder,
    std::size_t index,
    Stack::RawWorkspace::RawGallerySimilarityDescriptor descriptor) {
    Stack::RawWorkspace::RawGallerySimilarityInput input;
    input.sourceKey = std::move(key);
    input.folderKey = std::move(folder);
    input.catalogIndex = index;
    input.descriptor = std::move(descriptor);
    return input;
}

bool TestDescriptorAndGrouping() {
    constexpr int width = 96;
    constexpr int height = 72;
    const auto original = MakeScene(width, height, false, 1.0f, 0.0f, false);
    const auto exposureAndTint =
        MakeScene(width, height, false, 0.63f, 0.13f, true);
    const auto differentStructure =
        MakeScene(width, height, true, 1.0f, 0.0f, false);
    const auto first = Describe(original, width, height);
    const auto second = Describe(exposureAndTint, width, height);
    const auto different = Describe(differentStructure, width, height);

    if (!Check(first.IsValid() && second.IsValid() && different.IsValid(),
               "descriptor construction")) return false;

    auto stacks = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input("a", "shoot", 0, first),
        Input("b", "shoot", 1, second),
        Input("c", "shoot", 2, different)
    });
    if (!Check(stacks.size() == 2u, "same scene groups, different structure does not")) {
        return false;
    }
    if (!Check(stacks[0].sourceKeys == std::vector<std::string>({ "a", "b" }),
               "exposure and tint invariance")) return false;

    stacks = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input("a", "one", 0, first),
        Input("b", "two", 1, second)
    });
    if (!Check(stacks.size() == 2u, "folder boundary")) return false;

    auto incompatibleAspect = second;
    incompatibleAspect.aspectRatio = first.aspectRatio * 1.2f;
    stacks = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input("a", "shoot", 0, first),
        Input("b", "shoot", 1, incompatibleAspect)
    });
    if (!Check(stacks.size() == 2u, "aspect ratio gate")) return false;

    std::vector<unsigned char> flat(
        static_cast<std::size_t>(width) * height * 4u,
        127u);
    for (std::size_t index = 3u; index < flat.size(); index += 4u) {
        flat[index] = 255u;
    }
    const auto flatDescriptor = Describe(flat, width, height);
    stacks = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input("flat-a", "shoot", 0, flatDescriptor),
        Input("flat-b", "shoot", 1, flatDescriptor)
    });
    if (!Check(stacks.size() == 2u, "flat images remain singletons")) {
        return false;
    }

    auto captureSequenceVariation = first;
    captureSequenceVariation.perceptualHash = ~first.perceptualHash;
    stacks = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input(
            "IMG_260915_200117_589_001.dng",
            "shoot",
            0,
            first),
        Input(
            "IMG_260915_200122_340_003.dng",
            "shoot",
            1,
            captureSequenceVariation)
    });
    if (!Check(
            stacks.size() == 1u,
            "short camera capture sequence groups composition changes")) {
        return false;
    }

    stacks = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input(
            "IMG_260915_200117_589_001.dng",
            "shoot",
            0,
            first),
        Input(
            "IMG_260915_200735_236_015.dng",
            "shoot",
            1,
            captureSequenceVariation)
    });
    return Check(
        stacks.size() == 2u,
        "capture sequence time gap separates different shoots");
}

bool TestPersistenceAndDeterminism() {
    const auto pixels = MakeScene(80, 64, false, 1.0f, 0.0f, false);
    const auto descriptor = Describe(pixels, 80, 64);
    const nlohmann::json serialized =
        Stack::RawWorkspace::SerializeRawGallerySimilarityDescriptor(
            descriptor);
    Stack::RawWorkspace::RawGallerySimilarityDescriptor restored;
    if (!Check(
            Stack::RawWorkspace::DeserializeRawGallerySimilarityDescriptor(
                serialized,
                restored),
            "descriptor round trip parses")) return false;
    if (!Check(
            restored.perceptualHash == descriptor.perceptualHash &&
            restored.normalizedLuminance == descriptor.normalizedLuminance &&
            restored.gradientOrientation == descriptor.gradientOrientation,
            "descriptor round trip retains data")) return false;

    nlohmann::json stale = serialized;
    stale["version"] = 999;
    if (!Check(
            !Stack::RawWorkspace::DeserializeRawGallerySimilarityDescriptor(
                stale,
                restored),
            "unknown descriptor version rejected")) return false;

    const auto first = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input("c", "shoot", 2, descriptor),
        Input("a", "shoot", 0, descriptor),
        Input("b", "shoot", 1, descriptor)
    });
    const auto second = Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
        Input("b", "shoot", 1, descriptor),
        Input("c", "shoot", 2, descriptor),
        Input("a", "shoot", 0, descriptor)
    });
    if (!Check(
        first.size() == 1u && second.size() == 1u &&
        first[0].sourceKeys == second[0].sourceKeys &&
        first[0].sourceKeys == std::vector<std::string>({ "a", "b", "c" }),
        "grouping is independent of descriptor arrival order")) return false;

    Stack::RawWorkspace::RawGallerySimilarityRequest request;
    request.generation = 7;
    request.workspaceKey = "workspace-a";
    request.inputs.push_back(Input("a", "shoot", 0, descriptor));
    const auto result =
        Stack::RawWorkspace::BuildRawGallerySimilarityResult(request);
    if (!Check(
            Stack::RawWorkspace::IsRawGallerySimilarityResultCurrent(
                result, 7, "workspace-a"),
            "matching generation and workspace accepted")) return false;
    if (!Check(
            !Stack::RawWorkspace::IsRawGallerySimilarityResultCurrent(
                result, 8, "workspace-a"),
            "stale generation rejected")) return false;
    if (!Check(
            !Stack::RawWorkspace::IsRawGallerySimilarityResultCurrent(
                result, 7, "workspace-b"),
            "stale workspace rejected")) return false;

    Stack::RawWorkspace::RawGallerySimilarityDescriptor invalid;
    const auto fallback =
        Stack::RawWorkspace::BuildRawGallerySimilarityStacks({
            Input("invalid-a", "shoot", 0, invalid),
            Input("invalid-b", "shoot", 1, invalid)
        });
    return Check(
        fallback.size() == 2u,
        "failed analysis falls back to singleton cards");
}

bool TestManualGrouping() {
    using Stack::RawWorkspace::ApplyRawGalleryManualGrouping;
    using Stack::RawWorkspace::DetachRawGalleryManualStackMember;
    using Stack::RawWorkspace::MergeRawGalleryManualStack;
    using Stack::RawWorkspace::RawGalleryManualGrouping;
    using Stack::RawWorkspace::RawGallerySimilarityStack;

    std::vector<Stack::RawWorkspace::RawGallerySimilarityInput> catalog;
    catalog.push_back(Input("a", "shoot", 0, {}));
    catalog.push_back(Input("b", "shoot", 1, {}));
    catalog.push_back(Input("c", "shoot", 2, {}));
    catalog.push_back(Input("d", "shoot", 3, {}));
    const std::vector<RawGallerySimilarityStack> automatic {
        { "shoot", 0, { "a", "b" } },
        { "shoot", 2, { "c" } },
        { "shoot", 3, { "d" } }
    };

    RawGalleryManualGrouping grouping;
    if (!Check(
            MergeRawGalleryManualStack(grouping, "d", { "a", "b" }),
            "dropping onto a stack creates a manual merge")) return false;
    auto resolved = ApplyRawGalleryManualGrouping(
        automatic, catalog, grouping);
    if (!Check(
            resolved.size() == 2u &&
            resolved[0].sourceKeys ==
                std::vector<std::string>({ "a", "b", "d" }) &&
            resolved[1].sourceKeys == std::vector<std::string>({ "c" }),
            "manual merge overrides automatic singleton placement")) {
        return false;
    }

    const nlohmann::json encoded =
        Stack::RawWorkspace::SerializeRawGalleryManualGrouping(grouping);
    RawGalleryManualGrouping restored;
    if (!Check(
            Stack::RawWorkspace::DeserializeRawGalleryManualGrouping(
                encoded, restored) && restored.stacks == grouping.stacks,
            "manual grouping persists losslessly")) return false;
    nlohmann::json unsupported = encoded;
    unsupported["version"] = 999;
    if (!Check(
            !Stack::RawWorkspace::DeserializeRawGalleryManualGrouping(
                unsupported, restored),
            "unsupported manual grouping version is ignored")) return false;

    if (!Check(
            DetachRawGalleryManualStackMember(grouping, "b"),
            "blank-space drop detaches a member")) return false;
    resolved = ApplyRawGalleryManualGrouping(automatic, catalog, grouping);
    if (!Check(
            resolved.size() == 3u &&
            resolved[0].sourceKeys ==
                std::vector<std::string>({ "a", "d" }) &&
            resolved[1].sourceKeys == std::vector<std::string>({ "b" }),
            "detached member remains a forced singleton")) return false;

    if (!Check(
            DetachRawGalleryManualStackMember(grouping, "a"),
            "detaching from a two-card manual stack succeeds")) return false;
    resolved = ApplyRawGalleryManualGrouping(automatic, catalog, grouping);
    return Check(
        resolved.size() == 4u &&
        std::all_of(
            resolved.begin(), resolved.end(), [](const auto& stack) {
                return stack.sourceKeys.size() == 1u;
            }),
        "a split two-card stack leaves two independent cards");
}

bool TestPresentationHelpers() {
    const std::vector<std::string> members { "a", "b", "c" };
    const std::string selectedCover =
        Stack::RawWorkspace::ResolveRawGalleryStackCover(
            members,
            "c");
    if (!Check(selectedCover == "c", "selected card becomes the cover")) {
        return false;
    }
    const std::string fallbackCover =
        Stack::RawWorkspace::ResolveRawGalleryStackCover(
            members,
            "missing");
    if (!Check(fallbackCover == "a", "earliest card is the fallback cover")) {
        return false;
    }
    const auto selected = Stack::RawWorkspace::BuildRawGalleryExpansionOrder(
        members,
        "c");
    if (!Check(
            selected == std::vector<std::string>({ "c", "a", "b" }),
            "the opening cover begins the frozen expansion order")) return false;
    const auto frozenAfterSelection = selected;
    if (!Check(
            frozenAfterSelection ==
                std::vector<std::string>({ "c", "a", "b" }),
            "selection changes do not rebuild an open stack")) return false;
    const auto unselected = Stack::RawWorkspace::BuildRawGalleryExpansionOrder(
        members,
        "missing");
    if (!Check(unselected == members, "missing cover preserves catalog order")) {
        return false;
    }

    const float drawerHeight =
        Stack::RawWorkspace::ComputeRawGalleryFilmstripDrawerTargetHeight(
            180.0f, 100.0f, 10.0f, 3u, 1000.0f);
    if (!Check(
            std::abs(drawerHeight - 400.0f) < 0.001f,
            "drawer fits the tallest stack")) return false;
    const float cappedDrawerHeight =
        Stack::RawWorkspace::ComputeRawGalleryFilmstripDrawerTargetHeight(
            180.0f, 100.0f, 10.0f, 8u, 700.0f);
    if (!Check(
            std::abs(cappedDrawerHeight - 350.0f) < 0.001f,
            "drawer stops at half the workspace")) return false;
    const float singletonDrawerHeight =
        Stack::RawWorkspace::ComputeRawGalleryFilmstripDrawerTargetHeight(
            180.0f, 100.0f, 10.0f, 1u, 1000.0f);
    if (!Check(
            std::abs(singletonDrawerHeight - 290.0f) < 0.001f,
            "singleton filmstrips still open by one tile interval")) return false;

    const auto natural =
        Stack::RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
            3u, 100.0f, 10.0f, 500.0f, 100.0f);
    if (!Check(
            std::abs(natural.cardStep - 110.0f) < 0.001f &&
            std::abs(natural.top - 280.0f) < 0.001f,
            "unconstrained cards use full spacing")) return false;
    const auto compressed =
        Stack::RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
            3u, 100.0f, 10.0f, 500.0f, 400.0f);
    if (!Check(
        std::abs(compressed.cardStep - 50.0f) < 0.001f &&
        std::abs(compressed.top - 400.0f) < 0.001f,
        "tall stack compresses at the drawer boundary")) return false;
    const auto collapsed =
        Stack::RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
            3u, 100.0f, 10.0f, 500.0f, 100.0f, 0.0f);
    if (!Check(
            std::abs(collapsed.cardStep) < 0.001f &&
            std::abs(collapsed.top - 500.0f) < 0.001f,
            "closed drawer fully collapses every card onto its cover")) {
        return false;
    }
    const auto halfExpanded =
        Stack::RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
            3u, 100.0f, 10.0f, 500.0f, 100.0f, 0.5f);
    if (!Check(
        std::abs(halfExpanded.cardStep - 55.0f) < 0.001f &&
        std::abs(halfExpanded.top - 390.0f) < 0.001f,
        "drawer progress continuously interpolates stack movement")) {
        return false;
    }
    return Check(
        Stack::RawWorkspace::IsRawGalleryFilmstripExpansionOrderCurrent(
            { "a", "b", "c" },
            { "c", "a", "b" }) &&
            !Stack::RawWorkspace::IsRawGalleryFilmstripExpansionOrderCurrent(
                { "a", "b", "c" },
                { "a" }),
        "a drawer rebuilds its order when asynchronous grouping adds members");
}

bool TestFilmstripOrganization() {
    using Stack::RawWorkspace::RawGalleryFilmstripSortMode;
    using Stack::RawWorkspace::RawGalleryFilmstripSourceSortInfo;
    using Stack::RawWorkspace::RawGalleryManualGrouping;
    using Stack::RawWorkspace::RawGallerySimilarityStack;

    const std::vector<RawGalleryFilmstripSourceSortInfo> sources {
        { "a", "zebra.dng", "two", 20, 0 },
        { "b", "behind.dng", "two", 21, 1 },
        { "c", "apple.dng", "one", 30, 2 },
        { "d", "middle.dng", "one", 10, 3 }
    };
    const std::vector<RawGallerySimilarityStack> stacks {
        { "two", 0, { "a", "b" } },
        { "one", 2, { "c" } },
        { "one", 3, { "d" } }
    };
    const auto anchors = [](const auto& orderedStacks) {
        std::vector<std::string> result;
        for (const auto& stack : orderedStacks) {
            result.push_back(stack.sourceKeys.front());
        }
        return result;
    };

    RawGalleryManualGrouping organization;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, sources, organization)) ==
                std::vector<std::string>({ "d", "a", "c" }),
            "default timeline sorts by time")) return false;
    auto captureSources = sources;
    captureSources[0].captureTimestamp = 100;
    captureSources[1].captureTimestamp = 101;
    captureSources[2].captureTimestamp = 300;
    captureSources[3].modifiedUnixSeconds = 200;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, captureSources, organization)) ==
                std::vector<std::string>({ "a", "d", "c" }),
            "timeline uses capture metadata and falls back to file modification time")) return false;
    organization.sortMode = RawGalleryFilmstripSortMode::TimelineByFolder;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, captureSources, organization)) ==
                std::vector<std::string>({ "d", "c", "a" }),
            "folder timeline keeps folders together and sorts their capture times")) return false;
    organization.sortMode = RawGalleryFilmstripSortMode::FileNameAscending;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, sources, organization)) ==
                std::vector<std::string>({ "c", "d", "a" }),
            "filename ascending sort")) return false;
    organization.sortMode = RawGalleryFilmstripSortMode::FileNameDescending;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, sources, organization)) ==
                std::vector<std::string>({ "a", "d", "c" }),
            "filename descending uses the stable stack anchor")) return false;
    organization.sortMode = RawGalleryFilmstripSortMode::ModifiedNewestFirst;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, sources, organization)) ==
                std::vector<std::string>({ "c", "a", "d" }),
            "modified newest sort")) return false;
    organization.sortMode = RawGalleryFilmstripSortMode::ModifiedOldestFirst;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, sources, organization)) ==
                std::vector<std::string>({ "d", "a", "c" }),
            "modified oldest sort")) return false;
    organization.sortMode =
        RawGalleryFilmstripSortMode::FolderThenNameDescending;
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, sources, organization)) ==
                std::vector<std::string>({ "a", "d", "c" }),
            "folder descending sort")) return false;
    organization.sortMode = RawGalleryFilmstripSortMode::Manual;
    organization.manualSourceOrder = { "d", "a", "b", "c" };
    if (!Check(
            anchors(Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
                stacks, sources, organization)) ==
                std::vector<std::string>({ "d", "a", "c" }),
            "manual sort follows persisted source ranks")) return false;

    const std::vector<std::string> catalog { "a", "b", "c", "d" };
    if (!Check(
            Stack::RawWorkspace::NormalizeRawGalleryManualSourceOrder(
                { "b", "missing", "b", "a" }, catalog) ==
                std::vector<std::string>({ "b", "a", "c", "d" }),
            "manual order drops missing keys and appends new sources")) {
        return false;
    }
    if (!Check(
            Stack::RawWorkspace::SeedRawGalleryManualSourceOrder(stacks) ==
                std::vector<std::string>({ "a", "b", "c", "d" }),
            "manual order seeds from visible stack order")) return false;
    if (!Check(
            Stack::RawWorkspace::ReorderRawGalleryManualSourceOrder(
                stacks, { "b" }, 3u, catalog) ==
                std::vector<std::string>({ "a", "c", "d", "b" }),
            "moving one stack member preserves the remaining stack")) {
        return false;
    }
    if (!Check(
            Stack::RawWorkspace::ReorderRawGalleryManualSourceOrder(
                stacks, { "a", "b" }, 3u, catalog) ==
                std::vector<std::string>({ "c", "d", "a", "b" }),
            "moving an entire stack keeps its members contiguous")) {
        return false;
    }

    RawGalleryManualGrouping mergeGrouping;
    if (!Check(
            Stack::RawWorkspace::MergeRawGalleryManualStackMembers(
                mergeGrouping, { "c", "d" }, { "a", "b" }) &&
            mergeGrouping.stacks.size() == 1u &&
            mergeGrouping.stacks.front() ==
                std::vector<std::string>({ "a", "b", "c", "d" }),
            "whole-stack merge appends members in stable order")) return false;

    const nlohmann::json encoded =
        Stack::RawWorkspace::SerializeRawGalleryManualGrouping(organization);
    RawGalleryManualGrouping decoded;
    if (!Check(
            Stack::RawWorkspace::DeserializeRawGalleryManualGrouping(
                encoded, decoded) &&
            decoded.sortMode == RawGalleryFilmstripSortMode::Manual &&
            decoded.manualSourceOrder == organization.manualSourceOrder,
            "filmstrip sort and manual order round trip")) return false;
    const nlohmann::json legacy {
        { "version", 1 },
        { "stacks", nlohmann::json::array({
            nlohmann::json::array({ "a", "b" }) }) },
        { "detached", nlohmann::json::array({ "c" }) }
    };
    if (!Check(
            Stack::RawWorkspace::DeserializeRawGalleryManualGrouping(
                legacy, decoded) &&
            decoded.stacks ==
                std::vector<std::vector<std::string>>({{ "a", "b" }}) &&
            decoded.sortMode ==
                RawGalleryFilmstripSortMode::TimelineAll,
            "legacy grouping migrates to the default filmstrip sort")) {
        return false;
    }

    if (!Check(
            Stack::RawWorkspace::ResolveRawGalleryFilmstripInsertionIndex(
                149.0f, 100.0f, 100.0f, 3u) == 0u &&
            Stack::RawWorkspace::ResolveRawGalleryFilmstripInsertionIndex(
                151.0f, 100.0f, 100.0f, 3u) == 1u &&
            Stack::RawWorkspace::ResolveRawGalleryFilmstripInsertionIndex(
                900.0f, 100.0f, 100.0f, 3u) == 3u,
            "insertion slots resolve at card midpoints")) return false;
    if (!Check(
            std::abs(
                Stack::RawWorkspace::ComputeRawGalleryFilmstripAutoScrollVelocity(
                    100.0f, 100.0f, 500.0f, 40.0f, 800.0f) +
                800.0f) < 0.001f &&
            std::abs(
                Stack::RawWorkspace::ComputeRawGalleryFilmstripAutoScrollVelocity(
                    300.0f, 100.0f, 500.0f, 40.0f, 800.0f)) < 0.001f &&
            std::abs(
                Stack::RawWorkspace::ComputeRawGalleryFilmstripAutoScrollVelocity(
                    500.0f, 100.0f, 500.0f, 40.0f, 800.0f) -
                800.0f) < 0.001f,
            "edge auto-scroll is directional and bounded")) return false;
    return Check(
        std::abs(Stack::RawWorkspace::EaseRawGalleryFilmstripDrag(0.5f) -
                 0.875f) < 0.001f,
        "drag motion uses deterministic cubic settling");
}

bool TestFilmstripDrawerState() {
    using Stack::RawWorkspace::RawGalleryFilmstripDrawerInput;
    using Stack::RawWorkspace::RawGalleryFilmstripDrawerState;
    using Stack::RawWorkspace::UpdateRawGalleryFilmstripDrawerState;

    RawGalleryFilmstripDrawerState state;
    RawGalleryFilmstripDrawerInput input;
    input.enabled = true;
    input.pointerInside = true;
    input.now = 1.0;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    input.now = 1.399;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(!state.open, "drawer remains closed before hover delay")) {
        return false;
    }
    input.now = 1.4;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(state.open, "drawer opens at the hover delay")) return false;

    input.pointerInside = false;
    input.now = 2.0;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    input.now = 2.099;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(state.open, "drawer remains open during leave grace")) {
        return false;
    }
    input.pointerInside = true;
    input.now = 2.1;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(
            state.open && state.leaveStartedAt < 0.0,
            "returning during grace cancels retraction")) return false;

    input.pointerInside = false;
    input.interactionRetained = true;
    input.now = 2.5;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(
            state.open && state.leaveStartedAt < 0.0,
            "an active drawer interaction retains the drawer")) return false;
    input.interactionRetained = false;

    input.pointerInside = false;
    input.now = 3.0;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    input.now = 3.1;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(!state.open, "drawer closes after leave grace")) return false;

    input.keyboardToggle = true;
    input.now = 4.0;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(
            state.open && state.keyboardPinned,
            "keyboard opens and pins the drawer")) return false;
    input.keyboardToggle = false;
    input.now = 5.0;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(state.open, "keyboard pin ignores pointer leave")) return false;

    input.pointerInside = true;
    input.keyboardToggle = true;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(
            !state.open && state.suppressHoverUntilPointerExit,
            "keyboard close suppresses immediate hover reopening")) return false;
    input.keyboardToggle = false;
    input.now = 6.0;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(!state.open, "suppressed pointer does not restart dwell")) {
        return false;
    }
    input.pointerInside = false;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    if (!Check(
        !state.suppressHoverUntilPointerExit,
        "pointer exit clears hover suppression")) return false;
    input.enabled = false;
    state = UpdateRawGalleryFilmstripDrawerState(state, input);
    return Check(
        !state.open && !state.keyboardPinned &&
            state.hoverStartedAt < 0.0 && state.leaveStartedAt < 0.0,
        "disabling the filmstrip clears transient drawer state");
}

bool TestGalleryRestorationPolicy() {
    using Stack::RawWorkspace::ShouldRestoreRawGalleryForEmptyWorkspace;
    if (!Check(
            ShouldRestoreRawGalleryForEmptyWorkspace(
                true, false, false, false, false, true),
            "empty Raw entry restores Gallery")) return false;
    if (!Check(
            !ShouldRestoreRawGalleryForEmptyWorkspace(
                false, false, false, false, false, true),
            "missing workspace suppresses restore")) return false;
    if (!Check(
            !ShouldRestoreRawGalleryForEmptyWorkspace(
                true, true, false, false, false, true),
            "locked workspace suppresses restore")) return false;
    if (!Check(
            !ShouldRestoreRawGalleryForEmptyWorkspace(
                true, false, true, false, false, true),
            "active project suppresses restore")) return false;
    if (!Check(
            !ShouldRestoreRawGalleryForEmptyWorkspace(
                true, false, false, false, true, true),
            "explicit open suppresses restore")) return false;
    return Check(
        !ShouldRestoreRawGalleryForEmptyWorkspace(
            true, false, false, false, false, false),
        "already-open Gallery is unchanged");
}

} // namespace

int main() {
    if (!TestDescriptorAndGrouping() ||
        !TestPersistenceAndDeterminism() ||
        !TestManualGrouping() ||
        !TestFilmstripOrganization() ||
        !TestPresentationHelpers() ||
        !TestFilmstripDrawerState() ||
        !TestGalleryRestorationPolicy()) {
        return 1;
    }
    std::cout << "Raw gallery similarity tests passed.\n";
    return 0;
}
