#include "NodeMath/PointwiseIR.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>

namespace {

using namespace Stack::NodeMath;

int gChecks = 0;

bool Check(bool condition, const std::string& message) {
    ++gChecks;
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

PointwiseSourceLocation Source(int nodeId, const std::string& definition) {
    return { nodeId, definition, "imageIn", "imageOut" };
}

PointwiseProgram BuildAddThenMultiply(double add, double multiply) {
    PointwiseProgram program;
    const auto input = AppendPointwiseInput(program, Source(10, "stack:test/source"));
    const auto addValue = AppendPointwiseConstant(program, { add, add, add, add }, Source(20, "stack:test/add"));
    const auto added = AppendPointwiseOperation(
        program, PointwiseOperation::Add, { input, addValue }, Source(20, "stack:test/add"));
    const auto multiplyValue = AppendPointwiseConstant(
        program, { multiply, multiply, multiply, multiply }, Source(30, "stack:test/multiply"));
    program.rootId = AppendPointwiseOperation(
        program, PointwiseOperation::Multiply, { added, multiplyValue }, Source(30, "stack:test/multiply"));
    return program;
}

PointwiseProgram BuildMultiplyThenAdd(double multiply, double add) {
    PointwiseProgram program;
    const auto input = AppendPointwiseInput(program, Source(10, "stack:test/source"));
    const auto multiplyValue = AppendPointwiseConstant(
        program, { multiply, multiply, multiply, multiply }, Source(30, "stack:test/multiply"));
    const auto multiplied = AppendPointwiseOperation(
        program, PointwiseOperation::Multiply, { input, multiplyValue }, Source(30, "stack:test/multiply"));
    const auto addValue = AppendPointwiseConstant(program, { add, add, add, add }, Source(20, "stack:test/add"));
    program.rootId = AppendPointwiseOperation(
        program, PointwiseOperation::Add, { multiplied, addValue }, Source(20, "stack:test/add"));
    return program;
}

bool Near(double left, double right, double tolerance = 1.0e-9) {
    return std::abs(left - right) <= tolerance;
}

bool RunCpuTests() {
    bool ok = true;

    const PointwiseProgram addThenMultiply = BuildAddThenMultiply(0.25, 2.0);
    const PointwiseProgram multiplyThenAdd = BuildMultiplyThenAdd(2.0, 0.25);
    const PointwiseOptimizationResult optimizedAddThenMultiply =
        OptimizePointwiseProgram(addThenMultiply);
    const PointwiseOptimizationResult optimizedMultiplyThenAdd =
        OptimizePointwiseProgram(multiplyThenAdd);
    ok &= Check(optimizedAddThenMultiply.success, "Add -> Multiply optimizes");
    ok &= Check(optimizedMultiplyThenAdd.success, "Multiply -> Add optimizes");
    ok &= Check(
        optimizedAddThenMultiply.semanticFingerprint != optimizedMultiplyThenAdd.semanticFingerprint,
        "authored operation order changes semantic identity");

    PointwisePixel first {};
    PointwisePixel second {};
    std::string error;
    ok &= Check(EvaluatePointwiseProgram(optimizedAddThenMultiply.program, { 0.5, 0.2, -0.5, 0.4 }, first, error),
        "Add -> Multiply evaluates");
    ok &= Check(EvaluatePointwiseProgram(optimizedMultiplyThenAdd.program, { 0.5, 0.2, -0.5, 0.4 }, second, error),
        "Multiply -> Add evaluates");
    ok &= Check(Near(first[0], 1.5) && Near(second[0], 1.25),
        "noncommutative authored sequences remain observably distinct");
    ok &= Check(Near(first[3], 1.3) && Near(second[3], 1.05),
        "generic Data Math preserves its authored all-channel behavior");

    PointwiseProgram folding;
    const auto foldingInput = AppendPointwiseInput(folding, Source(1, "stack:test/source"));
    const auto c1 = AppendPointwiseConstant(folding, { 1.0, 1.0, 1.0, 1.0 }, Source(2, "stack:test/c1"));
    const auto c2 = AppendPointwiseConstant(folding, { 2.0, 2.0, 2.0, 2.0 }, Source(3, "stack:test/c2"));
    const auto foldedConstant = AppendPointwiseOperation(
        folding, PointwiseOperation::Add, { c1, c2 }, Source(4, "stack:test/fold"));
    folding.rootId = AppendPointwiseOperation(
        folding, PointwiseOperation::Multiply, { foldingInput, foldedConstant }, Source(5, "stack:test/use"));
    const auto folded = OptimizePointwiseProgram(folding);
    ok &= Check(folded.success && folded.constantsFolded == 1, "uniform-only expression is constant-folded");
    ok &= Check(folded.program.instructions.size() == 3, "constant folding and CSE remove redundant constant storage");

    PointwiseProgram dead = BuildAddThenMultiply(1.0, 2.0);
    AppendPointwiseConstant(dead, { 99.0, 99.0, 99.0, 99.0 }, Source(99, "stack:test/dead"));
    const auto deadOptimized = OptimizePointwiseProgram(dead);
    ok &= Check(deadOptimized.deadInstructionsRemoved == 1, "unreachable expression is removed");

    PointwiseProgram cse;
    const auto cseInput = AppendPointwiseInput(cse, Source(1, "stack:test/source"));
    const auto cseConstant = AppendPointwiseConstant(cse, { 0.5, 0.5, 0.5, 0.5 }, Source(2, "stack:test/constant"));
    const auto addA = AppendPointwiseOperation(cse, PointwiseOperation::Add, { cseInput, cseConstant }, Source(3, "stack:test/add-a"));
    const auto addB = AppendPointwiseOperation(cse, PointwiseOperation::Add, { cseInput, cseConstant }, Source(4, "stack:test/add-b"));
    cse.rootId = AppendPointwiseOperation(cse, PointwiseOperation::Multiply, { addA, addB }, Source(5, "stack:test/multiply"));
    const auto cseOptimized = OptimizePointwiseProgram(cse);
    ok &= Check(cseOptimized.commonSubexpressionsReused == 1, "ordered identical expressions share one SSA value");
    const auto sharedAdd = std::find_if(
        cseOptimized.program.instructions.begin(), cseOptimized.program.instructions.end(),
        [](const PointwiseInstruction& instruction) { return instruction.operation == PointwiseOperation::Add; });
    ok &= Check(sharedAdd != cseOptimized.program.instructions.end() && sharedAdd->sources.size() == 2,
        "CSE retains both authored-node source mappings");

    PointwiseProgram ordered;
    const auto orderedInput = AppendPointwiseInput(ordered, Source(1, "stack:test/source"));
    const auto orderedConstant = AppendPointwiseConstant(ordered, { 0.5, 0.5, 0.5, 0.5 }, Source(2, "stack:test/c"));
    const auto subtractForward = AppendPointwiseOperation(
        ordered, PointwiseOperation::Subtract, { orderedInput, orderedConstant }, Source(3, "stack:test/sub-forward"));
    const auto subtractReverse = AppendPointwiseOperation(
        ordered, PointwiseOperation::Subtract, { orderedConstant, orderedInput }, Source(4, "stack:test/sub-reverse"));
    ordered.rootId = AppendPointwiseOperation(
        ordered, PointwiseOperation::Add, { subtractForward, subtractReverse }, Source(5, "stack:test/add"));
    const auto orderedOptimized = OptimizePointwiseProgram(ordered);
    const int subtractCount = static_cast<int>(std::count_if(
        orderedOptimized.program.instructions.begin(), orderedOptimized.program.instructions.end(),
        [](const PointwiseInstruction& instruction) { return instruction.operation == PointwiseOperation::Subtract; }));
    ok &= Check(subtractCount == 2, "CSE does not reorder noncommutative operands");

    const auto generatedA = GeneratePointwiseShader(optimizedAddThenMultiply);
    const auto generatedB = GeneratePointwiseShader(
        OptimizePointwiseProgram(BuildAddThenMultiply(0.75, 3.0)));
    ok &= Check(generatedA.success && generatedB.success, "valid IR generates GLSL");
    ok &= Check(!generatedA.fragmentSource.empty() && !generatedA.sourceMap.empty(),
        "generated GLSL retains authored source lines");
    ok &= Check(generatedA.structureFingerprint == generatedB.structureFingerprint,
        "parameter-only changes reuse the generated-program structure");
    ok &= Check(generatedA.semanticFingerprint != generatedB.semanticFingerprint,
        "parameter-only changes still change semantic identity");

    PointwiseProgram invalidUniform = BuildAddThenMultiply(1.0, 2.0);
    invalidUniform.instructions[1].constant[0] = std::numeric_limits<double>::quiet_NaN();
    const auto invalidOptimized = OptimizePointwiseProgram(invalidUniform);
    ok &= Check(!invalidOptimized.success && !invalidOptimized.diagnostics.empty(),
        "non-finite uniform blocks lowering");
    ok &= Check(!invalidOptimized.diagnostics.front().sources.empty() &&
        invalidOptimized.diagnostics.front().sources.front().authoredNodeId == 20,
        "lowering failure points to its authored node");

    PointwiseProgram invalidType = BuildAddThenMultiply(1.0, 2.0);
    invalidType.instructions.back().valueClass = PointwiseValueClass::UniformFloat4;
    const auto invalidTypeDiagnostics = ValidatePointwiseProgram(invalidType);
    ok &= Check(std::any_of(
            invalidTypeDiagnostics.begin(), invalidTypeDiagnostics.end(),
            [](const PointwiseDiagnostic& diagnostic) {
                return diagnostic.code == "pointwise.result-type";
            }),
        "declared IR result types must match their ordered operands");

    PointwiseProgram foldingOverflow;
    const auto overflowInput = AppendPointwiseInput(foldingOverflow, Source(1, "stack:test/source"));
    const auto maximum = AppendPointwiseConstant(
        foldingOverflow,
        { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
          std::numeric_limits<double>::max(), std::numeric_limits<double>::max() },
        Source(2, "stack:test/maximum"));
    const auto two = AppendPointwiseConstant(
        foldingOverflow, { 2.0, 2.0, 2.0, 2.0 }, Source(3, "stack:test/two"));
    const auto overflowed = AppendPointwiseOperation(
        foldingOverflow, PointwiseOperation::Multiply, { maximum, two }, Source(4, "stack:test/overflow"));
    foldingOverflow.rootId = AppendPointwiseOperation(
        foldingOverflow, PointwiseOperation::Add, { overflowInput, overflowed }, Source(5, "stack:test/use"));
    const auto overflowOptimized = OptimizePointwiseProgram(foldingOverflow);
    ok &= Check(!overflowOptimized.success && !overflowOptimized.diagnostics.empty() &&
            overflowOptimized.diagnostics.front().code == "pointwise.constant-fold-nonfinite",
        "constant-fold overflow is diagnosed instead of becoming a generated uniform");

    PointwiseProgram tooLarge;
    auto largeValue = AppendPointwiseInput(tooLarge, Source(1, "stack:test/source"));
    for (int index = 0; index < 49; ++index) {
        largeValue = AppendPointwiseOperation(
            tooLarge, PointwiseOperation::Identity, { largeValue },
            Source(100 + index, "stack:test/identity"));
    }
    tooLarge.rootId = largeValue;
    const auto largeGenerated = GeneratePointwiseShader(OptimizePointwiseProgram(tooLarge));
    ok &= Check(!largeGenerated.success && !largeGenerated.diagnostics.empty(),
        "generated program limit is enforced");
    ok &= Check(!largeGenerated.diagnostics.front().sources.empty() &&
        largeGenerated.diagnostics.front().sources.front().authoredNodeId == 148,
        "program-limit failure maps to the authored node that crossed the limit");

    PointwiseCompileLimits tinySourceLimit;
    tinySourceLimit.maximumGeneratedSourceBytes = 64;
    const auto sourceLimited = GeneratePointwiseShader(
        optimizedAddThenMultiply,
        tinySourceLimit);
    ok &= Check(!sourceLimited.success && !sourceLimited.diagnostics.empty() &&
            sourceLimited.diagnostics.front().code == "pointwise.source-limit",
        "generated shader source limit is enforced");
    ok &= Check(!sourceLimited.diagnostics.front().sources.empty() &&
            sourceLimited.diagnostics.front().sources.front().authoredNodeId == 30,
        "source-limit failure maps to the requested authored result");

    PointwiseProgram alpha;
    const auto alphaInput = AppendPointwiseInput(alpha, Source(1, "stack:test/source"));
    const auto premultiplied = AppendPointwiseOperation(
        alpha, PointwiseOperation::Premultiply, { alphaInput }, Source(2, "stack:test/premultiply"));
    alpha.rootId = AppendPointwiseOperation(
        alpha, PointwiseOperation::Unpremultiply, { premultiplied }, Source(3, "stack:test/unpremultiply"));
    PointwisePixel alphaOutput {};
    ok &= Check(EvaluatePointwiseProgram(alpha, { 1.5, -0.25, 0.75, 0.4 }, alphaOutput, error),
        "alpha program evaluates");
    ok &= Check(Near(alphaOutput[0], 1.5) && Near(alphaOutput[1], -0.25) && Near(alphaOutput[3], 0.4),
        "premultiply/unpremultiply preserves extended signed RGB above the alpha guard");
    ok &= Check(EvaluatePointwiseProgram(alpha, { 1.0, 0.5, 0.25, 0.0 }, alphaOutput, error),
        "zero-alpha program evaluates");
    ok &= Check(alphaOutput == PointwisePixel({ 0.0, 0.0, 0.0, 0.0 }),
        "guarded unpremultiply returns transparent black at zero alpha");
    ok &= Check(EvaluatePointwiseProgram(alpha, { 4.0, -2.0, 1.0, 5.0e-7 }, alphaOutput, error) &&
            Near(alphaOutput[0], 0.0) && Near(alphaOutput[1], 0.0) &&
            Near(alphaOutput[2], 0.0) && Near(alphaOutput[3], 5.0e-7),
        "guarded unpremultiply zeros unsafe RGB while preserving independent alpha");

    const PointwisePhysicalPlan fusedPlan = BuildPointwisePhysicalPlan(addThenMultiply, 3840, 2160);
    const PointwisePhysicalPlan debugPlan = BuildPointwisePhysicalPlan(addThenMultiply, 3840, 2160, { 20 });
    ok &= Check(fusedPlan.valid && fusedPlan.unfusedPassCount == 2 && fusedPlan.fusedPassCount == 1,
        "compatible chain plans as one fused pass");
    ok &= Check(fusedPlan.avoidedPassCount == 1 &&
        fusedPlan.unfusedMaterializedBytes > fusedPlan.plannedTransientBytes,
        "fused plan measures avoided passes and materialized bytes");
    ok &= Check(debugPlan.fusedPassCount == 2 && debugPlan.avoidedPassCount == 0,
        "explicit intermediate materialization splits the physical plan");
    ok &= Check(debugPlan.transientSlotCount == 2,
        "materialized sequential passes use a bounded ping-pong target plan");

    const std::vector<PersistentResourceEntry> resources {
        { "old", 80, 1, false },
        { "new", 80, 3, false },
        { "output", 80, 2, true }
    };
    const std::vector<std::string> evictions = SelectPersistentResourceEvictions(resources, 160);
    ok &= Check(evictions.size() == 1 && evictions.front() == "old",
        "persistent budget evicts the least-recently-used unprotected resource");

    return ok;
}

} // namespace

int main() {
    if (!RunCpuTests()) return 1;
    std::cout << "Phase 4 pointwise IR checks passed: " << gChecks << "\n";
    return 0;
}
