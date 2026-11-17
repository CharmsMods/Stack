#include "Renderer/ViewportSectionResidency.h"
#include "Editor/ViewportSectionDrawing.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace Stack::Renderer;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

ViewportSectionResidencyInput Section(bool visible, bool nativeNeeded,
    std::uint64_t nativeBytes = 0, std::uint64_t previewBytes = 0,
    std::uint64_t lastUse = 0) {
    return {nativeBytes, previewBytes, 32, 2, lastUse, visible, false, nativeNeeded};
}

void TestVisibleBudgetAndStableRestoration() {
    std::vector<ViewportSectionResidencyInput> sections{
        Section(true, true, 32, 2, 2), Section(true, true, 32, 2, 1),
        Section(true, true, 0, 2)};
    auto plan = PlanViewportSectionResidency(sections, 38);
    Require(plan.expectedResidentBytes == 38 && plan.sections[0].wantNative &&
        !plan.sections[1].wantNative && !plan.sections[2].wantNative,
        "Visible native sections must not pin more than the complete byte allowance");
    Require(plan.budgetLimited && !plan.covered && plan.nextVisibleRestore < 0,
        "Unadmitted visible native sections must not cause restore/evict churn");
    sections[1].nativeBytes = 0; // Apply the planned eviction.
    const auto settled = PlanViewportSectionResidency(sections, 38);
    Require(settled.expectedResidentBytes == plan.expectedResidentBytes &&
        settled.sections[0].wantNative && !settled.sections[1].wantNative &&
        settled.nextVisibleRestore < 0,
        "Applying an over-budget plan must leave the same stable desired set");

    plan = PlanViewportSectionResidency(sections, 102);
    Require(!plan.budgetLimited && plan.nextVisibleRestore == 1 && !plan.covered,
        "More memory should admit missing visible native detail");
    sections[1].nativeBytes = 32;
    sections[2].nativeBytes = 32;
    plan = PlanViewportSectionResidency(sections, 102);
    Require(plan.covered && !plan.budgetLimited && plan.nextVisibleRestore < 0 &&
        plan.expectedResidentBytes == 102,
        "Fully restored native and preview pairs must settle within the allowance");
}

void TestBaselineAndNativeReplacement() {
    std::vector<ViewportSectionResidencyInput> sections{
        Section(true, true, 32, 2, 10), Section(true, false)};
    auto plan = PlanViewportSectionResidency(sections, 32);
    Require(plan.sections[0].wantPreview && plan.sections[1].wantPreview &&
        !plan.sections[0].wantNative && plan.nextVisibleRestore == 1,
        "Visible baseline coverage must precede an unaffordable native upgrade");
    sections.resize(1);
    plan = PlanViewportSectionResidency(sections, 32);
    Require(plan.sections[0].wantNative && !plan.sections[0].wantPreview &&
        plan.expectedResidentBytes == 32 && plan.covered,
        "A redundant preview must not prevent retaining native detail that fits alone");

    sections = {Section(true, true), Section(true, true)};
    plan = PlanViewportSectionResidency(sections, 64);
    Require(plan.sections[0].wantNative && plan.sections[1].wantNative &&
        !plan.sections[0].wantPreview && !plan.sections[1].wantPreview,
        "Missing native resources must replace their reserved preview costs");
    sections[0].nativeBytes = 32;
    sections[0].previewBytes = 2; // A worker may produce both renditions.
    plan = PlanViewportSectionResidency(sections, 64);
    Require(plan.sections[0].wantNative && plan.sections[1].wantNative &&
        plan.nextVisibleRestore == 1 && plan.expectedResidentBytes == 64,
        "An extra completed preview must not displace an already admitted native upgrade");
}

void TestRestorePriorityAndNoPrefetch() {
    std::vector<ViewportSectionResidencyInput> sections{
        Section(true, true, 0, 2), Section(true, false), Section(false, true)};
    sections[2].nearby = true;
    auto plan = PlanViewportSectionResidency(sections, 100);
    Require(plan.nextVisibleRestore == 1 && !plan.sections[2].wantNative &&
        !plan.sections[2].wantPreview,
        "Missing visible coverage must precede upgrades and speculative nearby work");
    sections[1].previewBytes = 2;
    plan = PlanViewportSectionResidency(sections, 100);
    Require(plan.nextVisibleRestore == 0,
        "An admitted native upgrade should run after visible baseline coverage exists");
}

void TestResidentPreferenceAndOffscreenLru() {
    std::vector<ViewportSectionResidencyInput> sections{
        Section(true, true, 0, 2, 100), Section(true, true, 32, 2, 1)};
    auto plan = PlanViewportSectionResidency(sections, 36);
    Require(!plan.sections[0].wantNative && plan.sections[1].wantNative &&
        plan.nextVisibleRestore < 0,
        "Existing visible native detail must win over an equally costly missing upgrade");

    sections = {Section(true, false, 0, 2), Section(false, false, 32, 2, 9),
        Section(false, false, 32, 2, 8), Section(false, false, 32, 2, 10)};
    plan = PlanViewportSectionResidency(sections, 36);
    Require(plan.sections[3].wantNative && plan.sections[3].wantPreview &&
        !plan.sections[1].wantNative && !plan.sections[2].wantPreview && plan.covered,
        "Spare residency should retain the most recently used offscreen section");
    sections[1].nearby = true;
    plan = PlanViewportSectionResidency(sections, 36);
    Require(plan.sections[1].wantNative && !plan.sections[3].wantNative,
        "Already resident nearby detail should precede distant cache retention");
}

void TestZeroUnknownAndLargeCosts() {
    std::vector<ViewportSectionResidencyInput> sections{Section(true, true, 32, 2)};
    auto plan = PlanViewportSectionResidency(sections, 0);
    Require(!plan.sections[0].wantNative && !plan.sections[0].wantPreview &&
        !plan.covered && plan.budgetLimited && !plan.expectedResidentBytes &&
        plan.nextVisibleRestore < 0,
        "Zero allowance must mean zero residency, not an implicit fallback budget");
    sections = {{}};
    sections[0].visible = true;
    plan = PlanViewportSectionResidency(sections, 100);
    Require(plan.nextVisibleRestore < 0 && !plan.covered && plan.budgetLimited,
        "Missing resources with unknown costs cannot be admitted as free allocations");

    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    sections = {Section(true, true), Section(true, true)};
    for (auto& section : sections) {
        section.expectedNativeBytes = maximum;
        section.expectedPreviewBytes = 0;
    }
    plan = PlanViewportSectionResidency(sections, maximum);
    Require(plan.expectedResidentBytes == maximum && plan.sections[0].wantNative &&
        !plan.sections[1].wantNative && plan.nextVisibleRestore == 0 && plan.budgetLimited,
        "Cost addition must not wrap around and admit another enormous resource");
    plan = PlanViewportSectionResidency({}, 0);
    Require(plan.covered && !plan.budgetLimited && plan.nextVisibleRestore < 0,
        "An empty demand should have no work or memory restriction");
}

void TestPreviewPreservesPartialDetail() {
    using namespace Stack::EditorRendering;
    const ImageRegion core{4000,4000,1000,1000,1000,1000};
    const ImageRegion raster{4000,4000,996,996,1008,1008};
    const ViewportPreviewGeometry native{4000,4000,{4000,4000,1250,1250,500,500},500,500};
    auto plan = PlanViewportSectionPreviewDraw(core,raster,252,252,native);
    Require(plan.count == 4, "A coarse section must fill all four gaps around a sharp interior patch");
    double area = 0;
    for (std::size_t i=0; i<plan.count; ++i) {
        const auto& a = plan.clips[i];
        Require(a.Valid() && a.left >= .25 && a.top >= .25 && a.right <= .5 && a.bottom <= .5,
            "Section preview clips must remain inside their section core");
        Require(a.right <= .3125 || a.left >= .4375 || a.bottom <= .3125 || a.top >= .4375,
            "No preview clip may cover the sharper accepted native patch");
        for (std::size_t j=0; j<i; ++j) {
            const auto& b = plan.clips[j];
            Require(a.right <= b.left || a.left >= b.right || a.bottom <= b.top || a.top >= b.bottom,
                "Disjoint preview clips must not double-blend translucent image pixels");
        }
        area += (a.right-a.left)*(a.bottom-a.top);
    }
    Require(std::abs(area-(.25*.25-.125*.125)) < 1e-12,
        "Partial base coverage must leave every uncovered section area drawable");
    const ViewportPreviewGeometry fractional{4000,4000,{1999,1999,601,607,197,193},197,193};
    plan = PlanViewportSectionPreviewDraw(core,raster,252,252,fractional);
    Require(plan.count == 4 && plan.clips[0].bottom == 607.0/1999 && plan.clips[2].right == 601.0/1999,
        "A different accepted sample grid must retain its exact fractional coverage boundary");
    const ViewportPreviewGeometry outside{4000,4000,{4000,4000,3000,3000,500,500},500,500};
    Require(PlanViewportSectionPreviewDraw(core,raster,252,252,outside).count == 1,
        "A nonintersecting sharp patch must not suppress useful section coverage");
}

void TestPreviewUsesActualSampleDensity() {
    using namespace Stack::EditorRendering;
    const ImageRegion core{4000,4000,1000,1000,1000,1000};
    const ImageRegion raster{4000,4000,996,996,1008,1008};
    const ViewportPreviewGeometry fine{4000,4000,WholeImageRegion(2000,2000),2000,2000};
    Require(PlanViewportSectionPreviewDraw(core,raster,252,252,fine).count == 0,
        "A quarter-sized section must not blur a finer whole-image base");
    const ViewportPreviewGeometry coarse{4000,4000,{500,500,150,150,75,75},75,75};
    const auto plan = PlanViewportSectionPreviewDraw(core,raster,252,252,coarse,2000,2000);
    Require(plan.count == 1 && plan.clips[0].left == .3 && plan.clips[0].top == .3 &&
        plan.clips[0].right == .45 && plan.clips[0].bottom == .45,
        "A finer overview must retain the gaps while a section improves the coarser base patch");
    Require(PlanViewportSectionPreviewDraw(core,raster,252,252,fine,2000,2000).count == 0 &&
        PlanViewportSectionPreviewDraw(core,raster,252,252,coarse,500,500).count == 1,
        "Only actually finer matching imagery may suppress a section display copy");
    const ImageRegion edge{2051,2051,2048,2048,3,3};
    const ImageRegion edgeRaster{2051,2051,2044,2044,7,7};
    const ViewportPreviewGeometry edgeBase{2051,2051,WholeImageRegion(560,560),560,560};
    Require(PlanViewportSectionPreviewDraw(edge,edgeRaster,2,2,edgeBase).count == 1,
        "Small edge sections must use their actual rounded display-copy density, not a fixed four-pixel spacing");
    const ViewportPreviewGeometry wrongCanvas{2000,2000,WholeImageRegion(500,500),500,500};
    Require(PlanViewportSectionPreviewDraw(core,raster,252,252,wrongCanvas).count == 0,
        "Sections cannot be mapped using another accepted logical canvas");
}
}

int main() {
    try {
        TestVisibleBudgetAndStableRestoration();
        TestBaselineAndNativeReplacement();
        TestRestorePriorityAndNoPrefetch();
        TestResidentPreferenceAndOffscreenLru();
        TestZeroUnknownAndLargeCosts();
        TestPreviewPreservesPartialDetail();
        TestPreviewUsesActualSampleDensity();
        std::cout << "Viewport section residency budget, detail coverage, LRU and convergence checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
