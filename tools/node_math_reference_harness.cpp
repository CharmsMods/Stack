#include "node_math_reference_harness.h"

#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace Stack::NodeMathReference {
namespace {

std::string g_LastGlfwError;

void GlfwErrorCallback(int code, const char* description) {
    std::ostringstream stream;
    stream << "GLFW error " << code << ": "
           << (description ? description : "unknown error");
    g_LastGlfwError = stream.str();
}

Pixel EvaluatePixel(
    const Pixel& input,
    const Pixel& addend,
    const Pixel& multiplier,
    Formula formula) {
    Pixel output = input;
    for (std::size_t channel = 0; channel < output.size(); ++channel) {
        switch (formula) {
            case Formula::Identity:
                output[channel] = input[channel];
                break;
            case Formula::Add:
                output[channel] = input[channel] + addend[channel];
                break;
            case Formula::Multiply:
                output[channel] = input[channel] * multiplier[channel];
                break;
            case Formula::AddThenMultiply:
                output[channel] =
                    (input[channel] + addend[channel]) * multiplier[channel];
                break;
            case Formula::MultiplyThenAdd:
                output[channel] =
                    input[channel] * multiplier[channel] + addend[channel];
                break;
        }
    }
    return output;
}

bool PixelsMatch(
    const Pixel& actual,
    const Pixel& expected,
    double tolerance,
    std::string& error,
    const char* context) {
    for (std::size_t channel = 0; channel < actual.size(); ++channel) {
        if (!std::isfinite(actual[channel]) ||
            std::abs(actual[channel] - expected[channel]) > tolerance) {
            std::ostringstream stream;
            stream << context << " channel " << channel
                   << " expected " << std::setprecision(17) << expected[channel]
                   << " but received " << actual[channel]
                   << " (tolerance " << tolerance << ")";
            error = stream.str();
            return false;
        }
    }
    return true;
}

void DrainGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

bool CheckGl(const char* stage, std::string& error) {
    GLenum firstError = GL_NO_ERROR;
    int errorCount = 0;
    for (;;) {
        const GLenum current = glGetError();
        if (current == GL_NO_ERROR) {
            break;
        }
        if (firstError == GL_NO_ERROR) {
            firstError = current;
        }
        ++errorCount;
    }
    if (firstError == GL_NO_ERROR) {
        return true;
    }

    std::ostringstream stream;
    stream << "OpenGL failure after " << stage << ": first error 0x"
           << std::hex << static_cast<unsigned int>(firstError) << std::dec
           << " (" << errorCount << " error(s))";
    error = stream.str();
    return false;
}

std::string GlString(GLenum name) {
    const GLubyte* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unavailable";
}

} // namespace

const char* FormulaName(Formula formula) {
    switch (formula) {
        case Formula::Identity: return "Identity";
        case Formula::Add: return "Add";
        case Formula::Multiply: return "Multiply";
        case Formula::AddThenMultiply: return "AddThenMultiply";
        case Formula::MultiplyThenAdd: return "MultiplyThenAdd";
    }
    return "Unknown";
}

std::vector<Formula> RequiredFormulas() {
    return {
        Formula::Identity,
        Formula::Add,
        Formula::Multiply,
        Formula::AddThenMultiply,
        Formula::MultiplyThenAdd,
    };
}

GeneratedSuite BuildGeneratedSuite() {
    GeneratedSuite suite;
    suite.input = {
        Pixel{ 0.25, -0.5, 1.5, 0.0 },
        Pixel{ -2.0, 0.0, 0.125, 1.0 },
        Pixel{ 3.25, -1.25, 0.75, 2.0 },
        Pixel{ 1.0 / 3.0, 2.0 / 3.0, -0.25, 4.0 },
    };
    suite.addend = Pixel{ 0.75, -1.25, 2.0, 0.5 };
    suite.multiplier = Pixel{ 2.0, -0.5, 3.0, -2.0 };
    return suite;
}

std::vector<Pixel> EvaluateReference(
    const GeneratedSuite& suite,
    Formula formula) {
    std::vector<Pixel> output;
    output.reserve(suite.input.size());
    for (const Pixel& pixel : suite.input) {
        output.push_back(EvaluatePixel(
            pixel,
            suite.addend,
            suite.multiplier,
            formula));
    }
    return output;
}

bool ValidateCpuReference(std::string& error) {
    const Pixel input{ 1.0, 2.0, 3.0, 4.0 };
    const Pixel addend{ 5.0, 6.0, 7.0, 8.0 };
    const Pixel multiplier{ 2.0, 3.0, 4.0, 5.0 };
    struct Anchor {
        Formula formula;
        Pixel expected;
    };
    const Anchor anchors[] = {
        { Formula::Identity, Pixel{ 1.0, 2.0, 3.0, 4.0 } },
        { Formula::Add, Pixel{ 6.0, 8.0, 10.0, 12.0 } },
        { Formula::Multiply, Pixel{ 2.0, 6.0, 12.0, 20.0 } },
        { Formula::AddThenMultiply, Pixel{ 12.0, 24.0, 40.0, 60.0 } },
        { Formula::MultiplyThenAdd, Pixel{ 7.0, 12.0, 19.0, 28.0 } },
    };

    for (const Anchor& anchor : anchors) {
        const Pixel actual = EvaluatePixel(
            input,
            addend,
            multiplier,
            anchor.formula);
        if (!PixelsMatch(
                actual,
                anchor.expected,
                1.0e-12,
                error,
                FormulaName(anchor.formula))) {
            return false;
        }
    }

    const GeneratedSuite suite = BuildGeneratedSuite();
    const std::vector<Pixel> addThenMultiply =
        EvaluateReference(suite, Formula::AddThenMultiply);
    const std::vector<Pixel> multiplyThenAdd =
        EvaluateReference(suite, Formula::MultiplyThenAdd);
    if (MaximumAbsoluteDifference(addThenMultiply, multiplyThenAdd) < 0.5) {
        error = "Generated CPU cases do not distinguish Add->Multiply from Multiply->Add.";
        return false;
    }
    return true;
}

bool RunGpuSuite(
    const GeneratedSuite& suite,
    GpuSuiteResult& result,
    std::string& error) {
    result = {};
    if (suite.input.empty()) {
        error = "GPU suite requires at least one generated input pixel.";
        return false;
    }

    g_LastGlfwError.clear();
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        error = g_LastGlfwError.empty() ? "glfwInit failed." : g_LastGlfwError;
        return false;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(
        16,
        16,
        "Stack Node Math Reference",
        nullptr,
        nullptr);
    if (!window) {
        error = g_LastGlfwError.empty()
            ? "Unable to create a hidden OpenGL 4.3 context."
            : g_LastGlfwError;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        error = "OpenGL 4.3 function loading failed; see the preceding GLLoader diagnostics.";
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }

    result.vendor = GlString(GL_VENDOR);
    result.renderer = GlString(GL_RENDERER);
    result.version = GlString(GL_VERSION);
    result.width = static_cast<int>(suite.input.size());
    result.height = 1;
    result.sourceTargetBytes =
        suite.input.size() * 4u * sizeof(float);
    result.outputTargetBytes = result.sourceTargetBytes;

    GLuint sourceTexture = 0;
    GLuint outputTexture = 0;
    GLuint framebuffer = 0;
    GLuint vertexArray = 0;
    GLuint program = 0;
    auto cleanup = [&]() {
        if (program != 0) glDeleteProgram(program);
        if (vertexArray != 0) glDeleteVertexArrays(1, &vertexArray);
        if (framebuffer != 0) glDeleteFramebuffers(1, &framebuffer);
        if (outputTexture != 0) glDeleteTextures(1, &outputTexture);
        if (sourceTexture != 0) glDeleteTextures(1, &sourceTexture);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    };

    std::vector<float> sourceFloats;
    sourceFloats.reserve(suite.input.size() * 4u);
    for (const Pixel& pixel : suite.input) {
        for (double channel : pixel) {
            sourceFloats.push_back(static_cast<float>(channel));
        }
    }

    DrainGlErrors();
    glGenTextures(1, &sourceTexture);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA32F,
        result.width,
        result.height,
        0,
        GL_RGBA,
        GL_FLOAT,
        sourceFloats.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!CheckGl("generated RGBA32F source upload", error)) {
        cleanup();
        return false;
    }

    glGenTextures(1, &outputTexture);
    glBindTexture(GL_TEXTURE_2D, outputTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA32F,
        result.width,
        result.height,
        0,
        GL_RGBA,
        GL_FLOAT,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!CheckGl("RGBA32F output allocation", error)) {
        cleanup();
        return false;
    }

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        outputTexture,
        0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        error = "RGBA32F verification framebuffer is incomplete.";
        cleanup();
        return false;
    }
    if (!CheckGl("verification framebuffer setup", error)) {
        cleanup();
        return false;
    }

    static const char* vertexShader = R"glsl(
        #version 430 core
        void main() {
            const vec2 positions[3] = vec2[3](
                vec2(-1.0, -1.0),
                vec2( 3.0, -1.0),
                vec2(-1.0,  3.0));
            gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
        }
    )glsl";
    static const char* fragmentShader = R"glsl(
        #version 430 core
        layout(location = 0) out vec4 FragColor;
        uniform sampler2D uInput;
        uniform vec4 uAddend;
        uniform vec4 uMultiplier;
        uniform int uFormula;
        void main() {
            vec4 value = texelFetch(uInput, ivec2(gl_FragCoord.xy), 0);
            if (uFormula == 1) {
                value = value + uAddend;
            } else if (uFormula == 2) {
                value = value * uMultiplier;
            } else if (uFormula == 3) {
                value = (value + uAddend) * uMultiplier;
            } else if (uFormula == 4) {
                value = value * uMultiplier + uAddend;
            }
            FragColor = value;
        }
    )glsl";

    program = GLHelpers::CreateShaderProgram(vertexShader, fragmentShader);
    if (program == 0) {
        error = "Node-math verification shader failed to compile or link; see GLHelpers diagnostics.";
        cleanup();
        return false;
    }

    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(glGetUniformLocation(program, "uInput"), 0);
    glUniform4f(
        glGetUniformLocation(program, "uAddend"),
        static_cast<float>(suite.addend[0]),
        static_cast<float>(suite.addend[1]),
        static_cast<float>(suite.addend[2]),
        static_cast<float>(suite.addend[3]));
    glUniform4f(
        glGetUniformLocation(program, "uMultiplier"),
        static_cast<float>(suite.multiplier[0]),
        static_cast<float>(suite.multiplier[1]),
        static_cast<float>(suite.multiplier[2]),
        static_cast<float>(suite.multiplier[3]));
    glViewport(0, 0, result.width, result.height);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (!CheckGl("shader and uniform setup", error)) {
        cleanup();
        return false;
    }

    for (Formula formula : RequiredFormulas()) {
        glUniform1i(
            glGetUniformLocation(program, "uFormula"),
            static_cast<int>(formula));
        DrainGlErrors();
        glFinish();
        const auto started = std::chrono::steady_clock::now();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        const auto finished = std::chrono::steady_clock::now();
        if (!CheckGl(FormulaName(formula), error)) {
            cleanup();
            return false;
        }

        std::vector<float> outputFloats(suite.input.size() * 4u, 0.0f);
        glReadPixels(
            0,
            0,
            result.width,
            result.height,
            GL_RGBA,
            GL_FLOAT,
            outputFloats.data());
        if (!CheckGl("exact RGBA32F readback", error)) {
            cleanup();
            return false;
        }

        GpuCaseResult caseResult;
        caseResult.formula = formula;
        caseResult.synchronizedPassMilliseconds =
            std::chrono::duration<double, std::milli>(finished - started).count();
        caseResult.pixels.resize(suite.input.size());
        for (std::size_t pixelIndex = 0; pixelIndex < suite.input.size(); ++pixelIndex) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                caseResult.pixels[pixelIndex][channel] =
                    static_cast<double>(outputFloats[pixelIndex * 4u + channel]);
            }
        }
        result.cases.push_back(std::move(caseResult));
    }

    cleanup();
    return true;
}

double MaximumAbsoluteDifference(
    const std::vector<Pixel>& left,
    const std::vector<Pixel>& right) {
    if (left.size() != right.size()) {
        return std::numeric_limits<double>::infinity();
    }
    double maximum = 0.0;
    for (std::size_t pixelIndex = 0; pixelIndex < left.size(); ++pixelIndex) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            maximum = std::max(
                maximum,
                std::abs(left[pixelIndex][channel] - right[pixelIndex][channel]));
        }
    }
    return maximum;
}

} // namespace Stack::NodeMathReference
