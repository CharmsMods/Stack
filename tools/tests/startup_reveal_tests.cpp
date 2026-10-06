#include "App/StartupReveal.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Startup reveal test failed: " << message << '\n';
    }
    return condition;
}

bool NearlyEqual(float first, float second, float tolerance = 0.001f) {
    return std::abs(first - second) <= tolerance;
}

} // namespace

int main() {
    using Stack::StartupReveal::Controller;
    using Stack::StartupReveal::Phase;

    constexpr ImVec2 viewport(1280.0f, 800.0f);
    Controller reveal;
    reveal.Enable();
    reveal.Start(10.0);

    const auto initial = reveal.Update(10.0, viewport);
    const auto forming = reveal.Update(10.40, viewport);
    const auto formed = reveal.Update(10.79, viewport);
    const auto expanding = reveal.Update(11.12, viewport);
    const auto fullDisk = reveal.Update(11.449, viewport);
    const auto holdSurface = reveal.Update(11.60, viewport);
    const auto interfaceReveal = reveal.Update(11.95, viewport);
    const auto captionReveal = reveal.Update(12.40, viewport);
    const auto complete = reveal.Update(12.56, viewport);

    bool passed = true;
    passed &= Expect(initial.phase == Phase::FormDisk, "reveal begins by forming a disk");
    passed &= Expect(NearlyEqual(initial.diskOpacity, 0.0f), "disk starts invisible");
    passed &= Expect(forming.diskOpacity > initial.diskOpacity, "disk opacity increases while forming");
    passed &= Expect(forming.diskRadius > initial.diskRadius, "disk radius grows while forming");
    passed &= Expect(formed.diskFeather < forming.diskFeather, "disk edge hardens while forming");
    passed &= Expect(expanding.phase == Phase::ExpandDisk, "disk expands after it forms");
    passed &= Expect(expanding.diskRadius > formed.diskRadius, "expansion increases disk radius");
    passed &= Expect(expanding.HasTransparentBackdrop(), "desktop remains visible until disk fills the viewport");
    passed &= Expect(holdSurface.phase == Phase::HoldSurface &&
        holdSurface.interfaceOpacity == 0.0f && !holdSurface.HasTransparentBackdrop(),
        "full surface holds opaque before the interface begins fading in");
    passed &= Expect(interfaceReveal.phase == Phase::RevealInterface, "interface reveal follows disk expansion");
    passed &= Expect(interfaceReveal.interfaceOpacity > 0.0f &&
        interfaceReveal.captionButtonOpacity == 0.0f,
        "caption buttons remain hidden during interface reveal");
    passed &= Expect(captionReveal.phase == Phase::RevealCaptionButtons,
        "caption buttons reveal after the interface");
    passed &= Expect(captionReveal.interfaceOpacity == 1.0f &&
        captionReveal.captionButtonOpacity > 0.0f,
        "caption button fade starts after interface completion");
    passed &= Expect(complete.phase == Phase::Complete && complete.AllowsInput(),
        "reveal completes and enables interaction");
    passed &= Expect(!complete.HasTransparentBackdrop(), "completed reveal is opaque");

    const float coverageRadius = std::sqrt(viewport.x * viewport.x + viewport.y * viewport.y) * 0.5f;
    passed &= Expect(expanding.diskRadius <= coverageRadius + 2.0f,
        "expansion does not overshoot full coverage before completion");
    passed &= Expect(fullDisk.diskRadius >= coverageRadius,
        "disk covers every viewport corner before the interface appears");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
