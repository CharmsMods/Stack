#include "NodeMath/PointwiseIR.h"

#include "NodeMath/ContractTypes.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Stack::NodeMath {
namespace {

std::size_t RequiredInputCount(PointwiseOperation operation) {
    switch (operation) {
        case PointwiseOperation::Input:
        case PointwiseOperation::Constant:
            return 0;
        case PointwiseOperation::Identity:
        case PointwiseOperation::Premultiply:
        case PointwiseOperation::Unpremultiply:
            return 1;
        case PointwiseOperation::Add:
        case PointwiseOperation::Subtract:
        case PointwiseOperation::Multiply:
        case PointwiseOperation::Minimum:
        case PointwiseOperation::Maximum:
        case PointwiseOperation::AbsoluteDifference:
        case PointwiseOperation::ExposureEv:
            return 2;
        case PointwiseOperation::Clamp:
            return 3;
    }
    return std::numeric_limits<std::size_t>::max();
}

bool IsFinitePixel(const PointwisePixel& value) {
    return std::all_of(value.begin(), value.end(), [](double component) {
        return std::isfinite(component);
    });
}

void AppendUniqueSources(
    std::vector<PointwiseSourceLocation>& destination,
    const std::vector<PointwiseSourceLocation>& sources) {
    for (const PointwiseSourceLocation& source : sources) {
        const auto duplicate = std::find_if(
            destination.begin(),
            destination.end(),
            [&](const PointwiseSourceLocation& existing) {
                return existing.authoredNodeId == source.authoredNodeId &&
                    existing.definitionId == source.definitionId &&
                    existing.inputPortId == source.inputPortId &&
                    existing.outputPortId == source.outputPortId;
            });
        if (duplicate == destination.end()) {
            destination.push_back(source);
        }
    }
}

std::vector<PointwiseSourceLocation> SourcesForInstruction(const PointwiseInstruction& instruction) {
    return instruction.sources;
}

PointwiseDiagnostic MakeDiagnostic(
    std::string code,
    std::string message,
    const PointwiseInstruction* instruction = nullptr) {
    PointwiseDiagnostic diagnostic;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    if (instruction != nullptr) {
        diagnostic.sources = instruction->sources;
    }
    return diagnostic;
}

PointwisePixel ApplyOperation(
    PointwiseOperation operation,
    const std::vector<PointwisePixel>& inputs) {
    PointwisePixel result { 0.0, 0.0, 0.0, 0.0 };
    switch (operation) {
        case PointwiseOperation::Identity:
            return inputs[0];
        case PointwiseOperation::Add:
            for (int i = 0; i < 4; ++i) result[i] = inputs[0][i] + inputs[1][i];
            return result;
        case PointwiseOperation::Subtract:
            for (int i = 0; i < 4; ++i) result[i] = inputs[0][i] - inputs[1][i];
            return result;
        case PointwiseOperation::Multiply:
            for (int i = 0; i < 4; ++i) result[i] = inputs[0][i] * inputs[1][i];
            return result;
        case PointwiseOperation::Minimum:
            for (int i = 0; i < 4; ++i) result[i] = std::min(inputs[0][i], inputs[1][i]);
            return result;
        case PointwiseOperation::Maximum:
            for (int i = 0; i < 4; ++i) result[i] = std::max(inputs[0][i], inputs[1][i]);
            return result;
        case PointwiseOperation::AbsoluteDifference:
            for (int i = 0; i < 4; ++i) result[i] = std::abs(inputs[0][i] - inputs[1][i]);
            return result;
        case PointwiseOperation::Clamp:
            for (int i = 0; i < 4; ++i) {
                const double low = std::min(inputs[1][i], inputs[2][i]);
                const double high = std::max(inputs[1][i], inputs[2][i]);
                result[i] = std::clamp(inputs[0][i], low, high);
            }
            return result;
        case PointwiseOperation::ExposureEv: {
            const double gain = std::exp2(inputs[1][0]);
            result = inputs[0];
            result[0] *= gain;
            result[1] *= gain;
            result[2] *= gain;
            return result;
        }
        case PointwiseOperation::Premultiply:
            result = inputs[0];
            result[0] *= result[3];
            result[1] *= result[3];
            result[2] *= result[3];
            return result;
        case PointwiseOperation::Unpremultiply:
            result = inputs[0];
            if (inputs[0][3] <= 1.0e-6) {
                result[0] = 0.0;
                result[1] = 0.0;
                result[2] = 0.0;
                return result;
            }
            result[0] /= result[3];
            result[1] /= result[3];
            result[2] /= result[3];
            return result;
        case PointwiseOperation::Input:
        case PointwiseOperation::Constant:
            break;
    }
    return result;
}

std::string ExactDouble(double value) {
    std::ostringstream stream;
    stream << std::hexfloat << value;
    return stream.str();
}

std::string InstructionKey(
    const PointwiseInstruction& instruction,
    bool includeConstantValues) {
    std::ostringstream stream;
    stream << PointwiseOperationName(instruction.operation) << '|'
           << PointwiseValueClassName(instruction.valueClass);
    for (PointwiseValueId input : instruction.inputs) {
        stream << '|' << input;
    }
    if (instruction.operation == PointwiseOperation::Constant && includeConstantValues) {
        for (double component : instruction.constant) {
            stream << '|' << ExactDouble(component);
        }
    }
    return stream.str();
}

std::string CanonicalProgram(
    const PointwiseProgram& program,
    bool includeSources,
    bool includeConstantValues) {
    std::ostringstream stream;
    stream << "pointwise-ir-v1\nroot=" << program.rootId << '\n';
    for (const PointwiseInstruction& instruction : program.instructions) {
        stream << instruction.id << '|' << InstructionKey(instruction, includeConstantValues);
        if (includeSources) {
            std::vector<PointwiseSourceLocation> sources = instruction.sources;
            std::sort(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
                return std::tie(left.authoredNodeId, left.definitionId, left.inputPortId, left.outputPortId) <
                    std::tie(right.authoredNodeId, right.definitionId, right.inputPortId, right.outputPortId);
            });
            for (const auto& source : sources) {
                stream << "|src:" << source.authoredNodeId << ':' << source.definitionId
                       << ':' << source.inputPortId << ':' << source.outputPortId;
            }
        }
        stream << '\n';
    }
    return stream.str();
}

const PointwiseInstruction* FindInstruction(
    const PointwiseProgram& program,
    PointwiseValueId id) {
    const auto found = std::find_if(
        program.instructions.begin(),
        program.instructions.end(),
        [&](const PointwiseInstruction& instruction) { return instruction.id == id; });
    return found == program.instructions.end() ? nullptr : &(*found);
}

std::uint64_t SaturatingTargetBytes(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    const std::uint64_t w = static_cast<std::uint64_t>(width);
    const std::uint64_t h = static_cast<std::uint64_t>(height);
    if (w > std::numeric_limits<std::uint64_t>::max() / h) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t pixels = w * h;
    if (pixels > std::numeric_limits<std::uint64_t>::max() / 8u) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return pixels * 8u;
}

std::uint64_t SaturatingMultiply(std::uint64_t value, std::uint64_t multiplier) {
    if (multiplier != 0 && value > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return value * multiplier;
}

} // namespace

const char* PointwiseOperationName(PointwiseOperation operation) {
    switch (operation) {
        case PointwiseOperation::Input: return "input";
        case PointwiseOperation::Constant: return "constant";
        case PointwiseOperation::Identity: return "identity";
        case PointwiseOperation::Add: return "add";
        case PointwiseOperation::Subtract: return "subtract";
        case PointwiseOperation::Multiply: return "multiply";
        case PointwiseOperation::Minimum: return "minimum";
        case PointwiseOperation::Maximum: return "maximum";
        case PointwiseOperation::AbsoluteDifference: return "absolute-difference";
        case PointwiseOperation::Clamp: return "clamp";
        case PointwiseOperation::ExposureEv: return "exposure-ev";
        case PointwiseOperation::Premultiply: return "premultiply";
        case PointwiseOperation::Unpremultiply: return "unpremultiply";
    }
    return "invalid";
}

const char* PointwiseValueClassName(PointwiseValueClass valueClass) {
    switch (valueClass) {
        case PointwiseValueClass::UniformFloat4: return "uniform-float4";
        case PointwiseValueClass::RgbaFloatField: return "rgba-float-field";
    }
    return "invalid";
}

PointwiseValueId AppendPointwiseInput(
    PointwiseProgram& program,
    PointwiseSourceLocation source) {
    PointwiseInstruction instruction;
    instruction.id = program.instructions.empty() ? 0 : program.instructions.back().id + 1;
    instruction.operation = PointwiseOperation::Input;
    instruction.valueClass = PointwiseValueClass::RgbaFloatField;
    if (source.authoredNodeId >= 0 || !source.definitionId.empty()) {
        instruction.sources.push_back(std::move(source));
    }
    program.instructions.push_back(std::move(instruction));
    return program.instructions.back().id;
}

PointwiseValueId AppendPointwiseConstant(
    PointwiseProgram& program,
    const PointwisePixel& value,
    PointwiseSourceLocation source) {
    PointwiseInstruction instruction;
    instruction.id = program.instructions.empty() ? 0 : program.instructions.back().id + 1;
    instruction.operation = PointwiseOperation::Constant;
    instruction.valueClass = PointwiseValueClass::UniformFloat4;
    instruction.constant = value;
    if (source.authoredNodeId >= 0 || !source.definitionId.empty()) {
        instruction.sources.push_back(std::move(source));
    }
    program.instructions.push_back(std::move(instruction));
    return program.instructions.back().id;
}

PointwiseValueId AppendPointwiseOperation(
    PointwiseProgram& program,
    PointwiseOperation operation,
    std::vector<PointwiseValueId> inputs,
    PointwiseSourceLocation source) {
    PointwiseInstruction instruction;
    instruction.id = program.instructions.empty() ? 0 : program.instructions.back().id + 1;
    instruction.operation = operation;
    instruction.inputs = std::move(inputs);
    instruction.sources.push_back(std::move(source));
    instruction.valueClass = PointwiseValueClass::UniformFloat4;
    for (PointwiseValueId input : instruction.inputs) {
        if (const PointwiseInstruction* inputInstruction = FindInstruction(program, input);
            inputInstruction != nullptr && inputInstruction->valueClass == PointwiseValueClass::RgbaFloatField) {
            instruction.valueClass = PointwiseValueClass::RgbaFloatField;
            break;
        }
    }
    program.instructions.push_back(std::move(instruction));
    return program.instructions.back().id;
}

std::vector<PointwiseDiagnostic> ValidatePointwiseProgram(const PointwiseProgram& program) {
    std::vector<PointwiseDiagnostic> diagnostics;
    std::unordered_map<PointwiseValueId, const PointwiseInstruction*> seen;
    int inputCount = 0;
    for (const PointwiseInstruction& instruction : program.instructions) {
        if (instruction.id < 0 || seen.find(instruction.id) != seen.end()) {
            diagnostics.push_back(MakeDiagnostic(
                "pointwise.invalid-id",
                "Pointwise instructions require unique non-negative SSA value IDs.",
                &instruction));
            continue;
        }
        if (instruction.inputs.size() != RequiredInputCount(instruction.operation)) {
            diagnostics.push_back(MakeDiagnostic(
                "pointwise.invalid-arity",
                std::string(PointwiseOperationName(instruction.operation)) +
                    " has an invalid operand count.",
                &instruction));
        }
        for (PointwiseValueId input : instruction.inputs) {
            if (seen.find(input) == seen.end()) {
                diagnostics.push_back(MakeDiagnostic(
                    "pointwise.invalid-order",
                    "An operand does not refer to an earlier SSA value; authored dependency order cannot be established.",
                    &instruction));
            }
        }
        if (instruction.operation != PointwiseOperation::Input &&
            instruction.operation != PointwiseOperation::Constant) {
            bool hasFieldInput = false;
            bool allInputsResolve = true;
            for (PointwiseValueId input : instruction.inputs) {
                const auto found = seen.find(input);
                if (found == seen.end()) {
                    allInputsResolve = false;
                    continue;
                }
                hasFieldInput = hasFieldInput ||
                    found->second->valueClass == PointwiseValueClass::RgbaFloatField;
            }
            const PointwiseValueClass expectedClass = hasFieldInput
                ? PointwiseValueClass::RgbaFloatField
                : PointwiseValueClass::UniformFloat4;
            if (allInputsResolve && instruction.valueClass != expectedClass) {
                diagnostics.push_back(MakeDiagnostic(
                    "pointwise.result-type",
                    "An instruction's declared value class does not match its ordered operands.",
                    &instruction));
            }
        }
        if (instruction.operation == PointwiseOperation::Input) {
            ++inputCount;
            if (instruction.valueClass != PointwiseValueClass::RgbaFloatField) {
                diagnostics.push_back(MakeDiagnostic(
                    "pointwise.input-type",
                    "The v1 sampled input must be an RGBA float field.",
                    &instruction));
            }
        }
        if (instruction.operation == PointwiseOperation::Constant) {
            if (instruction.valueClass != PointwiseValueClass::UniformFloat4 ||
                !IsFinitePixel(instruction.constant)) {
                diagnostics.push_back(MakeDiagnostic(
                    "pointwise.invalid-uniform",
                    "Uniform float4 constants must be finite.",
                    &instruction));
            }
        }
        seen[instruction.id] = &instruction;
    }
    if (inputCount > 1) {
        diagnostics.push_back(MakeDiagnostic(
            "pointwise.multiple-sampled-inputs",
            "The v1 pointwise IR accepts only one sampled RGBA input."));
    }
    const auto root = seen.find(program.rootId);
    if (root == seen.end()) {
        diagnostics.push_back(MakeDiagnostic(
            "pointwise.missing-root",
            "The pointwise program root does not resolve to an instruction."));
    } else if (root->second->valueClass != PointwiseValueClass::RgbaFloatField) {
        diagnostics.push_back(MakeDiagnostic(
            "pointwise.root-type",
            "A live pointwise image program must produce an RGBA float field.",
            root->second));
    }
    return diagnostics;
}

PointwiseOptimizationResult OptimizePointwiseProgram(const PointwiseProgram& program) {
    PointwiseOptimizationResult result;
    result.diagnostics = ValidatePointwiseProgram(program);
    if (!result.diagnostics.empty()) {
        return result;
    }

    std::unordered_map<PointwiseValueId, const PointwiseInstruction*> byId;
    for (const PointwiseInstruction& instruction : program.instructions) {
        byId[instruction.id] = &instruction;
    }
    std::unordered_set<PointwiseValueId> reachable;
    std::vector<PointwiseValueId> stack { program.rootId };
    while (!stack.empty()) {
        const PointwiseValueId id = stack.back();
        stack.pop_back();
        if (!reachable.insert(id).second) continue;
        for (PointwiseValueId input : byId[id]->inputs) stack.push_back(input);
    }
    result.deadInstructionsRemoved =
        static_cast<int>(program.instructions.size() - reachable.size());

    std::unordered_map<PointwiseValueId, PointwiseValueId> remap;
    std::map<std::string, PointwiseValueId> common;
    for (const PointwiseInstruction& original : program.instructions) {
        if (reachable.find(original.id) == reachable.end()) continue;

        PointwiseInstruction lowered = original;
        for (PointwiseValueId& input : lowered.inputs) input = remap[input];
        lowered.id = result.program.instructions.empty()
            ? 0 : result.program.instructions.back().id + 1;

        bool allUniform = lowered.operation != PointwiseOperation::Input &&
            lowered.operation != PointwiseOperation::Constant;
        std::vector<PointwisePixel> foldedInputs;
        for (PointwiseValueId input : lowered.inputs) {
            const PointwiseInstruction* inputInstruction = FindInstruction(result.program, input);
            if (inputInstruction == nullptr ||
                inputInstruction->operation != PointwiseOperation::Constant) {
                allUniform = false;
                break;
            }
            foldedInputs.push_back(inputInstruction->constant);
        }
        if (allUniform) {
            lowered.operation = PointwiseOperation::Constant;
            lowered.valueClass = PointwiseValueClass::UniformFloat4;
            lowered.constant = ApplyOperation(original.operation, foldedInputs);
            if (!IsFinitePixel(lowered.constant)) {
                result.diagnostics.push_back(MakeDiagnostic(
                    "pointwise.constant-fold-nonfinite",
                    "Constant folding produced a non-finite uniform; the expression cannot be fused.",
                    &original));
                return result;
            }
            lowered.inputs.clear();
            for (PointwiseValueId input : original.inputs) {
                const PointwiseInstruction* foldedSource = FindInstruction(result.program, remap[input]);
                if (foldedSource != nullptr) AppendUniqueSources(lowered.sources, foldedSource->sources);
            }
            ++result.constantsFolded;
        }

        const std::string key = InstructionKey(lowered, true);
        const auto existing = common.find(key);
        if (existing != common.end() && lowered.operation != PointwiseOperation::Input) {
            PointwiseInstruction* existingInstruction = nullptr;
            for (PointwiseInstruction& candidate : result.program.instructions) {
                if (candidate.id == existing->second) {
                    existingInstruction = &candidate;
                    break;
                }
            }
            if (existingInstruction != nullptr) {
                AppendUniqueSources(existingInstruction->sources, lowered.sources);
            }
            remap[original.id] = existing->second;
            ++result.commonSubexpressionsReused;
            continue;
        }

        result.program.instructions.push_back(std::move(lowered));
        remap[original.id] = result.program.instructions.back().id;
        common[key] = result.program.instructions.back().id;
    }

    result.program.rootId = remap[program.rootId];

    // Folding and CSE can make previously reachable operands unreachable.
    // Compact once more so the generated program owns only values needed by
    // the requested root and uses deterministic dense SSA IDs.
    std::unordered_map<PointwiseValueId, const PointwiseInstruction*> optimizedById;
    for (const PointwiseInstruction& instruction : result.program.instructions) {
        optimizedById[instruction.id] = &instruction;
    }
    std::unordered_set<PointwiseValueId> optimizedReachable;
    std::vector<PointwiseValueId> optimizedStack { result.program.rootId };
    while (!optimizedStack.empty()) {
        const PointwiseValueId id = optimizedStack.back();
        optimizedStack.pop_back();
        if (!optimizedReachable.insert(id).second) continue;
        for (PointwiseValueId input : optimizedById[id]->inputs) optimizedStack.push_back(input);
    }
    PointwiseProgram compacted;
    std::unordered_map<PointwiseValueId, PointwiseValueId> compactRemap;
    for (const PointwiseInstruction& instruction : result.program.instructions) {
        if (optimizedReachable.find(instruction.id) == optimizedReachable.end()) continue;
        PointwiseInstruction compact = instruction;
        compact.id = compacted.instructions.empty() ? 0 : compacted.instructions.back().id + 1;
        compactRemap[instruction.id] = compact.id;
        for (PointwiseValueId& input : compact.inputs) input = compactRemap[input];
        compacted.instructions.push_back(std::move(compact));
    }
    compacted.rootId = compactRemap[result.program.rootId];
    result.program = std::move(compacted);
    result.semanticFingerprint = PointwiseProgramSemanticFingerprint(result.program);
    result.success = true;
    return result;
}

bool EvaluatePointwiseProgram(
    const PointwiseProgram& program,
    const PointwisePixel& input,
    PointwisePixel& output,
    std::string& error) {
    const std::vector<PointwiseDiagnostic> diagnostics = ValidatePointwiseProgram(program);
    if (!diagnostics.empty()) {
        error = diagnostics.front().message;
        return false;
    }
    std::unordered_map<PointwiseValueId, PointwisePixel> values;
    for (const PointwiseInstruction& instruction : program.instructions) {
        if (instruction.operation == PointwiseOperation::Input) {
            values[instruction.id] = input;
        } else if (instruction.operation == PointwiseOperation::Constant) {
            values[instruction.id] = instruction.constant;
        } else {
            std::vector<PointwisePixel> operands;
            operands.reserve(instruction.inputs.size());
            for (PointwiseValueId operand : instruction.inputs) operands.push_back(values[operand]);
            values[instruction.id] = ApplyOperation(instruction.operation, operands);
        }
    }
    output = values[program.rootId];
    error.clear();
    return true;
}

GeneratedPointwiseShader GeneratePointwiseShader(
    const PointwiseOptimizationResult& optimized,
    const PointwiseCompileLimits& limits) {
    GeneratedPointwiseShader generated;
    generated.semanticFingerprint = optimized.semanticFingerprint;
    if (!optimized.success) {
        generated.diagnostics = optimized.diagnostics;
        return generated;
    }

    std::size_t operationCount = 0;
    for (const PointwiseInstruction& instruction : optimized.program.instructions) {
        if (instruction.operation != PointwiseOperation::Input &&
            instruction.operation != PointwiseOperation::Constant) {
            ++operationCount;
            if (operationCount > limits.maximumOperations) {
                generated.diagnostics.push_back(MakeDiagnostic(
                    "pointwise.operation-limit",
                    "The fused pointwise group exceeds the 48-operation v1 limit.",
                    &instruction));
                return generated;
            }
        }
    }

    std::vector<std::string> lines {
        "#version 330 core",
        "in vec2 vTexCoord;",
        "out vec4 FragColor;",
        "uniform sampler2D uInput;"
    };
    for (const PointwiseInstruction& instruction : optimized.program.instructions) {
        if (instruction.operation == PointwiseOperation::Constant) {
            const std::string name = "uConstant" + std::to_string(instruction.id);
            lines.push_back("uniform vec4 " + name + ";");
            generated.uniforms.push_back({ name, instruction.constant });
        }
    }
    lines.push_back("void main() {");
    for (const PointwiseInstruction& instruction : optimized.program.instructions) {
        const std::string output = "v" + std::to_string(instruction.id);
        auto value = [](PointwiseValueId id) { return "v" + std::to_string(id); };
        std::string expression;
        switch (instruction.operation) {
            case PointwiseOperation::Input:
                expression = "texture(uInput, vTexCoord)";
                break;
            case PointwiseOperation::Constant:
                expression = "uConstant" + std::to_string(instruction.id);
                break;
            case PointwiseOperation::Identity:
                expression = value(instruction.inputs[0]);
                break;
            case PointwiseOperation::Add:
                expression = value(instruction.inputs[0]) + " + " + value(instruction.inputs[1]);
                break;
            case PointwiseOperation::Subtract:
                expression = value(instruction.inputs[0]) + " - " + value(instruction.inputs[1]);
                break;
            case PointwiseOperation::Multiply:
                expression = value(instruction.inputs[0]) + " * " + value(instruction.inputs[1]);
                break;
            case PointwiseOperation::Minimum:
                expression = "min(" + value(instruction.inputs[0]) + ", " + value(instruction.inputs[1]) + ")";
                break;
            case PointwiseOperation::Maximum:
                expression = "max(" + value(instruction.inputs[0]) + ", " + value(instruction.inputs[1]) + ")";
                break;
            case PointwiseOperation::AbsoluteDifference:
                expression = "abs(" + value(instruction.inputs[0]) + " - " + value(instruction.inputs[1]) + ")";
                break;
            case PointwiseOperation::Clamp:
                expression = "clamp(" + value(instruction.inputs[0]) + ", min(" +
                    value(instruction.inputs[1]) + ", " + value(instruction.inputs[2]) + "), max(" +
                    value(instruction.inputs[1]) + ", " + value(instruction.inputs[2]) + "))";
                break;
            case PointwiseOperation::ExposureEv:
                expression = "vec4(" + value(instruction.inputs[0]) + ".rgb * exp2(" +
                    value(instruction.inputs[1]) + ".x), " + value(instruction.inputs[0]) + ".a)";
                break;
            case PointwiseOperation::Premultiply:
                expression = "vec4(" + value(instruction.inputs[0]) + ".rgb * " +
                    value(instruction.inputs[0]) + ".a, " + value(instruction.inputs[0]) + ".a)";
                break;
            case PointwiseOperation::Unpremultiply:
                expression = "(" + value(instruction.inputs[0]) + ".a > 0.000001 ? vec4(" +
                    value(instruction.inputs[0]) + ".rgb / " + value(instruction.inputs[0]) +
                    ".a, " + value(instruction.inputs[0]) + ".a) : vec4(0.0, 0.0, 0.0, " +
                    value(instruction.inputs[0]) + ".a))";
                break;
        }
        lines.push_back("    vec4 " + output + " = " + expression + ";");
        if (!instruction.sources.empty()) {
            generated.sourceMap.push_back({ static_cast<int>(lines.size()), instruction.sources });
        }
    }
    lines.push_back("    FragColor = v" + std::to_string(optimized.program.rootId) + ";");
    lines.push_back("}");

    std::ostringstream source;
    for (const std::string& line : lines) source << line << '\n';
    generated.fragmentSource = source.str();
    if (generated.fragmentSource.size() > limits.maximumGeneratedSourceBytes) {
        const PointwiseInstruction* root = FindInstruction(optimized.program, optimized.program.rootId);
        generated.diagnostics.push_back(MakeDiagnostic(
            "pointwise.source-limit",
            "The generated pointwise shader exceeds the 64-KiB v1 source limit.",
            root));
        generated.fragmentSource.clear();
        generated.sourceMap.clear();
        generated.uniforms.clear();
        return generated;
    }
    generated.structureFingerprint = Sha256ContentIdentity(generated.fragmentSource);
    generated.success = true;
    return generated;
}

PointwisePhysicalPlan BuildPointwisePhysicalPlan(
    const PointwiseProgram& program,
    int width,
    int height,
    const std::set<int>& materializeAfterAuthoredNodes) {
    PointwisePhysicalPlan plan;
    plan.diagnostics = ValidatePointwiseProgram(program);
    if (!plan.diagnostics.empty() || width <= 0 || height <= 0) {
        if (width <= 0 || height <= 0) {
            plan.diagnostics.push_back(MakeDiagnostic(
                "pointwise.invalid-extent",
                "Physical pointwise planning requires a positive target extent."));
        }
        return plan;
    }

    std::vector<int> currentGroup;
    for (const PointwiseInstruction& instruction : program.instructions) {
        if (instruction.valueClass != PointwiseValueClass::RgbaFloatField ||
            instruction.operation == PointwiseOperation::Input) {
            continue;
        }
        ++plan.unfusedPassCount;
        int sourceNodeId = -1;
        if (!instruction.sources.empty()) sourceNodeId = instruction.sources.front().authoredNodeId;
        if (sourceNodeId >= 0) currentGroup.push_back(sourceNodeId);
        if (sourceNodeId >= 0 &&
            materializeAfterAuthoredNodes.find(sourceNodeId) != materializeAfterAuthoredNodes.end()) {
            plan.fusedAuthoredNodeGroups.push_back(currentGroup);
            currentGroup.clear();
        }
    }
    if (!currentGroup.empty()) plan.fusedAuthoredNodeGroups.push_back(currentGroup);
    if (plan.unfusedPassCount > 0 && plan.fusedAuthoredNodeGroups.empty()) {
        plan.fusedAuthoredNodeGroups.emplace_back();
    }
    plan.fusedPassCount = static_cast<int>(plan.fusedAuthoredNodeGroups.size());
    plan.avoidedPassCount = std::max(0, plan.unfusedPassCount - plan.fusedPassCount);
    plan.transientSlotCount = std::min(2, plan.fusedPassCount);
    plan.bytesPerTarget = SaturatingTargetBytes(width, height);
    plan.unfusedMaterializedBytes = SaturatingMultiply(
        plan.bytesPerTarget,
        static_cast<std::uint64_t>(plan.unfusedPassCount));
    plan.plannedTransientBytes = SaturatingMultiply(
        plan.bytesPerTarget,
        static_cast<std::uint64_t>(plan.transientSlotCount));
    plan.valid = true;
    return plan;
}

std::vector<std::string> SelectPersistentResourceEvictions(
    const std::vector<PersistentResourceEntry>& entries,
    std::uint64_t byteBudget) {
    std::uint64_t total = 0;
    for (const PersistentResourceEntry& entry : entries) {
        if (entry.bytes > std::numeric_limits<std::uint64_t>::max() - total) {
            total = std::numeric_limits<std::uint64_t>::max();
            break;
        }
        total += entry.bytes;
    }
    if (total <= byteBudget) return {};

    std::vector<PersistentResourceEntry> candidates;
    for (const PersistentResourceEntry& entry : entries) {
        if (!entry.protectedResource && entry.bytes > 0) candidates.push_back(entry);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return std::tie(left.lastUseSerial, left.key) < std::tie(right.lastUseSerial, right.key);
    });
    std::vector<std::string> evictions;
    for (const PersistentResourceEntry& candidate : candidates) {
        if (total <= byteBudget) break;
        evictions.push_back(candidate.key);
        total = candidate.bytes > total ? 0 : total - candidate.bytes;
    }
    return evictions;
}

std::string PointwiseProgramSemanticFingerprint(const PointwiseProgram& program) {
    return Sha256ContentIdentity(CanonicalProgram(program, true, true));
}

} // namespace Stack::NodeMath
