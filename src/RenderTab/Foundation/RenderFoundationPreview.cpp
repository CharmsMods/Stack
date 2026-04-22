#include "RenderFoundationPreview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace RenderFoundation {

namespace {

struct CameraBasis {
    Vec3 forward {};
    Vec3 right {};
    Vec3 up {};
};

CameraBasis BuildCameraBasis(const Camera& camera) {
    CameraBasis basis;
    basis.forward = ForwardFromYawPitch(camera.yawDegrees, camera.pitchDegrees);
    basis.right = RightFromForward(basis.forward);
    basis.up = UpFromBasis(basis.right, basis.forward);
    return basis;
}

bool ProjectPoint(
    const Camera& camera,
    const CameraBasis& basis,
    float width,
    float height,
    const Vec3& worldPoint,
    Vec2& outScreen,
    float& outDepth) {

    const Vec3 relative = worldPoint - camera.position;
    const float cameraX = Dot(relative, basis.right);
    const float cameraY = Dot(relative, basis.up);
    const float cameraZ = Dot(relative, basis.forward);
    if (cameraZ <= 0.1f) {
        return false;
    }

    const float focalLength = (0.5f * height) / std::tan(DegreesToRadians(camera.fieldOfViewDegrees) * 0.5f);
    outScreen.x = width * 0.5f + (cameraX / cameraZ) * focalLength;
    outScreen.y = height * 0.5f - (cameraY / cameraZ) * focalLength;
    outDepth = cameraZ;
    return true;
}

Vec3 ResolvePrimitiveColor(const State& state, const Primitive& primitive) {
    const Material* material = state.FindMaterial(primitive.materialId);
    if (material == nullptr) {
        return { 0.8f, 0.8f, 0.8f };
    }

    Vec3 color = material->baseColor;
    if (material->baseMaterial == BaseMaterial::Metal) {
        color = color * 0.95f + Vec3 { 0.05f, 0.05f, 0.05f };
    } else if (material->baseMaterial == BaseMaterial::Glass) {
        color = color * 0.72f + Vec3 { 0.16f, 0.18f, 0.2f };
    } else if (material->baseMaterial == BaseMaterial::Emissive) {
        color = Clamp01(material->emissionColor);
    }
    return Clamp01(color);
}

PreviewGlyph ResolveGlyph(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Sphere:
            return PreviewGlyph::Sphere;
        case PrimitiveType::Cube:
            return PreviewGlyph::Cube;
        case PrimitiveType::Plane:
            return PreviewGlyph::Plane;
    }
    return PreviewGlyph::Sphere;
}

PreviewGlyph ResolveGlyph(LightType type) {
    switch (type) {
        case LightType::Point:
            return PreviewGlyph::LightPoint;
        case LightType::Spot:
            return PreviewGlyph::LightSpot;
        case LightType::Area:
            return PreviewGlyph::LightArea;
        case LightType::Directional:
            return PreviewGlyph::LightDirectional;
    }
    return PreviewGlyph::LightPoint;
}

std::uint8_t ToByte(float value) {
    return static_cast<std::uint8_t>(std::round(Clamp01(value) * 255.0f));
}

void BlendPixel(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    int x,
    int y,
    const Vec3& color,
    float alpha) {

    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }

    const int index = (y * width + x) * 4;
    const float blend = Clamp01(alpha);
    const float inverse = 1.0f - blend;
    pixels[index + 0] = static_cast<unsigned char>(pixels[index + 0] * inverse + ToByte(color.x) * blend);
    pixels[index + 1] = static_cast<unsigned char>(pixels[index + 1] * inverse + ToByte(color.y) * blend);
    pixels[index + 2] = static_cast<unsigned char>(pixels[index + 2] * inverse + ToByte(color.z) * blend);
    pixels[index + 3] = 255;
}

void DrawFilledRect(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    int left,
    int top,
    int right,
    int bottom,
    const Vec3& color,
    float alpha) {

    const int clampedLeft = std::max(0, left);
    const int clampedTop = std::max(0, top);
    const int clampedRight = std::min(width - 1, right);
    const int clampedBottom = std::min(height - 1, bottom);
    for (int y = clampedTop; y <= clampedBottom; ++y) {
        for (int x = clampedLeft; x <= clampedRight; ++x) {
            BlendPixel(pixels, width, height, x, y, color, alpha);
        }
    }
}

void DrawFilledCircle(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    int centerX,
    int centerY,
    int radius,
    const Vec3& color,
    float alpha) {

    const int radiusSquared = radius * radius;
    for (int y = centerY - radius; y <= centerY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            const int dx = x - centerX;
            const int dy = y - centerY;
            if ((dx * dx + dy * dy) <= radiusSquared) {
                BlendPixel(pixels, width, height, x, y, color, alpha);
            }
        }
    }
}

void DrawDiamond(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    int centerX,
    int centerY,
    int radius,
    const Vec3& color,
    float alpha) {

    for (int y = centerY - radius; y <= centerY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            const int dx = std::abs(x - centerX);
            const int dy = std::abs(y - centerY);
            if (dx + dy <= radius) {
                BlendPixel(pixels, width, height, x, y, color, alpha);
            }
        }
    }
}

void DrawPreviewElement(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    const ProjectedElement& element) {

    const int cx = static_cast<int>(std::round(element.center.x));
    const int cy = static_cast<int>(std::round(element.center.y));
    const int hx = static_cast<int>(std::round(element.halfSize.x));
    const int hy = static_cast<int>(std::round(element.halfSize.y));

    DrawFilledRect(
        pixels,
        width,
        height,
        cx - hx + 3,
        cy - hy + 4,
        cx + hx + 3,
        cy + hy + 4,
        { 0.0f, 0.0f, 0.0f },
        0.18f);

    switch (element.glyph) {
        case PreviewGlyph::Sphere:
            DrawFilledCircle(pixels, width, height, cx, cy, std::max(hx, hy), element.color, 0.96f);
            DrawFilledCircle(pixels, width, height, cx - std::max(2, hx / 3), cy - std::max(2, hy / 3), std::max(2, hx / 4), { 1.0f, 1.0f, 1.0f }, 0.18f);
            break;
        case PreviewGlyph::Cube:
            DrawFilledRect(pixels, width, height, cx - hx, cy - hy, cx + hx, cy + hy, element.color, 0.94f);
            break;
        case PreviewGlyph::Plane:
            DrawFilledRect(pixels, width, height, cx - hx, cy - hy, cx + hx, cy + hy, element.color, 0.7f);
            break;
        case PreviewGlyph::LightArea:
            DrawFilledRect(pixels, width, height, cx - hx, cy - hy, cx + hx, cy + hy, element.color, 0.9f);
            break;
        case PreviewGlyph::LightDirectional:
        case PreviewGlyph::LightPoint:
        case PreviewGlyph::LightSpot:
            DrawDiamond(pixels, width, height, cx, cy, std::max(hx, hy), element.color, 0.96f);
            break;
    }

    if (element.selected) {
        DrawFilledRect(
            pixels,
            width,
            height,
            cx - hx - 3,
            cy - hy - 3,
            cx + hx + 3,
            cy + hy + 3,
            { 1.0f, 0.94f, 0.65f },
            0.16f);
    }
}

std::uint32_t HashPixel(int x, int y, int seed) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 1973u;
    value ^= static_cast<std::uint32_t>(y) * 9277u;
    value ^= static_cast<std::uint32_t>(seed) * 26699u;
    value ^= (value << 6u);
    value ^= (value >> 11u);
    return value;
}

} // namespace

std::vector<ProjectedElement> BuildProjectedElements(const State& state, float width, float height) {
    std::vector<ProjectedElement> elements;
    if (width <= 8.0f || height <= 8.0f) {
        return elements;
    }

    const CameraBasis basis = BuildCameraBasis(state.GetCamera());
    const Selection selection = state.GetSelection();

    elements.reserve(state.GetPrimitives().size() + state.GetLights().size());

    for (const Primitive& primitive : state.GetPrimitives()) {
        if (!primitive.visible) {
            continue;
        }

        Vec2 projected {};
        float depth = 0.0f;
        if (!ProjectPoint(state.GetCamera(), basis, width, height, primitive.transform.translation, projected, depth)) {
            continue;
        }

        const float worldSize = std::max(0.65f, MaxComponent(primitive.transform.scale));
        const float focalLength = (0.5f * height) / std::tan(DegreesToRadians(state.GetCamera().fieldOfViewDegrees) * 0.5f);
        const float projectedRadius = std::clamp((worldSize / depth) * focalLength * 0.45f, 8.0f, 95.0f);

        ProjectedElement element;
        element.selectionType = SelectionType::Primitive;
        element.id = primitive.id;
        element.glyph = ResolveGlyph(primitive.type);
        element.color = ResolvePrimitiveColor(state, primitive);
        element.center = projected;
        element.depth = depth;
        element.label = primitive.name;
        element.selected = selection.type == SelectionType::Primitive && selection.id == primitive.id;

        switch (primitive.type) {
            case PrimitiveType::Sphere:
                element.halfSize = { projectedRadius, projectedRadius };
                break;
            case PrimitiveType::Cube:
                element.halfSize = { projectedRadius * 0.9f, projectedRadius * 0.9f };
                break;
            case PrimitiveType::Plane:
                element.halfSize = { projectedRadius * 1.7f, std::max(6.0f, projectedRadius * 0.25f) };
                break;
        }

        elements.push_back(std::move(element));
    }

    for (const Light& light : state.GetLights()) {
        if (!light.enabled) {
            continue;
        }

        const Vec3 lightPosition =
            light.type == LightType::Directional
                ? state.GetCamera().position + basis.forward * 6.0f + Vec3 { 0.0f, 2.4f, 0.0f }
                : light.transform.translation;

        Vec2 projected {};
        float depth = 0.0f;
        if (!ProjectPoint(state.GetCamera(), basis, width, height, lightPosition, projected, depth)) {
            continue;
        }

        const float projectedRadius = std::clamp((1.2f / depth) * height * 0.55f, 8.0f, 20.0f);

        ProjectedElement element;
        element.selectionType = SelectionType::Light;
        element.id = light.id;
        element.glyph = ResolveGlyph(light.type);
        element.color = Clamp01(light.color);
        element.center = projected;
        element.halfSize = { projectedRadius, projectedRadius };
        if (light.type == LightType::Area) {
            element.halfSize = { projectedRadius * 1.4f, projectedRadius };
        } else if (light.type == LightType::Directional) {
            element.halfSize = { projectedRadius * 1.15f, projectedRadius * 0.85f };
        }
        element.depth = depth;
        element.label = light.name;
        element.selected = selection.type == SelectionType::Light && selection.id == light.id;
        elements.push_back(std::move(element));
    }

    std::sort(elements.begin(), elements.end(), [](const ProjectedElement& a, const ProjectedElement& b) {
        return a.depth > b.depth;
    });

    return elements;
}

void BuildPreviewPixels(
    const State& state,
    int width,
    int height,
    int sampleCount,
    std::vector<unsigned char>& outPixels) {

    outPixels.assign(std::max(1, width * height * 4), 255);
    if (width <= 0 || height <= 0) {
        return;
    }

    const Vec3 fogColor = Clamp01(state.GetSceneMetadata().fogColor);
    const float environmentIntensity = ClampFloat(state.GetSceneMetadata().environmentIntensity, 0.25f, 2.0f);
    Vec3 topColor { 0.11f, 0.14f, 0.18f };
    topColor = topColor + (fogColor * 0.18f * environmentIntensity);
    Vec3 bottomColor { 0.04f, 0.05f, 0.07f };

    if (state.GetSettings().viewMode == ViewMode::PathTrace) {
        topColor = topColor + Vec3 { 0.03f, 0.02f, 0.015f };
    }

    for (int y = 0; y < height; ++y) {
        const float t = static_cast<float>(y) / std::max(1, height - 1);
        const Vec3 rowColor = {
            topColor.x * (1.0f - t) + bottomColor.x * t,
            topColor.y * (1.0f - t) + bottomColor.y * t,
            topColor.z * (1.0f - t) + bottomColor.z * t
        };

        for (int x = 0; x < width; ++x) {
            const float grid = ((x % 40) == 0 || (y % 40) == 0) ? 0.05f : 0.0f;
            const int index = (y * width + x) * 4;
            outPixels[index + 0] = ToByte(rowColor.x + grid);
            outPixels[index + 1] = ToByte(rowColor.y + grid);
            outPixels[index + 2] = ToByte(rowColor.z + grid);
            outPixels[index + 3] = 255;
        }
    }

    const auto elements = BuildProjectedElements(state, static_cast<float>(width), static_cast<float>(height));
    for (const ProjectedElement& element : elements) {
        DrawPreviewElement(outPixels, width, height, element);
    }

    if (state.GetSettings().viewMode == ViewMode::PathTrace) {
        const float noiseStrength = 0.16f / std::sqrt(std::max(1, sampleCount));
        if (noiseStrength > 0.0f) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::uint32_t hash = HashPixel(x, y, sampleCount + 17);
                    const float centeredNoise = (static_cast<float>(hash & 1023u) / 1023.0f) - 0.5f;
                    const float gain = centeredNoise * noiseStrength;
                    const int index = (y * width + x) * 4;
                    outPixels[index + 0] = ToByte((outPixels[index + 0] / 255.0f) + gain);
                    outPixels[index + 1] = ToByte((outPixels[index + 1] / 255.0f) + gain);
                    outPixels[index + 2] = ToByte((outPixels[index + 2] / 255.0f) + gain);
                }
            }
        }
    }
}

} // namespace RenderFoundation
