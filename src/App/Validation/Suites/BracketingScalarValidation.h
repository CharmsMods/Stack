#pragma once

#include "Raw/Bracketing/AlignmentExposureCohorts.h"
#include "Raw/Bracketing/ExposureGraph.h"
#include "Raw/Tone/BurstStartingLook.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace BracketingScalarValidation {

inline void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline bool Near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

inline void ValidateExposureGraph() {
    using namespace Raw::Bracketing;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::vector<ExposureEdge> edges{{0, 1, 1, .01}, {1, 2, 2, .04}};
    std::vector<bool> fixed{true, false, false};
    std::vector<double> ev{0, 0, 0};
    std::vector<double> variance;

    Require(SolveExposureGraph(edges, fixed, ev, variance), "Known exposure chain rejected");
    Require(Near(ev[0], 0) && Near(ev[1], 1) && Near(ev[2], 3),
        "Exposure chain gains changed");
    Require(Near(variance[0], 0) && Near(variance[1], .01) && Near(variance[2], .05),
        "Exposure chain marginal variances changed");

    fixed = {true, false, true};
    ev = {0, 0, 3};
    Require(SolveExposureGraph(edges, fixed, ev, variance) && Near(ev[1], 1) &&
        variance[0] == 0 && variance[2] == 0, "Multiple explicit exposure anchors changed");

    fixed = {true, false, false};
    ev = {0, 0, 0};
    Require(!SolveExposureGraph({{0, 1, 1, .01}}, fixed, ev, variance),
        "Disconnected exposure solve accepted");
    const auto reached = ConnectedExposureNodes(
        {{0, 1, 1, .01}, {2, 3, 1, .01}}, {true, false, false, false, true});
    Require(reached == std::vector<bool>({true, true, false, false, true}),
        "Exposure component reachability changed");

    // Keep the malformed-edge cases connected when the edge is valid, so
    // rejection cannot be explained by an unrelated disconnected node.
    fixed = {true, false};
    ev = {0, 0};
    Require(SolveExposureGraph({{0, 1, 1, .01}}, fixed, ev, variance),
        "Valid control edge for malformed exposure cases rejected");
    const std::vector<ExposureEdge> invalidEdges{
        {0, 1, nan, .01}, {0, 1, infinity, .01}, {0, 1, 1, 0},
        {0, 1, 1, -1}, {0, 1, 1, nan}, {0, 1, 1, infinity}, {0, 9, 1, .01}};
    for (const auto& edge : invalidEdges) {
        ev = {0, 0};
        Require(!SolveExposureGraph({edge}, fixed, ev, variance), "Invalid exposure edge accepted");
    }
    fixed = {true, false, false};
    ev = {0, nan, 0};
    Require(!SolveExposureGraph(edges, fixed, ev, variance), "Nonfinite initial exposure gain accepted");
    for (double anchor : {-32., 32.}) {
        ev = {anchor, 0};
        Require(SolveExposureGraph({{0, 1, 1, .01}}, {true, false}, ev, variance) &&
            Near(ev[1], anchor + 1), "Manual exposure anchor translation changed");
    }
}

inline void ValidateExposureCohorts() {
    using namespace Raw::Bracketing;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    auto cohorts = BuildAlignmentExposureCohorts(
        {{true, true, 1}, {true, true, std::exp2(.1)}, {true, true, std::exp2(.2)}}, 0);
    Require(cohorts.memberCount == std::vector<std::size_t>({2, 2, 1}),
        "Adjacent exposure cohorts were chained");
    Require(cohorts.anchorForSource[0] == 0 && cohorts.anchorForSource[1] == 0 &&
        cohorts.anchorForSource[2] == 2, "Exposure cohort anchor changed");
    Require(BuildAlignmentExposureCohorts(
        {{true, true, 1}, {true, true, std::exp2(.149999)}}, 0).memberCount[1] == 2,
        "Source inside exposure cohort boundary excluded");
    Require(BuildAlignmentExposureCohorts(
        {{true, true, 1}, {true, true, std::exp2(.150001)}}, 0).memberCount[1] == 1,
        "Source outside exposure cohort boundary included");

    cohorts = BuildAlignmentExposureCohorts({
        {false, true, 1}, {true, true, 1.05}, {false, true, .25},
        {true, false, .25}, {true, true, nan}, {true, true, 0}, {true, true, infinity}}, 0);
    Require(cohorts.memberCount[0] == 2 && cohorts.anchorForSource[1] == 0,
        "Disabled origin lost its coordinate anchor role");
    Require(cohorts.memberCount[2] == 0 &&
        std::find(cohorts.order.begin(), cohorts.order.end(), 2) == cohorts.order.end(),
        "Disabled non-origin source included in exposure cohorts");
    for (unsigned index : {3u, 4u, 5u, 6u}) {
        Require(cohorts.memberCount[index] == 1 && cohorts.anchorForSource[index] == 0,
            "Unmeasured source did not align independently to origin");
    }

    cohorts = BuildAlignmentExposureCohorts({
        {true, true, 1}, {true, true, .25}, {true, true, .25}, {true, true, .25}}, 0);
    Require(cohorts.anchorForSource[1] == 2 && cohorts.anchorForSource[3] == 2,
        "Stable median exposure cohort anchor changed");
    Require(std::find(cohorts.order.begin(), cohorts.order.end(), 2) <
        std::find(cohorts.order.begin(), cohorts.order.end(), 1),
        "Exposure cohort anchor was not scheduled first");
    Require(BuildAlignmentExposureCohorts({}, 0).order.empty(), "Empty exposure cohorts produced work");

    // Bounded, reproducible scalar coverage; no image or capture inputs.
    std::mt19937 random(31337);
    std::uniform_real_distribution<double> gainEv(-32, 32);
    for (unsigned trial = 0; trial < 200; ++trial) {
        std::vector<AlignmentExposureCohortInput> inputs(1 + random() % 24);
        for (auto& input : inputs) {
            input = {bool(random() % 2), bool(random() % 2), std::exp2(gainEv(random))};
        }
        const auto origin = random() % inputs.size();
        const auto result = BuildAlignmentExposureCohorts(inputs, origin);
        std::vector<unsigned> seen(inputs.size());
        for (auto index : result.order) {
            Require(index < inputs.size(), "Exposure cohort order index out of range");
            ++seen[index];
        }
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            Require(seen[index] == unsigned(inputs[index].enabled || index == origin),
                "Exposure cohort order omitted or duplicated a participating source");
            if (result.memberCount[index] > 1) {
                const auto anchor = result.anchorForSource[index];
                Require(anchor < inputs.size(), "Exposure cohort anchor index out of range");
                Require(result.memberCount[anchor] == result.memberCount[index],
                    "Exposure cohort anchor member count differs");
                Require(std::find(result.order.begin(), result.order.end(), anchor) <=
                    std::find(result.order.begin(), result.order.end(), index),
                    "Exposure cohort member preceded its anchor");
                Require(std::abs(std::log2(inputs[anchor].scale) - std::log2(inputs[index].scale)) <=
                    kImageExposureSpanEv, "Exposure cohort exceeded its gain span");
            }
        }
    }
}

inline void ValidateStartingLook() {
    using Stack::RawRecipe::PlanBurstStartingLook;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    auto plan = PlanBurstStartingLook(.18f, .18f);
    Require(plan.valid && std::abs(plan.exposureEv) < 1e-6f && plan.shoulder == 0,
        "Neutral burst starting look changed");
    const std::vector<std::pair<float, float>> invalidInputs{
        {0, 1}, {-1, 1}, {nan, 1}, {.18f, infinity}, {.18f, .1f}};
    for (const auto& input : invalidInputs) {
        Require(!PlanBurstStartingLook(input.first, input.second).valid,
            "Invalid burst starting-look input accepted");
    }
    for (float median : {.0001f, .01f, .18f, 1.f, 100.f}) {
        for (float ratio : {1.f, 2.f, 16.f, 1024.f}) {
            plan = PlanBurstStartingLook(median, median * ratio);
            Require(plan.valid && std::isfinite(plan.exposureEv) && std::abs(plan.exposureEv) <= .5f,
                "Burst starting-look exposure exceeded its bound");
            float previous = -infinity;
            for (float knot : {-16.f, -8.f, -4.f, 0.f, 1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f}) {
                const auto mapped = plan.OutputEv(knot);
                Require(std::isfinite(mapped) && mapped >= previous,
                    "Burst starting-look shoulder became nonfinite or nonmonotone");
                previous = mapped;
            }
            Require(plan.OutputEv(0) == 0 && plan.OutputEv(1) == 1,
                "Burst starting-look shoulder changed shadows or midtones");
        }
    }
}

} // namespace BracketingScalarValidation

inline void ValidateBracketingScalars() {
    BracketingScalarValidation::ValidateExposureGraph();
    BracketingScalarValidation::ValidateExposureCohorts();
    BracketingScalarValidation::ValidateStartingLook();
}

} // namespace Stack::Validation
