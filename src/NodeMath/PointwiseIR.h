#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Stack::NodeMath {

using PointwiseValueId = int;
using PointwisePixel = std::array<double, 4>;

enum class PointwiseValueClass {
    UniformFloat4,
    RgbaFloatField
};

enum class PointwiseOperation {
    Input,
    Constant,
    Identity,
    Add,
    Subtract,
    Multiply,
    Minimum,
    Maximum,
    AbsoluteDifference,
    Clamp,
    ExposureEv,
    Premultiply,
    Unpremultiply
};

struct PointwiseSourceLocation {
    int authoredNodeId = -1;
    std::string definitionId;
    std::string inputPortId;
    std::string outputPortId;
};

struct PointwiseInstruction {
    PointwiseValueId id = -1;
    PointwiseOperation operation = PointwiseOperation::Input;
    PointwiseValueClass valueClass = PointwiseValueClass::RgbaFloatField;
    std::vector<PointwiseValueId> inputs;
    PointwisePixel constant { 0.0, 0.0, 0.0, 0.0 };
    std::vector<PointwiseSourceLocation> sources;
};

struct PointwiseProgram {
    std::vector<PointwiseInstruction> instructions;
    PointwiseValueId rootId = -1;
};

enum class PointwiseDiagnosticSeverity {
    Information,
    Warning,
    Error
};

struct PointwiseDiagnostic {
    PointwiseDiagnosticSeverity severity = PointwiseDiagnosticSeverity::Error;
    std::string code;
    std::string message;
    std::vector<PointwiseSourceLocation> sources;
};

struct PointwiseOptimizationResult {
    bool success = false;
    PointwiseProgram program;
    int deadInstructionsRemoved = 0;
    int constantsFolded = 0;
    int commonSubexpressionsReused = 0;
    std::string semanticFingerprint;
    std::vector<PointwiseDiagnostic> diagnostics;
};

struct PointwiseCompileLimits {
    std::size_t maximumOperations = 48;
    std::size_t maximumGeneratedSourceBytes = 64u * 1024u;
};

struct PointwiseUniformBinding {
    std::string name;
    PointwisePixel value { 0.0, 0.0, 0.0, 0.0 };
};

struct PointwiseGeneratedLine {
    int line = 0;
    std::vector<PointwiseSourceLocation> sources;
};

struct GeneratedPointwiseShader {
    bool success = false;
    std::string fragmentSource;
    std::string structureFingerprint;
    std::string semanticFingerprint;
    std::vector<PointwiseUniformBinding> uniforms;
    std::vector<PointwiseGeneratedLine> sourceMap;
    std::vector<PointwiseDiagnostic> diagnostics;
};

struct PointwisePhysicalPlan {
    bool valid = false;
    int unfusedPassCount = 0;
    int fusedPassCount = 0;
    int avoidedPassCount = 0;
    int transientSlotCount = 0;
    std::uint64_t bytesPerTarget = 0;
    std::uint64_t unfusedMaterializedBytes = 0;
    std::uint64_t plannedTransientBytes = 0;
    std::vector<std::vector<int>> fusedAuthoredNodeGroups;
    std::vector<PointwiseDiagnostic> diagnostics;
};

struct PersistentResourceEntry {
    std::string key;
    std::uint64_t bytes = 0;
    std::uint64_t lastUseSerial = 0;
    bool protectedResource = false;
};

const char* PointwiseOperationName(PointwiseOperation operation);
const char* PointwiseValueClassName(PointwiseValueClass valueClass);

PointwiseValueId AppendPointwiseInput(
    PointwiseProgram& program,
    PointwiseSourceLocation source = {});
PointwiseValueId AppendPointwiseConstant(
    PointwiseProgram& program,
    const PointwisePixel& value,
    PointwiseSourceLocation source = {});
PointwiseValueId AppendPointwiseOperation(
    PointwiseProgram& program,
    PointwiseOperation operation,
    std::vector<PointwiseValueId> inputs,
    PointwiseSourceLocation source);

std::vector<PointwiseDiagnostic> ValidatePointwiseProgram(const PointwiseProgram& program);
PointwiseOptimizationResult OptimizePointwiseProgram(const PointwiseProgram& program);
bool EvaluatePointwiseProgram(
    const PointwiseProgram& program,
    const PointwisePixel& input,
    PointwisePixel& output,
    std::string& error);
GeneratedPointwiseShader GeneratePointwiseShader(
    const PointwiseOptimizationResult& optimized,
    const PointwiseCompileLimits& limits = {});

PointwisePhysicalPlan BuildPointwisePhysicalPlan(
    const PointwiseProgram& program,
    int width,
    int height,
    const std::set<int>& materializeAfterAuthoredNodes = {});

std::vector<std::string> SelectPersistentResourceEvictions(
    const std::vector<PersistentResourceEntry>& entries,
    std::uint64_t byteBudget);

std::string PointwiseProgramSemanticFingerprint(const PointwiseProgram& program);

} // namespace Stack::NodeMath
