#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace Stack::Renderer {

// Costs include every rendition (for example SDR + HDR). Zero resident bytes
// means absent; a missing resource needs a nonzero expected cost to be admitted.
struct ViewportSectionResidencyInput {
    std::uint64_t nativeBytes = 0, previewBytes = 0;
    std::uint64_t expectedNativeBytes = 0, expectedPreviewBytes = 0;
    std::uint64_t lastUse = 0;
    bool visible = false, nearby = false, nativeNeeded = false;
};

struct ViewportSectionResidencyDecision {
    bool wantNative = false, wantPreview = false;
};

struct ViewportSectionResidencyPlan {
    std::vector<ViewportSectionResidencyDecision> sections;
    int nextVisibleRestore = -1;
    // Coverage describes resident resources that survive this plan, at the
    // requested display quality. Authoritative CPU/disk completion is separate.
    bool covered = true, budgetLimited = false;
    std::uint64_t expectedResidentBytes = 0;
};

// The caller supplies a total residency allowance, not remaining free VRAM.
// Plan the desired set before evicting or restoring anything: a visible section
// rejected by this plan must not be restored repeatedly just because it is absent.
inline ViewportSectionResidencyPlan PlanViewportSectionResidency(
    const std::vector<ViewportSectionResidencyInput>& inputs,
    std::uint64_t budget) {
    ViewportSectionResidencyPlan result;
    result.sections.resize(inputs.size());
    std::vector<std::size_t> order(inputs.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    const auto nativeCost = [&](std::size_t i) {
        return inputs[i].nativeBytes ? inputs[i].nativeBytes : inputs[i].expectedNativeBytes;
    };
    const auto previewCost = [&](std::size_t i) {
        return inputs[i].previewBytes ? inputs[i].previewBytes : inputs[i].expectedPreviewBytes;
    };
    const auto recentFirst = [&](std::size_t a, std::size_t b) {
        return inputs[a].lastUse != inputs[b].lastUse ?
            inputs[a].lastUse > inputs[b].lastUse : a < b;
    };
    const auto admit = [&](bool& wanted, std::uint64_t bytes) {
        if (wanted) return true;
        if (!bytes || bytes > budget - result.expectedResidentBytes) return false;
        wanted = true;
        result.expectedResidentBytes += bytes;
        return true;
    };

    // Give every affordable visible section a baseline before spending on
    // native detail. Keep existing coverage ahead of missing display copies.
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (bool(inputs[a].previewBytes) != bool(inputs[b].previewBytes))
            return bool(inputs[a].previewBytes);
        return recentFirst(a, b);
    });
    for (const auto i : order)
        if (inputs[i].visible) admit(result.sections[i].wantPreview, previewCost(i));

    // Retain visible native detail before restoring more. A native texture
    // covers its own preview, so redundant previews are optional rather than
    // preventing otherwise affordable native coverage.
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (bool(inputs[a].nativeBytes) != bool(inputs[b].nativeBytes))
            return bool(inputs[a].nativeBytes);
        return recentFirst(a, b);
    });
    for (const auto i : order) {
        auto& wanted = result.sections[i];
        if (!inputs[i].visible || (!inputs[i].nativeNeeded && wanted.wantPreview)) continue;
        const auto replaced = wanted.wantPreview ? previewCost(i) : 0;
        const auto withoutPreview = result.expectedResidentBytes - replaced;
        const auto bytes = nativeCost(i);
        if (bytes && bytes <= budget - withoutPreview) {
            wanted.wantNative = true;
            wanted.wantPreview = false;
            result.expectedResidentBytes = withoutPreview + bytes;
        }
    }

    // Preserve cheap fallback copies and additional visible detail only in
    // spare space. Do not create a missing, redundant rendition for this cache.
    for (const auto i : order) if (inputs[i].visible) {
        admit(result.sections[i].wantPreview, inputs[i].previewBytes);
        admit(result.sections[i].wantNative, inputs[i].nativeBytes);
    }
    // Recent offscreen pixels help pan reversals, but never displace visible
    // coverage. Nearby entries win ties in usefulness; all additions are already
    // resident, so this policy never initiates speculative uploads.
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (inputs[a].nearby != inputs[b].nearby) return inputs[a].nearby;
        return recentFirst(a, b);
    });
    for (const auto i : order) if (!inputs[i].visible) {
        admit(result.sections[i].wantPreview, inputs[i].previewBytes);
        admit(result.sections[i].wantNative, inputs[i].nativeBytes);
    }

    // Fill missing coverage before upgrading an already displayed preview.
    // Rejected resources never enter either restore queue, which makes a
    // settled, over-budget view stop instead of evicting/restoring forever.
    int nextUpgrade = -1;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (!inputs[i].visible) continue;
        const auto& wanted = result.sections[i];
        const bool native = wanted.wantNative && inputs[i].nativeBytes != 0;
        const bool preview = wanted.wantPreview && inputs[i].previewBytes != 0;
        result.covered &= native || (!inputs[i].nativeNeeded && preview);
        result.budgetLimited |= inputs[i].nativeNeeded ? !wanted.wantNative :
            !wanted.wantNative && !wanted.wantPreview;
        if (!native && !preview && (wanted.wantNative || wanted.wantPreview)) {
            if (result.nextVisibleRestore < 0) result.nextVisibleRestore = static_cast<int>(i);
        } else if (inputs[i].nativeNeeded && wanted.wantNative && !native && nextUpgrade < 0) {
            nextUpgrade = static_cast<int>(i);
        }
    }
    if (result.nextVisibleRestore < 0) result.nextVisibleRestore = nextUpgrade;
    return result;
}

} // namespace Stack::Renderer
