#include "node_math_reference_harness.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Stack::NodeMathReference::Formula;
using Stack::NodeMathReference::GeneratedSuite;
using Stack::NodeMathReference::GpuCaseResult;
using Stack::NodeMathReference::GpuSuiteResult;

const GpuCaseResult* FindGpuCase(
    const GpuSuiteResult& result,
    Formula formula) {
    const auto found = std::find_if(
        result.cases.begin(),
        result.cases.end(),
        [formula](const GpuCaseResult& item) {
            return item.formula == formula;
        });
    return found == result.cases.end() ? nullptr : &(*found);
}

} // namespace

int main(int argc, char** argv) {
    bool cpuOnly = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] ? argv[index] : "";
        if (argument == "--cpu-only") {
            cpuOnly = true;
        } else if (argument == "--gpu") {
            cpuOnly = false;
        } else {
            std::cerr << "FAIL: unknown argument: " << argument << "\n";
            return 2;
        }
    }

    std::string error;
    if (!Stack::NodeMathReference::ValidateCpuReference(error)) {
        std::cerr << "FAIL: CPU reference validation: " << error << "\n";
        return 1;
    }

    const GeneratedSuite suite = Stack::NodeMathReference::BuildGeneratedSuite();
    std::cout << "CPU reference: "
              << Stack::NodeMathReference::RequiredFormulas().size()
              << " formulas x " << suite.input.size()
              << " generated RGBA values passed.\n";
    const std::vector<Stack::NodeMathReference::Pixel> cpuAddThenMultiply =
        Stack::NodeMathReference::EvaluateReference(
            suite,
            Formula::AddThenMultiply);
    const std::vector<Stack::NodeMathReference::Pixel> cpuMultiplyThenAdd =
        Stack::NodeMathReference::EvaluateReference(
            suite,
            Formula::MultiplyThenAdd);
    const double cpuOrderDifference =
        Stack::NodeMathReference::MaximumAbsoluteDifference(
            cpuAddThenMultiply,
            cpuMultiplyThenAdd);
    std::cout << "CPU authored-order separation: "
              << std::setprecision(9) << cpuOrderDifference << "\n";

    if (cpuOnly) {
        std::cout << "PASS: Stack node-math CPU reference suite.\n";
        return 0;
    }

    GpuSuiteResult gpu;
    if (!Stack::NodeMathReference::RunGpuSuite(suite, gpu, error)) {
        std::cerr << "FAIL: GPU reference execution: " << error << "\n";
        return 1;
    }

    constexpr double kGpuTolerance = 2.0e-6;
    double maximumGpuError = 0.0;
    for (Formula formula : Stack::NodeMathReference::RequiredFormulas()) {
        const GpuCaseResult* gpuCase = FindGpuCase(gpu, formula);
        if (!gpuCase) {
            std::cerr << "FAIL: GPU result is missing "
                      << Stack::NodeMathReference::FormulaName(formula) << ".\n";
            return 1;
        }
        const std::vector<Stack::NodeMathReference::Pixel> cpu =
            Stack::NodeMathReference::EvaluateReference(suite, formula);
        const double difference =
            Stack::NodeMathReference::MaximumAbsoluteDifference(
                cpu,
                gpuCase->pixels);
        if (!std::isfinite(difference) || difference > kGpuTolerance) {
            std::cerr << "FAIL: "
                      << Stack::NodeMathReference::FormulaName(formula)
                      << " CPU/GPU maximum error " << std::setprecision(12)
                      << difference << " exceeds tolerance " << kGpuTolerance
                      << ".\n";
            return 1;
        }
        maximumGpuError = std::max(maximumGpuError, difference);
    }

    const GpuCaseResult* gpuAddThenMultiply =
        FindGpuCase(gpu, Formula::AddThenMultiply);
    const GpuCaseResult* gpuMultiplyThenAdd =
        FindGpuCase(gpu, Formula::MultiplyThenAdd);
    if (!gpuAddThenMultiply || !gpuMultiplyThenAdd) {
        std::cerr << "FAIL: GPU order-sensitive cases are incomplete.\n";
        return 1;
    }
    const double gpuOrderDifference =
        Stack::NodeMathReference::MaximumAbsoluteDifference(
            gpuAddThenMultiply->pixels,
            gpuMultiplyThenAdd->pixels);
    if (!std::isfinite(gpuOrderDifference) || gpuOrderDifference < 0.5) {
        std::cerr << "FAIL: GPU cases do not preserve observable Add/Multiply order.\n";
        return 1;
    }

    std::cout << "OpenGL vendor: " << gpu.vendor << "\n";
    std::cout << "OpenGL renderer: " << gpu.renderer << "\n";
    std::cout << "OpenGL version: " << gpu.version << "\n";
    std::cout << "Generated image: " << gpu.width << "x" << gpu.height
              << " RGBA32F\n";
    std::cout << "Target bytes: source=" << gpu.sourceTargetBytes
              << ", output=" << gpu.outputTargetBytes
              << ", peak-test-targets="
              << (gpu.sourceTargetBytes + gpu.outputTargetBytes) << "\n";
    for (const GpuCaseResult& item : gpu.cases) {
        std::cout << "Synchronized pass "
                  << Stack::NodeMathReference::FormulaName(item.formula)
                  << ": " << std::fixed << std::setprecision(6)
                  << item.synchronizedPassMilliseconds << " ms\n";
    }
    std::cout << "CPU/GPU maximum absolute error: "
              << std::scientific << maximumGpuError
              << " (tolerance " << kGpuTolerance << ")\n";
    std::cout << "GPU authored-order separation: "
              << std::fixed << std::setprecision(9)
              << gpuOrderDifference << "\n";
    std::cout << "PASS: Stack node-math CPU/GPU reference suite.\n";
    return 0;
}
