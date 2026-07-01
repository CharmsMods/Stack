#include "App/Validation/ValidationImageUtils.h"

#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>
#include <cmath>

namespace Stack::Validation {
namespace {

struct ScopedFramebufferState {
    GLint framebuffer = 0;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint readBuffer = 0;
    GLint drawBuffer = 0;

    ScopedFramebufferState() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    }

    void Restore() const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glReadBuffer(static_cast<GLenum>(readBuffer));
        glDrawBuffer(static_cast<GLenum>(drawBuffer));
    }
};

} // namespace

std::size_t CountPixelsWithNonZeroAlpha(const std::vector<unsigned char>& pixels) {
    std::size_t count = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            ++count;
        }
    }
    return count;
}

std::size_t CountPixelsWithNonZeroRgb(const std::vector<unsigned char>& pixels) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0) {
            ++count;
        }
    }
    return count;
}

float ComputeAverageNormalizedLuma(const std::vector<unsigned char>& pixels) {
    if (pixels.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
        const float r = static_cast<float>(pixels[i + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[i + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[i + 2]) / 255.0f;
        sum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        ++count;
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

ValidationColorStats ComputeValidationColorStats(const std::vector<unsigned char>& pixels) {
    ValidationColorStats stats;
    if (pixels.empty()) {
        return stats;
    }

    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    double chromaSum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
        const float r = static_cast<float>(pixels[i + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[i + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[i + 2]) / 255.0f;
        const float pixelMax = (std::max)({ r, g, b });
        const float pixelMin = (std::min)({ r, g, b });
        sumR += r;
        sumG += g;
        sumB += b;
        chromaSum += pixelMax - pixelMin;
        ++count;
    }

    if (count == 0) {
        return stats;
    }

    const float invCount = 1.0f / static_cast<float>(count);
    stats.avgR = static_cast<float>(sumR) * invCount;
    stats.avgG = static_cast<float>(sumG) * invCount;
    stats.avgB = static_cast<float>(sumB) * invCount;
    stats.avgLuma = 0.2126f * stats.avgR + 0.7152f * stats.avgG + 0.0722f * stats.avgB;
    stats.avgPixelChroma = static_cast<float>(chromaSum) * invCount;
    const float maxAvg = (std::max)({ stats.avgR, stats.avgG, stats.avgB });
    const float minAvg = (std::min)({ stats.avgR, stats.avgG, stats.avgB });
    stats.channelSpread = maxAvg - minAvg;
    stats.channelRatio = maxAvg / (std::max)(0.0001f, minAvg);
    const float safeLuma = (std::max)(0.0001f, stats.avgLuma);
    stats.warmCoolBias = (stats.avgR - stats.avgB) / safeLuma;
    stats.magentaGreenBias = (((stats.avgR + stats.avgB) * 0.5f) - stats.avgG) / safeLuma;
    stats.biasRisk = std::clamp(
        (stats.channelRatio - 1.35f) / 1.15f +
            (std::abs(stats.warmCoolBias) + std::abs(stats.magentaGreenBias)) * 0.20f,
        0.0f,
        1.0f);
    return stats;
}

ValidationFineNoiseStats ComputeValidationFineNoiseStats(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    ValidationFineNoiseStats stats;
    if (pixels.empty() || width < 2 || height < 2) {
        return stats;
    }

    auto sample = [&](int x, int y, int channel) {
        const std::size_t index = static_cast<std::size_t>((y * width + x) * 4 + channel);
        return static_cast<float>(pixels[index]) / 255.0f;
    };
    auto lumaAt = [&](int x, int y) {
        return 0.2126f * sample(x, y, 0) + 0.7152f * sample(x, y, 1) + 0.0722f * sample(x, y, 2);
    };
    auto chromaAt = [&](int x, int y) {
        const float r = sample(x, y, 0);
        const float g = sample(x, y, 1);
        const float b = sample(x, y, 2);
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float cr = r - luma;
        const float cg = g - luma;
        const float cb = b - luma;
        return std::sqrt(cr * cr + cg * cg + cb * cb);
    };

    double lumaDiff = 0.0;
    double chromaDiff = 0.0;
    std::size_t count = 0;
    for (int y = 0; y + 1 < height; ++y) {
        for (int x = 0; x + 1 < width; ++x) {
            const float centerLuma = lumaAt(x, y);
            const float centerChroma = chromaAt(x, y);
            const float rightLuma = lumaAt(x + 1, y);
            const float downLuma = lumaAt(x, y + 1);
            const float rightChroma = chromaAt(x + 1, y);
            const float downChroma = chromaAt(x, y + 1);
            lumaDiff += (std::abs(centerLuma - rightLuma) + std::abs(centerLuma - downLuma)) * 0.5;
            chromaDiff += (std::abs(centerChroma - rightChroma) + std::abs(centerChroma - downChroma)) * 0.5;
            ++count;
        }
    }

    if (count > 0) {
        const float invCount = 1.0f / static_cast<float>(count);
        stats.lumaHighFrequency = static_cast<float>(lumaDiff) * invCount;
        stats.chromaHighFrequency = static_cast<float>(chromaDiff) * invCount;
        stats.combined = stats.lumaHighFrequency + stats.chromaHighFrequency * 1.4f;
    }
    return stats;
}

std::vector<float> ReadTextureRgbaFloat(unsigned int texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) {
        return {};
    }

    std::vector<float> pixels(static_cast<std::size_t>(width * height * 4), 0.0f);
    const ScopedFramebufferState savedState;

    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    const bool readOk = glGetError() == GL_NO_ERROR;

    savedState.Restore();
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
    }

    return readOk ? pixels : std::vector<float>{};
}

float ReadTextureMaxRgb(unsigned int texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) {
        return 0.0f;
    }

    std::vector<float> pixels(static_cast<std::size_t>(width * height * 4), 0.0f);
    const ScopedFramebufferState savedState;

    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    const bool readOk = glGetError() == GL_NO_ERROR;

    savedState.Restore();
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
    }

    if (!readOk) {
        return 0.0f;
    }

    float maxRgb = 0.0f;
    for (std::size_t i = 0; i + 2 < pixels.size(); i += 4) {
        const float r = std::isfinite(pixels[i + 0]) ? (std::max)(0.0f, pixels[i + 0]) : 0.0f;
        const float g = std::isfinite(pixels[i + 1]) ? (std::max)(0.0f, pixels[i + 1]) : 0.0f;
        const float b = std::isfinite(pixels[i + 2]) ? (std::max)(0.0f, pixels[i + 2]) : 0.0f;
        maxRgb = (std::max)(maxRgb, (std::max)(r, (std::max)(g, b)));
    }
    return maxRgb;
}

std::filesystem::path ResolveValidationInputPath(const char* rawPath) {
    std::filesystem::path path(rawPath ? rawPath : "");
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return path;
    }
#ifdef _WIN32
    const std::filesystem::path fromWorkspace = std::filesystem::current_path(ec).parent_path() / path;
    if (!ec && std::filesystem::exists(fromWorkspace, ec)) {
        return fromWorkspace;
    }
#endif
    return path;
}

std::string SanitizeValidationFileStem(std::string value) {
    for (char& ch : value) {
        const bool keep =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' ||
            ch == '_';
        if (!keep) {
            ch = '_';
        }
    }
    return value.empty() ? std::string("raw") : value;
}

bool WriteValidationPng(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    if (path.empty() || pixels.empty() || width <= 0 || height <= 0) {
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    std::vector<unsigned char> displayPixels = pixels;
    for (std::size_t i = 0; i + 2 < displayPixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const float linear = static_cast<float>(displayPixels[i + static_cast<std::size_t>(c)]) / 255.0f;
            const float encoded = linear <= 0.0031308f
                ? linear * 12.92f
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            displayPixels[i + static_cast<std::size_t>(c)] =
                static_cast<unsigned char>(std::clamp(std::lround(encoded * 255.0f), 0l, 255l));
        }
    }
    const std::filesystem::path tempPath =
        path.parent_path() / (path.filename().string() + ".tmp.png");
    std::filesystem::remove(tempPath, ec);
    ec.clear();
    if (stbi_write_png(tempPath.string().c_str(), width, height, 4, displayPixels.data(), width * 4) == 0) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    std::filesystem::remove(path, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

} // namespace Stack::Validation
