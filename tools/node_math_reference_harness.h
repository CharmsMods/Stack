#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Stack::NodeMathReference {

using Pixel = std::array<double, 4>;

enum class Formula {
    Identity = 0,
    Add = 1,
    Multiply = 2,
    AddThenMultiply = 3,
    MultiplyThenAdd = 4,
};

struct GeneratedSuite {
    std::vector<Pixel> input;
    Pixel addend = {};
    Pixel multiplier = {};
};

struct GpuCaseResult {
    Formula formula = Formula::Identity;
    std::vector<Pixel> pixels;
    double synchronizedPassMilliseconds = 0.0;
};

struct GpuSuiteResult {
    std::string vendor;
    std::string renderer;
    std::string version;
    int width = 0;
    int height = 0;
    std::size_t sourceTargetBytes = 0;
    std::size_t outputTargetBytes = 0;
    std::vector<GpuCaseResult> cases;
};

const char* FormulaName(Formula formula);
std::vector<Formula> RequiredFormulas();
GeneratedSuite BuildGeneratedSuite();
std::vector<Pixel> EvaluateReference(
    const GeneratedSuite& suite,
    Formula formula);

bool ValidateCpuReference(std::string& error);
bool RunGpuSuite(
    const GeneratedSuite& suite,
    GpuSuiteResult& result,
    std::string& error);

double MaximumAbsoluteDifference(
    const std::vector<Pixel>& left,
    const std::vector<Pixel>& right);

} // namespace Stack::NodeMathReference
