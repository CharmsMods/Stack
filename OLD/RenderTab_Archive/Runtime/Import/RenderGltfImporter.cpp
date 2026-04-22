#include "RenderGltfImporter.h"

#include "ThirdParty/json.hpp"
#include "ThirdParty/stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <utility>

namespace {

using json = nlohmann::json;

struct Matrix4 {
    std::array<float, 16> values {};
};

struct AccessorView {
    const unsigned char* data = nullptr;
    std::size_t stride = 0;
    int count = 0;
    int componentType = 0;
    int componentCount = 0;
    bool normalized = false;
};

constexpr std::uint32_t kGlbMagic = 0x46546C67;
constexpr std::uint32_t kGlbJsonChunkType = 0x4E4F534A;
constexpr std::uint32_t kGlbBinChunkType = 0x004E4942;

Matrix4 IdentityMatrix() {
    Matrix4 matrix {};
    matrix.values = { 1.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 1.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 1.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 1.0f };
    return matrix;
}

Matrix4 Multiply(const Matrix4& left, const Matrix4& right) {
    Matrix4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) {
                value += left.values[k * 4 + row] * right.values[column * 4 + k];
            }
            result.values[column * 4 + row] = value;
        }
    }
    return result;
}

Matrix4 TranslationMatrix(const RenderFloat3& translation) {
    Matrix4 matrix = IdentityMatrix();
    matrix.values[12] = translation.x;
    matrix.values[13] = translation.y;
    matrix.values[14] = translation.z;
    return matrix;
}

Matrix4 ScaleMatrix(const RenderFloat3& scale) {
    Matrix4 matrix = IdentityMatrix();
    matrix.values[0] = scale.x;
    matrix.values[5] = scale.y;
    matrix.values[10] = scale.z;
    return matrix;
}

Matrix4 QuaternionMatrix(float x, float y, float z, float w) {
    Matrix4 matrix = IdentityMatrix();
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    matrix.values[0] = 1.0f - 2.0f * (yy + zz);
    matrix.values[1] = 2.0f * (xy + wz);
    matrix.values[2] = 2.0f * (xz - wy);

    matrix.values[4] = 2.0f * (xy - wz);
    matrix.values[5] = 1.0f - 2.0f * (xx + zz);
    matrix.values[6] = 2.0f * (yz + wx);

    matrix.values[8] = 2.0f * (xz + wy);
    matrix.values[9] = 2.0f * (yz - wx);
    matrix.values[10] = 1.0f - 2.0f * (xx + yy);
    return matrix;
}

RenderFloat3 TransformPoint(const Matrix4& matrix, const RenderFloat3& point) {
    return MakeRenderFloat3(
        matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12],
        matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13],
        matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14]);
}

float ClampNormalizedInt(std::int32_t value, std::int32_t minValue, std::int32_t maxValue) {
    if (value <= minValue) {
        return -1.0f;
    }
    if (value >= maxValue) {
        return 1.0f;
    }
    return static_cast<float>(value) / static_cast<float>(maxValue);
}

RenderFloat3 ExtractScale(const Matrix4& matrix) {
    return MakeRenderFloat3(
        Length(MakeRenderFloat3(matrix.values[0], matrix.values[1], matrix.values[2])),
        Length(MakeRenderFloat3(matrix.values[4], matrix.values[5], matrix.values[6])),
        Length(MakeRenderFloat3(matrix.values[8], matrix.values[9], matrix.values[10])));
}

RenderFloat3 ExtractEulerDegrees(const Matrix4& matrix, const RenderFloat3& scale) {
    const float sx = std::max(scale.x, 0.0001f);
    const float sy = std::max(scale.y, 0.0001f);
    const float sz = std::max(scale.z, 0.0001f);

    const float r00 = matrix.values[0] / sx;
    const float r10 = matrix.values[1] / sx;
    const float r20 = matrix.values[2] / sx;
    const float r11 = matrix.values[5] / sy;
    const float r12 = matrix.values[9] / sz;
    const float r21 = matrix.values[6] / sy;
    const float r22 = matrix.values[10] / sz;

    float xRadians = 0.0f;
    float yRadians = 0.0f;
    float zRadians = 0.0f;
    const float sineY = -std::clamp(r20, -1.0f, 1.0f);
    if (std::fabs(sineY) < 0.9999f) {
        yRadians = std::asin(sineY);
        xRadians = std::atan2(r21, r22);
        zRadians = std::atan2(r10, r00);
    } else {
        yRadians = sineY > 0.0f ? 1.57079632679f : -1.57079632679f;
        xRadians = std::atan2(-r12, r11);
        zRadians = 0.0f;
    }

    const float degrees = 57.2957795131f;
    return MakeRenderFloat3(xRadians * degrees, yRadians * degrees, zRadians * degrees);
}

RenderTransform DecomposeTransform(const Matrix4& matrix) {
    RenderTransform transform;
    transform.translation = MakeRenderFloat3(matrix.values[12], matrix.values[13], matrix.values[14]);
    transform.scale = ExtractScale(matrix);
    transform.rotationDegrees = ExtractEulerDegrees(matrix, transform.scale);
    return transform;
}

bool ReadFileBinary(const std::filesystem::path& path, std::vector<unsigned char>& bytes) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        return false;
    }
    stream.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    return stream.good() || stream.eof();
}

bool IsDataUri(const std::string& uri) {
    return uri.rfind("data:", 0) == 0;
}

int DecodeBase64Char(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

bool DecodeBase64(const std::string& encoded, std::vector<unsigned char>& output) {
    output.clear();
    int buffer = 0;
    int bits = 0;
    for (char ch : encoded) {
        if (ch == '=') {
            break;
        }
        const int value = DecodeBase64Char(ch);
        if (value < 0) {
            continue;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        while (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
        }
    }
    return !output.empty();
}

bool DecodeDataUri(const std::string& uri, std::vector<unsigned char>& output) {
    const std::size_t comma = uri.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    return DecodeBase64(uri.substr(comma + 1), output);
}

bool ParseGlb(
    const std::vector<unsigned char>& fileBytes,
    std::string& jsonText,
    std::vector<unsigned char>& binChunk,
    std::string& errorMessage) {
    if (fileBytes.size() < 20) {
        errorMessage = "The .glb file is too small to contain a valid header.";
        return false;
    }

    const auto readU32 = [&](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(fileBytes[offset + 0]) |
            (static_cast<std::uint32_t>(fileBytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(fileBytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(fileBytes[offset + 3]) << 24);
    };

    if (readU32(0) != kGlbMagic) {
        errorMessage = "The .glb file has an invalid magic header.";
        return false;
    }

    if (readU32(4) != 2) {
        errorMessage = "Only glTF 2.0 binary containers are supported.";
        return false;
    }

    const std::uint32_t totalLength = readU32(8);
    if (totalLength > fileBytes.size()) {
        errorMessage = "The .glb file is truncated.";
        return false;
    }

    std::size_t cursor = 12;
    while (cursor + 8 <= totalLength) {
        const std::uint32_t chunkLength = readU32(cursor);
        const std::uint32_t chunkType = readU32(cursor + 4);
        cursor += 8;
        if (cursor + chunkLength > totalLength) {
            errorMessage = "The .glb file contains an invalid chunk length.";
            return false;
        }

        if (chunkType == kGlbJsonChunkType) {
            jsonText.assign(reinterpret_cast<const char*>(fileBytes.data() + cursor), chunkLength);
        } else if (chunkType == kGlbBinChunkType) {
            binChunk.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(cursor), fileBytes.begin() + static_cast<std::ptrdiff_t>(cursor + chunkLength));
        }

        cursor += chunkLength;
    }

    if (jsonText.empty()) {
        errorMessage = "The .glb file is missing its JSON chunk.";
        return false;
    }
    return true;
}

int ComponentCountFromType(const std::string& type) {
    if (type == "SCALAR") {
        return 1;
    }
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    return 0;
}

int ComponentSize(int componentType) {
    switch (componentType) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

bool GetAccessorView(
    const json& root,
    const std::vector<std::vector<unsigned char>>& buffers,
    int accessorIndex,
    AccessorView& view,
    std::string& errorMessage) {
    if (accessorIndex < 0 || !root.contains("accessors") || accessorIndex >= static_cast<int>(root["accessors"].size())) {
        errorMessage = "A glTF accessor reference was out of range.";
        return false;
    }

    const json& accessor = root["accessors"][accessorIndex];
    const int bufferViewIndex = accessor.value("bufferView", -1);
    if (bufferViewIndex < 0 || !root.contains("bufferViews") || bufferViewIndex >= static_cast<int>(root["bufferViews"].size())) {
        errorMessage = "A glTF accessor is missing its bufferView.";
        return false;
    }

    const json& bufferView = root["bufferViews"][bufferViewIndex];
    const int bufferIndex = bufferView.value("buffer", -1);
    if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size())) {
        errorMessage = "A glTF bufferView references a missing buffer.";
        return false;
    }

    const int componentCount = ComponentCountFromType(accessor.value("type", ""));
    const int componentSize = ComponentSize(accessor.value("componentType", 0));
    if (componentCount <= 0 || componentSize <= 0) {
        errorMessage = "A glTF accessor uses an unsupported type/component format.";
        return false;
    }

    const std::size_t accessorOffset = static_cast<std::size_t>(accessor.value("byteOffset", 0));
    const std::size_t bufferViewOffset = static_cast<std::size_t>(bufferView.value("byteOffset", 0));
    const std::size_t stride = static_cast<std::size_t>(bufferView.value("byteStride", componentCount * componentSize));
    const int count = accessor.value("count", 0);
    if (count <= 0) {
        errorMessage = "A glTF accessor has no elements.";
        return false;
    }

    const std::vector<unsigned char>& buffer = buffers[static_cast<std::size_t>(bufferIndex)];
    const std::size_t start = bufferViewOffset + accessorOffset;
    const std::size_t required = start + stride * static_cast<std::size_t>(count - 1) + static_cast<std::size_t>(componentCount * componentSize);
    if (required > buffer.size()) {
        errorMessage = "A glTF accessor extends past the end of its source buffer.";
        return false;
    }

    view.data = buffer.data() + start;
    view.stride = stride;
    view.count = count;
    view.componentType = accessor.value("componentType", 0);
    view.componentCount = componentCount;
    view.normalized = accessor.value("normalized", false);
    return true;
}

double ReadAccessorComponent(const AccessorView& view, int elementIndex, int componentIndex) {
    const unsigned char* element = view.data + view.stride * static_cast<std::size_t>(elementIndex);
    const unsigned char* source = element + ComponentSize(view.componentType) * static_cast<std::size_t>(componentIndex);
    switch (view.componentType) {
    case 5120:
    {
        const std::int8_t value = *reinterpret_cast<const std::int8_t*>(source);
        return view.normalized ? ClampNormalizedInt(value, -128, 127) : static_cast<double>(value);
    }
    case 5121:
    {
        const std::uint8_t value = *reinterpret_cast<const std::uint8_t*>(source);
        return view.normalized ? static_cast<double>(value) / 255.0 : static_cast<double>(value);
    }
    case 5122:
    {
        const std::int16_t value = *reinterpret_cast<const std::int16_t*>(source);
        return view.normalized ? ClampNormalizedInt(value, -32768, 32767) : static_cast<double>(value);
    }
    case 5123:
    {
        const std::uint16_t value = *reinterpret_cast<const std::uint16_t*>(source);
        return view.normalized ? static_cast<double>(value) / 65535.0 : static_cast<double>(value);
    }
    case 5125:
        return static_cast<double>(*reinterpret_cast<const std::uint32_t*>(source));
    case 5126:
        return static_cast<double>(*reinterpret_cast<const float*>(source));
    default:
        return 0.0;
    }
}

RenderFloat3 ReadFloat3(const AccessorView& view, int index) {
    return MakeRenderFloat3(
        static_cast<float>(ReadAccessorComponent(view, index, 0)),
        static_cast<float>(ReadAccessorComponent(view, index, 1)),
        static_cast<float>(ReadAccessorComponent(view, index, 2)));
}

RenderFloat2 ReadFloat2(const AccessorView& view, int index) {
    return MakeRenderFloat2(
        static_cast<float>(ReadAccessorComponent(view, index, 0)),
        static_cast<float>(ReadAccessorComponent(view, index, 1)));
}

std::uint32_t ReadIndex(const AccessorView& view, int index) {
    return static_cast<std::uint32_t>(ReadAccessorComponent(view, index, 0));
}

bool LoadImagePixels(
    const json& image,
    const json& root,
    const std::filesystem::path& assetPath,
    const std::vector<std::vector<unsigned char>>& buffers,
    std::vector<unsigned char>& pixels,
    int& width,
    int& height,
    RenderImportedTextureSourceKind& sourceKind,
    std::string& sourcePath,
    std::string& sourceUri,
    std::string& errorMessage) {
    std::vector<unsigned char> encoded;
    sourceKind = RenderImportedTextureSourceKind::None;
    sourcePath.clear();
    sourceUri.clear();

    if (image.contains("uri")) {
        sourceUri = image["uri"].get<std::string>();
        if (IsDataUri(sourceUri)) {
            if (!DecodeDataUri(sourceUri, encoded)) {
                errorMessage = "Failed to decode an embedded data URI image.";
                return false;
            }
            sourceKind = RenderImportedTextureSourceKind::EmbeddedAssetImage;
            sourcePath = assetPath.string();
        } else {
            const std::filesystem::path resolvedPath = assetPath.parent_path() / std::filesystem::path(sourceUri);
            if (!ReadFileBinary(resolvedPath, encoded)) {
                errorMessage = std::string("Failed to read image file: ") + resolvedPath.string();
                return false;
            }
            sourceKind = RenderImportedTextureSourceKind::ExternalFile;
            sourcePath = resolvedPath.string();
        }
    } else {
        const int bufferViewIndex = image.value("bufferView", -1);
        if (bufferViewIndex < 0 || !root.contains("bufferViews") || bufferViewIndex >= static_cast<int>(root["bufferViews"].size())) {
            errorMessage = "An image entry is missing both uri and bufferView.";
            return false;
        }

        const json& bufferView = root["bufferViews"][bufferViewIndex];
        const int bufferIndex = bufferView.value("buffer", -1);
        if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size())) {
            errorMessage = "An image bufferView references a missing buffer.";
            return false;
        }

        const std::size_t offset = static_cast<std::size_t>(bufferView.value("byteOffset", 0));
        const std::size_t length = static_cast<std::size_t>(bufferView.value("byteLength", 0));
        const std::vector<unsigned char>& buffer = buffers[static_cast<std::size_t>(bufferIndex)];
        if (offset + length > buffer.size()) {
            errorMessage = "An image bufferView extends past the end of the buffer.";
            return false;
        }

        encoded.assign(buffer.begin() + static_cast<std::ptrdiff_t>(offset), buffer.begin() + static_cast<std::ptrdiff_t>(offset + length));
        sourceKind = RenderImportedTextureSourceKind::EmbeddedAssetImage;
        sourcePath = assetPath.string();
    }

    int channels = 0;
    unsigned char* decoded = stbi_load_from_memory(
        encoded.data(),
        static_cast<int>(encoded.size()),
        &width,
        &height,
        &channels,
        4);
    if (decoded == nullptr) {
        errorMessage = "Failed to decode an imported image into RGBA pixels.";
        return false;
    }

    pixels.assign(decoded, decoded + static_cast<std::size_t>(width * height * 4));
    stbi_image_free(decoded);
    return true;
}

bool LoadBuffers(
    const json& root,
    const std::filesystem::path& assetPath,
    const std::vector<unsigned char>& glbBinChunk,
    std::vector<std::vector<unsigned char>>& buffers,
    std::string& errorMessage) {
    buffers.clear();
    const json bufferArray = root.value("buffers", json::array());
    if (!bufferArray.is_array()) {
        errorMessage = "The glTF file contains an invalid buffers array.";
        return false;
    }

    buffers.reserve(bufferArray.size());
    for (std::size_t i = 0; i < bufferArray.size(); ++i) {
        const json& buffer = bufferArray[i];
        std::vector<unsigned char> bytes;
        if (buffer.contains("uri")) {
            const std::string uri = buffer["uri"].get<std::string>();
            if (IsDataUri(uri)) {
                if (!DecodeDataUri(uri, bytes)) {
                    errorMessage = "Failed to decode an embedded glTF buffer URI.";
                    return false;
                }
            } else {
                const std::filesystem::path resolvedPath = assetPath.parent_path() / std::filesystem::path(uri);
                if (!ReadFileBinary(resolvedPath, bytes)) {
                    errorMessage = std::string("Failed to read glTF buffer file: ") + resolvedPath.string();
                    return false;
                }
            }
        } else {
            bytes = glbBinChunk;
        }

        const std::size_t declaredLength = static_cast<std::size_t>(buffer.value("byteLength", 0));
        if (declaredLength > 0 && bytes.size() < declaredLength) {
            errorMessage = "A glTF buffer is shorter than its declared byteLength.";
            return false;
        }

        buffers.push_back(std::move(bytes));
    }

    return true;
}

RenderFloat3 JsonToFloat3(const json& value, const RenderFloat3& fallback) {
    if (!value.is_array() || value.size() != 3) {
        return fallback;
    }
    return MakeRenderFloat3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

RenderFloat2 JsonToFloat2(const json& value, const RenderFloat2& fallback) {
    if (!value.is_array() || value.size() != 2) {
        return fallback;
    }
    return MakeRenderFloat2(value[0].get<float>(), value[1].get<float>());
}

Matrix4 NodeLocalTransform(const json& node) {
    if (node.contains("matrix")) {
        const json& values = node["matrix"];
        if (values.is_array() && values.size() == 16) {
            Matrix4 matrix {};
            for (int i = 0; i < 16; ++i) {
                matrix.values[static_cast<std::size_t>(i)] = values[static_cast<std::size_t>(i)].get<float>();
            }
            return matrix;
        }
    }

    const RenderFloat3 translation = JsonToFloat3(node.value("translation", json::array()), MakeRenderFloat3(0.0f, 0.0f, 0.0f));
    const RenderFloat3 scale = JsonToFloat3(node.value("scale", json::array()), MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    const json rotationJson = node.value("rotation", json::array());
    float rx = 0.0f;
    float ry = 0.0f;
    float rz = 0.0f;
    float rw = 1.0f;
    if (rotationJson.is_array() && rotationJson.size() == 4) {
        rx = rotationJson[0].get<float>();
        ry = rotationJson[1].get<float>();
        rz = rotationJson[2].get<float>();
        rw = rotationJson[3].get<float>();
    }

    return Multiply(TranslationMatrix(translation), Multiply(QuaternionMatrix(rx, ry, rz, rw), ScaleMatrix(scale)));
}

std::string ResolveName(const json& value, const std::string& fallback) {
    const std::string name = value.value("name", "");
    return name.empty() ? fallback : name;
}

int AppendImportedTexture(
    const json& root,
    const std::filesystem::path& assetPath,
    const std::vector<std::vector<unsigned char>>& buffers,
    int assetIndex,
    int textureIndex,
    RenderTextureSemantic semantic,
    std::vector<RenderImportedTexture>& importedTextures,
    std::map<std::pair<int, int>, int>& textureCache,
    std::string& errorMessage) {
    const std::pair<int, int> cacheKey(textureIndex, static_cast<int>(semantic));
    const auto existing = textureCache.find(cacheKey);
    if (existing != textureCache.end()) {
        return existing->second;
    }

    if (!root.contains("textures") || textureIndex < 0 || textureIndex >= static_cast<int>(root["textures"].size())) {
        return -1;
    }

    const json& texture = root["textures"][textureIndex];
    const int imageIndex = texture.value("source", -1);
    if (!root.contains("images") || imageIndex < 0 || imageIndex >= static_cast<int>(root["images"].size())) {
        return -1;
    }

    const json& image = root["images"][imageIndex];
    RenderImportedTexture importedTexture;
    importedTexture.name = ResolveName(image, std::string("Texture ") + std::to_string(textureIndex));
    importedTexture.semantic = semantic;
    importedTexture.sourceAssetIndex = assetIndex;
    importedTexture.sourceImageIndex = imageIndex;

    if (!LoadImagePixels(
            image,
            root,
            assetPath,
            buffers,
            importedTexture.pixels,
            importedTexture.width,
            importedTexture.height,
            importedTexture.sourceKind,
            importedTexture.sourcePath,
            importedTexture.sourceUri,
            errorMessage)) {
        return -1;
    }

    const int importedTextureIndex = static_cast<int>(importedTextures.size());
    importedTextures.push_back(std::move(importedTexture));
    textureCache.emplace(cacheKey, importedTextureIndex);
    return importedTextureIndex;
}

bool AppendMaterial(
    const json& root,
    const json& materialValue,
    const std::filesystem::path& assetPath,
    const std::vector<std::vector<unsigned char>>& buffers,
    int assetIndex,
    int materialIndex,
    std::vector<RenderMaterial>& materials,
    std::vector<RenderImportedTexture>& importedTextures,
    std::map<std::pair<int, int>, int>& textureCache,
    std::string& errorMessage) {
    RenderMaterial material;
    material.name = ResolveName(materialValue, std::string("Material ") + std::to_string(materialIndex));
    material.sourceAssetIndex = assetIndex;
    material.sourceMaterialName = material.name;

    const json pbr = materialValue.value("pbrMetallicRoughness", json::object());
    material.baseColor = JsonToFloat3(pbr.value("baseColorFactor", json::array({ 1.0f, 1.0f, 1.0f, 1.0f })), MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    material.roughness = pbr.value("roughnessFactor", 1.0f);
    material.metallic = pbr.value("metallicFactor", 1.0f);
    material.emissionColor = JsonToFloat3(materialValue.value("emissiveFactor", json::array({ 0.0f, 0.0f, 0.0f })), MakeRenderFloat3(0.0f, 0.0f, 0.0f));
    material.emissionStrength =
        (material.emissionColor.x > 0.0001f || material.emissionColor.y > 0.0001f || material.emissionColor.z > 0.0001f)
        ? 1.0f
        : 0.0f;

    if (pbr.contains("baseColorTexture")) {
        const int textureIndex = pbr["baseColorTexture"].value("index", -1);
        const int importedTextureIndex = AppendImportedTexture(
            root,
            assetPath,
            buffers,
            assetIndex,
            textureIndex,
            RenderTextureSemantic::BaseColor,
            importedTextures,
            textureCache,
            errorMessage);
        if (!errorMessage.empty()) {
            return false;
        }
        material.baseColorTexture.textureIndex = importedTextureIndex;
        material.baseColorTexture.uvSet = pbr["baseColorTexture"].value("texCoord", 0);
    }

    if (pbr.contains("metallicRoughnessTexture")) {
        const int textureIndex = pbr["metallicRoughnessTexture"].value("index", -1);
        const int importedTextureIndex = AppendImportedTexture(
            root,
            assetPath,
            buffers,
            assetIndex,
            textureIndex,
            RenderTextureSemantic::MetallicRoughness,
            importedTextures,
            textureCache,
            errorMessage);
        if (!errorMessage.empty()) {
            return false;
        }
        material.metallicRoughnessTexture.textureIndex = importedTextureIndex;
        material.metallicRoughnessTexture.uvSet = pbr["metallicRoughnessTexture"].value("texCoord", 0);
    }

    if (materialValue.contains("emissiveTexture")) {
        const int textureIndex = materialValue["emissiveTexture"].value("index", -1);
        const int importedTextureIndex = AppendImportedTexture(
            root,
            assetPath,
            buffers,
            assetIndex,
            textureIndex,
            RenderTextureSemantic::Emissive,
            importedTextures,
            textureCache,
            errorMessage);
        if (!errorMessage.empty()) {
            return false;
        }
        material.emissiveTexture.textureIndex = importedTextureIndex;
        material.emissiveTexture.uvSet = materialValue["emissiveTexture"].value("texCoord", 0);
    }

    if (materialValue.contains("normalTexture")) {
        const int textureIndex = materialValue["normalTexture"].value("index", -1);
        const int importedTextureIndex = AppendImportedTexture(
            root,
            assetPath,
            buffers,
            assetIndex,
            textureIndex,
            RenderTextureSemantic::Normal,
            importedTextures,
            textureCache,
            errorMessage);
        if (!errorMessage.empty()) {
            return false;
        }
        material.normalTexture.textureIndex = importedTextureIndex;
        material.normalTexture.uvSet = materialValue["normalTexture"].value("texCoord", 0);
    }

    materials.push_back(std::move(material));
    return true;
}

bool BuildMeshDefinition(
    const json& root,
    const std::vector<std::vector<unsigned char>>& buffers,
    const json& meshValue,
    int meshIndex,
    int assetIndex,
    int defaultMaterialIndex,
    RenderMeshDefinition& meshDefinition,
    std::string& errorMessage) {
    std::vector<RenderMeshTriangle> triangles;
    const json primitiveArray = meshValue.value("primitives", json::array());
    if (!primitiveArray.is_array()) {
        errorMessage = "A glTF mesh contains an invalid primitives array.";
        return false;
    }

    for (std::size_t primitiveIndex = 0; primitiveIndex < primitiveArray.size(); ++primitiveIndex) {
        const json& primitive = primitiveArray[primitiveIndex];
        const int mode = primitive.value("mode", 4);
        if (mode != 4) {
            continue;
        }

        const json attributes = primitive.value("attributes", json::object());
        const int positionAccessorIndex = attributes.value("POSITION", -1);
        if (positionAccessorIndex < 0) {
            continue;
        }

        AccessorView positions;
        AccessorView normals;
        AccessorView uvs;
        if (!GetAccessorView(root, buffers, positionAccessorIndex, positions, errorMessage)) {
            return false;
        }

        const bool hasNormals = attributes.contains("NORMAL") &&
            GetAccessorView(root, buffers, attributes.value("NORMAL", -1), normals, errorMessage);
        if (attributes.contains("NORMAL") && !hasNormals && !errorMessage.empty()) {
            return false;
        }

        errorMessage.clear();
        const bool hasUvs = attributes.contains("TEXCOORD_0") &&
            GetAccessorView(root, buffers, attributes.value("TEXCOORD_0", -1), uvs, errorMessage);
        if (attributes.contains("TEXCOORD_0") && !hasUvs && !errorMessage.empty()) {
            return false;
        }
        errorMessage.clear();

        std::vector<std::uint32_t> indices;
        if (primitive.contains("indices")) {
            AccessorView indexAccessor;
            if (!GetAccessorView(root, buffers, primitive.value("indices", -1), indexAccessor, errorMessage)) {
                return false;
            }

            indices.reserve(static_cast<std::size_t>(indexAccessor.count));
            for (int index = 0; index < indexAccessor.count; ++index) {
                indices.push_back(ReadIndex(indexAccessor, index));
            }
        } else {
            indices.reserve(static_cast<std::size_t>(positions.count));
            for (int index = 0; index < positions.count; ++index) {
                indices.push_back(static_cast<std::uint32_t>(index));
            }
        }

        const int materialIndex = primitive.value("material", defaultMaterialIndex);
        const int resolvedMaterialIndex =
            materialIndex >= 0 && materialIndex < defaultMaterialIndex ? materialIndex : defaultMaterialIndex;
        const std::string trianglePrefix = ResolveName(
            primitive,
            ResolveName(meshValue, std::string("Mesh ") + std::to_string(meshIndex)) + " Primitive " + std::to_string(primitiveIndex));

        const std::size_t triangleCount = indices.size() / 3;
        for (std::size_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
            const std::uint32_t ia = indices[triangleIndex * 3 + 0];
            const std::uint32_t ib = indices[triangleIndex * 3 + 1];
            const std::uint32_t ic = indices[triangleIndex * 3 + 2];
            if (ia >= static_cast<std::uint32_t>(positions.count) ||
                ib >= static_cast<std::uint32_t>(positions.count) ||
                ic >= static_cast<std::uint32_t>(positions.count)) {
                continue;
            }

            RenderMeshTriangle triangle;
            triangle.name = trianglePrefix + " Triangle " + std::to_string(triangleIndex);
            triangle.localA = ReadFloat3(positions, static_cast<int>(ia));
            triangle.localB = ReadFloat3(positions, static_cast<int>(ib));
            triangle.localC = ReadFloat3(positions, static_cast<int>(ic));
            triangle.materialIndex = resolvedMaterialIndex;

            if (hasNormals) {
                triangle.localNormalA = ReadFloat3(normals, static_cast<int>(ia));
                triangle.localNormalB = ReadFloat3(normals, static_cast<int>(ib));
                triangle.localNormalC = ReadFloat3(normals, static_cast<int>(ic));
            } else {
                const RenderFloat3 edge1 = Subtract(triangle.localB, triangle.localA);
                const RenderFloat3 edge2 = Subtract(triangle.localC, triangle.localA);
                const RenderFloat3 faceNormal = Normalize(Cross(edge1, edge2));
                triangle.localNormalA = faceNormal;
                triangle.localNormalB = faceNormal;
                triangle.localNormalC = faceNormal;
            }

            if (hasUvs) {
                triangle.uvA = ReadFloat2(uvs, static_cast<int>(ia));
                triangle.uvB = ReadFloat2(uvs, static_cast<int>(ib));
                triangle.uvC = ReadFloat2(uvs, static_cast<int>(ic));
            }

            triangles.push_back(std::move(triangle));
        }
    }

    if (triangles.empty()) {
        return false;
    }

    meshDefinition = BuildRenderMeshDefinition(
        ResolveName(meshValue, std::string("Mesh ") + std::to_string(meshIndex)),
        std::move(triangles));
    meshDefinition.sourceAssetIndex = assetIndex;
    meshDefinition.sourceMeshName = meshDefinition.name;
    return true;
}

bool BuildSceneInstances(
    const json& root,
    const std::vector<int>& meshMap,
    std::vector<RenderMeshInstance>& meshInstances,
    std::string& errorMessage) {
    const json nodeArray = root.value("nodes", json::array());
    if (!nodeArray.is_array()) {
        errorMessage = "The glTF file contains an invalid nodes array.";
        return false;
    }

    std::function<bool(int, const Matrix4&)> processNode;
    processNode = [&](int nodeIndex, const Matrix4& parentTransform) -> bool {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodeArray.size())) {
            errorMessage = "A glTF scene references an out-of-range node.";
            return false;
        }

        const json& node = nodeArray[static_cast<std::size_t>(nodeIndex)];
        const Matrix4 worldTransform = Multiply(parentTransform, NodeLocalTransform(node));
        const int meshIndex = node.value("mesh", -1);
        if (meshIndex >= 0 && meshIndex < static_cast<int>(meshMap.size()) && meshMap[static_cast<std::size_t>(meshIndex)] >= 0) {
            RenderMeshInstance instance;
            instance.name = ResolveName(node, std::string("Imported Instance ") + std::to_string(meshInstances.size() + 1));
            instance.meshIndex = meshMap[static_cast<std::size_t>(meshIndex)];
            instance.transform = DecomposeTransform(worldTransform);
            meshInstances.push_back(std::move(instance));
        }

        const json children = node.value("children", json::array());
        if (!children.is_array()) {
            errorMessage = "A glTF node contains an invalid children array.";
            return false;
        }

        for (const json& childIndexValue : children) {
            if (!processNode(childIndexValue.get<int>(), worldTransform)) {
                return false;
            }
        }
        return true;
    };

    json rootNodes = json::array();
    const json sceneArray = root.value("scenes", json::array());
    if (sceneArray.is_array() && !sceneArray.empty()) {
        int sceneIndex = root.value("scene", 0);
        if (sceneIndex < 0 || sceneIndex >= static_cast<int>(sceneArray.size())) {
            sceneIndex = 0;
        }
        rootNodes = sceneArray[static_cast<std::size_t>(sceneIndex)].value("nodes", json::array());
    } else if (nodeArray.is_array()) {
        for (int i = 0; i < static_cast<int>(nodeArray.size()); ++i) {
            rootNodes.push_back(i);
        }
    }

    for (const json& rootNode : rootNodes) {
        if (!processNode(rootNode.get<int>(), IdentityMatrix())) {
            return false;
        }
    }

    return true;
}

} // namespace

namespace RenderGltfImporter {

bool ImportScene(
    const std::filesystem::path& path,
    RenderImportResult& result,
    std::string& errorMessage) {
    result = RenderImportResult();
    errorMessage.clear();

    std::vector<unsigned char> fileBytes;
    if (!ReadFileBinary(path, fileBytes)) {
        errorMessage = std::string("Failed to read glTF file: ") + path.string();
        return false;
    }

    std::string jsonText;
    std::vector<unsigned char> glbBinChunk;
    const bool isBinaryContainer = path.extension() == ".glb";
    if (isBinaryContainer) {
        if (!ParseGlb(fileBytes, jsonText, glbBinChunk, errorMessage)) {
            return false;
        }
    } else {
        jsonText.assign(reinterpret_cast<const char*>(fileBytes.data()), fileBytes.size());
    }

    json root;
    try {
        root = json::parse(jsonText);
    } catch (const std::exception& exception) {
        errorMessage = std::string("Failed to parse glTF JSON: ") + exception.what();
        return false;
    }

    std::vector<std::vector<unsigned char>> buffers;
    if (!LoadBuffers(root, path, glbBinChunk, buffers, errorMessage)) {
        return false;
    }

    const int assetIndex = 0;
    result.label = path.stem().string();
    result.description = std::string("Imported static glTF scene from ") + path.filename().string() + ".";
    result.backgroundMode = RenderBackgroundMode::Black;
    result.importedAssets.push_back(RenderImportedAsset { result.label, path.string(), isBinaryContainer });

    std::map<std::pair<int, int>, int> textureCache;
    const json materialArray = root.value("materials", json::array());
    if (materialArray.is_array()) {
        result.materials.reserve(materialArray.size() + 1);
        for (std::size_t i = 0; i < materialArray.size(); ++i) {
            if (!AppendMaterial(
                    root,
                    materialArray[i],
                    path,
                    buffers,
                    assetIndex,
                    static_cast<int>(i),
                    result.materials,
                    result.importedTextures,
                    textureCache,
                    errorMessage)) {
                return false;
            }
        }
    }

    const int defaultMaterialIndex = static_cast<int>(result.materials.size());
    result.materials.push_back(BuildRenderMaterial("Imported Default", MakeRenderFloat3(0.8f, 0.8f, 0.8f)));

    const json meshArray = root.value("meshes", json::array());
    std::vector<int> meshMap;
    if (meshArray.is_array()) {
        meshMap.assign(meshArray.size(), -1);
        for (std::size_t i = 0; i < meshArray.size(); ++i) {
            RenderMeshDefinition meshDefinition;
            if (!BuildMeshDefinition(
                    root,
                    buffers,
                    meshArray[i],
                    static_cast<int>(i),
                    assetIndex,
                    defaultMaterialIndex,
                    meshDefinition,
                    errorMessage)) {
                if (!errorMessage.empty()) {
                    return false;
                }
                continue;
            }

            meshMap[i] = static_cast<int>(result.meshes.size());
            result.meshes.push_back(std::move(meshDefinition));
        }
    }

    if (!BuildSceneInstances(root, meshMap, result.meshInstances, errorMessage)) {
        return false;
    }

    if (result.meshInstances.empty() && !result.meshes.empty()) {
        for (int meshIndex = 0; meshIndex < static_cast<int>(result.meshes.size()); ++meshIndex) {
            RenderMeshInstance instance;
            instance.name = result.meshes[static_cast<std::size_t>(meshIndex)].name;
            instance.meshIndex = meshIndex;
            result.meshInstances.push_back(std::move(instance));
        }
    }

    if (result.meshes.empty() || result.meshInstances.empty()) {
        errorMessage = "No static triangle mesh content was found in the selected glTF scene.";
        return false;
    }

    errorMessage.clear();
    return true;
}

} // namespace RenderGltfImporter
