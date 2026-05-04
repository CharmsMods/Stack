#include "RenderFoundationModelImport.h"

#include "Utils/Base64.h"

#include "json.hpp"
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

namespace RenderFoundation {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr std::uint32_t kGlbMagic = 0x46546C67u;
constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534Au;
constexpr std::uint32_t kGlbBinChunk = 0x004E4942u;

struct BufferViewRecord {
    int buffer = -1;
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    std::size_t byteStride = 0;
};

struct AccessorRecord {
    int bufferView = -1;
    std::size_t byteOffset = 0;
    std::size_t count = 0;
    int componentType = 5126;
    std::string type = "SCALAR";
    bool normalized = false;
};

struct ImageRecord {
    std::string name;
    std::string sourcePath;
    std::string sourceUri;
    RenderImportedTextureSourceKind sourceKind = RenderImportedTextureSourceKind::None;
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<unsigned char> pixels;
};

struct TextureRecord {
    int sourceImage = -1;
};

struct MeshRecord {
    std::string name;
    std::vector<RenderMeshTriangle> triangles;
};

struct Mat4 {
    std::array<float, 16> m {};
};

struct NodeRecord {
    std::string name;
    int mesh = -1;
    std::vector<int> children;
    Mat4 localMatrix {};
};

bool ReadBinaryFile(const fs::path& path, std::vector<unsigned char>& outBytes) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamsize byteCount = stream.tellg();
    if (byteCount < 0) {
        return false;
    }
    stream.seekg(0, std::ios::beg);
    outBytes.resize(static_cast<std::size_t>(byteCount));
    if (byteCount > 0) {
        stream.read(reinterpret_cast<char*>(outBytes.data()), byteCount);
    }
    return stream.good() || stream.eof();
}

bool ReadTextFile(const fs::path& path, std::string& outText) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    std::ostringstream builder;
    builder << stream.rdbuf();
    outText = builder.str();
    return stream.good() || stream.eof();
}

bool IsDataUri(const std::string& uri) {
    return uri.rfind("data:", 0) == 0;
}

std::vector<unsigned char> DecodeDataUri(const std::string& uri) {
    const std::size_t comma = uri.find(',');
    if (comma == std::string::npos) {
        return {};
    }
    const std::string_view header(uri.data(), comma);
    const std::string payload = uri.substr(comma + 1);
    if (header.find(";base64") != std::string_view::npos) {
        return Utils::Base64Decode(payload);
    }
    return std::vector<unsigned char>(payload.begin(), payload.end());
}

std::uint32_t ReadU32(const unsigned char* data) {
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

Mat4 IdentityMatrix() {
    Mat4 matrix {};
    matrix.m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    return matrix;
}

Mat4 Multiply(const Mat4& left, const Mat4& right) {
    Mat4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result.m[column * 4 + row] =
                left.m[0 * 4 + row] * right.m[column * 4 + 0] +
                left.m[1 * 4 + row] * right.m[column * 4 + 1] +
                left.m[2 * 4 + row] * right.m[column * 4 + 2] +
                left.m[3 * 4 + row] * right.m[column * 4 + 3];
        }
    }
    return result;
}

Mat4 MakeTranslationMatrix(const Vec3& translation) {
    Mat4 matrix = IdentityMatrix();
    matrix.m[12] = translation.x;
    matrix.m[13] = translation.y;
    matrix.m[14] = translation.z;
    return matrix;
}

Mat4 MakeScaleMatrix(const Vec3& scale) {
    Mat4 matrix = IdentityMatrix();
    matrix.m[0] = scale.x;
    matrix.m[5] = scale.y;
    matrix.m[10] = scale.z;
    return matrix;
}

Mat4 MakeQuaternionMatrix(float x, float y, float z, float w) {
    Mat4 matrix = IdentityMatrix();
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    matrix.m[0] = 1.0f - 2.0f * (yy + zz);
    matrix.m[1] = 2.0f * (xy + wz);
    matrix.m[2] = 2.0f * (xz - wy);

    matrix.m[4] = 2.0f * (xy - wz);
    matrix.m[5] = 1.0f - 2.0f * (xx + zz);
    matrix.m[6] = 2.0f * (yz + wx);

    matrix.m[8] = 2.0f * (xz + wy);
    matrix.m[9] = 2.0f * (yz - wx);
    matrix.m[10] = 1.0f - 2.0f * (xx + yy);
    return matrix;
}

Mat4 ParseNodeMatrix(const json& nodeValue) {
    Mat4 matrix = IdentityMatrix();
    if (const json matrixValue = nodeValue.value("matrix", json()); matrixValue.is_array() && matrixValue.size() == 16) {
        for (std::size_t index = 0; index < 16; ++index) {
            matrix.m[index] = matrixValue[index].get<float>();
        }
        return matrix;
    }

    Vec3 translation { 0.0f, 0.0f, 0.0f };
    Vec3 scale { 1.0f, 1.0f, 1.0f };
    std::array<float, 4> rotation { 0.0f, 0.0f, 0.0f, 1.0f };

    if (const json translationValue = nodeValue.value("translation", json()); translationValue.is_array() && translationValue.size() == 3) {
        translation.x = translationValue[0].get<float>();
        translation.y = translationValue[1].get<float>();
        translation.z = translationValue[2].get<float>();
    }
    if (const json scaleValue = nodeValue.value("scale", json()); scaleValue.is_array() && scaleValue.size() == 3) {
        scale.x = scaleValue[0].get<float>();
        scale.y = scaleValue[1].get<float>();
        scale.z = scaleValue[2].get<float>();
    }
    if (const json rotationValue = nodeValue.value("rotation", json()); rotationValue.is_array() && rotationValue.size() == 4) {
        rotation[0] = rotationValue[0].get<float>();
        rotation[1] = rotationValue[1].get<float>();
        rotation[2] = rotationValue[2].get<float>();
        rotation[3] = rotationValue[3].get<float>();
    }

    return Multiply(
        MakeTranslationMatrix(translation),
        Multiply(
            MakeQuaternionMatrix(rotation[0], rotation[1], rotation[2], rotation[3]),
            MakeScaleMatrix(scale)));
}

RenderFloat3 TransformPointNoTranslation(const Mat4& matrix, const RenderFloat3& point) {
    return MakeRenderFloat3(
        matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z,
        matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z,
        matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z);
}

RenderFloat3 TransformPoint(const Mat4& matrix, const RenderFloat3& point) {
    return MakeRenderFloat3(
        matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
        matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
        matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]);
}

bool InvertLinear3x3(const Mat4& matrix, std::array<float, 9>& outInverse) {
    const float a00 = matrix.m[0];
    const float a01 = matrix.m[4];
    const float a02 = matrix.m[8];
    const float a10 = matrix.m[1];
    const float a11 = matrix.m[5];
    const float a12 = matrix.m[9];
    const float a20 = matrix.m[2];
    const float a21 = matrix.m[6];
    const float a22 = matrix.m[10];

    const float b01 = a22 * a11 - a12 * a21;
    const float b11 = -a22 * a10 + a12 * a20;
    const float b21 = a21 * a10 - a11 * a20;
    float determinant = a00 * b01 + a01 * b11 + a02 * b21;
    if (std::fabs(determinant) <= 1e-8f) {
        return false;
    }

    determinant = 1.0f / determinant;
    outInverse[0] = b01 * determinant;
    outInverse[1] = (-a22 * a01 + a02 * a21) * determinant;
    outInverse[2] = (a12 * a01 - a02 * a11) * determinant;
    outInverse[3] = b11 * determinant;
    outInverse[4] = (a22 * a00 - a02 * a20) * determinant;
    outInverse[5] = (-a12 * a00 + a02 * a10) * determinant;
    outInverse[6] = b21 * determinant;
    outInverse[7] = (-a21 * a00 + a01 * a20) * determinant;
    outInverse[8] = (a11 * a00 - a01 * a10) * determinant;
    return true;
}

RenderFloat3 TransformNormalNoTranslation(const Mat4& matrix, const RenderFloat3& normal) {
    std::array<float, 9> inverse {};
    if (!InvertLinear3x3(matrix, inverse)) {
        return Normalize(TransformPointNoTranslation(matrix, normal));
    }

    RenderFloat3 transformed = MakeRenderFloat3(
        inverse[0] * normal.x + inverse[3] * normal.y + inverse[6] * normal.z,
        inverse[1] * normal.x + inverse[4] * normal.y + inverse[7] * normal.z,
        inverse[2] * normal.x + inverse[5] * normal.y + inverse[8] * normal.z);
    return Normalize(transformed);
}

int ComponentCountForType(const std::string& type) {
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    if (type == "MAT2") {
        return 4;
    }
    if (type == "MAT3") {
        return 9;
    }
    if (type == "MAT4") {
        return 16;
    }
    return 1;
}

std::size_t ComponentSizeBytes(int componentType) {
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
    }
    return 0;
}

double ReadComponentValue(const unsigned char* data, int componentType, bool normalized) {
    switch (componentType) {
        case 5120: {
            const auto value = static_cast<std::int8_t>(*data);
            return normalized ? std::max(-1.0, static_cast<double>(value) / 127.0) : static_cast<double>(value);
        }
        case 5121: {
            const auto value = static_cast<std::uint8_t>(*data);
            return normalized ? static_cast<double>(value) / 255.0 : static_cast<double>(value);
        }
        case 5122: {
            std::int16_t value = static_cast<std::int16_t>(static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8u));
            return normalized ? std::max(-1.0, static_cast<double>(value) / 32767.0) : static_cast<double>(value);
        }
        case 5123: {
            std::uint16_t value = static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8u);
            return normalized ? static_cast<double>(value) / 65535.0 : static_cast<double>(value);
        }
        case 5125:
            return static_cast<double>(ReadU32(data));
        case 5126: {
            float value = 0.0f;
            std::memcpy(&value, data, sizeof(float));
            return static_cast<double>(value);
        }
    }
    return 0.0;
}

bool DecodeAccessorFloats(
    const AccessorRecord& accessor,
    const std::vector<BufferViewRecord>& bufferViews,
    const std::vector<std::vector<unsigned char>>& buffers,
    int expectedComponents,
    std::vector<float>& outValues,
    std::string& errorMessage) {

    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(bufferViews.size())) {
        errorMessage = "glTF accessor is missing a valid bufferView.";
        return false;
    }
    if (ComponentCountForType(accessor.type) != expectedComponents) {
        errorMessage = "glTF accessor type does not match the expected attribute width.";
        return false;
    }

    const BufferViewRecord& view = bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(buffers.size())) {
        errorMessage = "glTF bufferView references an invalid buffer.";
        return false;
    }
    const std::vector<unsigned char>& buffer = buffers[static_cast<std::size_t>(view.buffer)];
    const std::size_t componentSize = ComponentSizeBytes(accessor.componentType);
    if (componentSize == 0) {
        errorMessage = "glTF accessor uses an unsupported component type.";
        return false;
    }

    const std::size_t packedStride = componentSize * static_cast<std::size_t>(expectedComponents);
    const std::size_t stride = view.byteStride > 0 ? view.byteStride : packedStride;
    const std::size_t accessorOffset = view.byteOffset + accessor.byteOffset;
    outValues.assign(accessor.count * static_cast<std::size_t>(expectedComponents), 0.0f);

    for (std::size_t elementIndex = 0; elementIndex < accessor.count; ++elementIndex) {
        const std::size_t elementOffset = accessorOffset + elementIndex * stride;
        if (elementOffset + packedStride > buffer.size()) {
            errorMessage = "glTF accessor overruns its backing buffer.";
            return false;
        }
        for (int component = 0; component < expectedComponents; ++component) {
            const unsigned char* componentData = buffer.data() + elementOffset + component * componentSize;
            outValues[elementIndex * static_cast<std::size_t>(expectedComponents) + static_cast<std::size_t>(component)] =
                static_cast<float>(ReadComponentValue(componentData, accessor.componentType, accessor.normalized));
        }
    }

    return true;
}

bool DecodeAccessorIndices(
    const AccessorRecord& accessor,
    const std::vector<BufferViewRecord>& bufferViews,
    const std::vector<std::vector<unsigned char>>& buffers,
    std::vector<unsigned int>& outIndices,
    std::string& errorMessage) {

    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(bufferViews.size())) {
        errorMessage = "glTF index accessor is missing a valid bufferView.";
        return false;
    }
    if (accessor.type != "SCALAR") {
        errorMessage = "glTF index accessor must be scalar.";
        return false;
    }

    const BufferViewRecord& view = bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(buffers.size())) {
        errorMessage = "glTF index bufferView references an invalid buffer.";
        return false;
    }
    const std::vector<unsigned char>& buffer = buffers[static_cast<std::size_t>(view.buffer)];
    const std::size_t componentSize = ComponentSizeBytes(accessor.componentType);
    if (componentSize == 0) {
        errorMessage = "glTF index accessor uses an unsupported component type.";
        return false;
    }

    const std::size_t stride = view.byteStride > 0 ? view.byteStride : componentSize;
    const std::size_t accessorOffset = view.byteOffset + accessor.byteOffset;
    outIndices.assign(accessor.count, 0u);
    for (std::size_t index = 0; index < accessor.count; ++index) {
        const std::size_t elementOffset = accessorOffset + index * stride;
        if (elementOffset + componentSize > buffer.size()) {
            errorMessage = "glTF index accessor overruns its backing buffer.";
            return false;
        }
        outIndices[index] = static_cast<unsigned int>(ReadComponentValue(
            buffer.data() + elementOffset,
            accessor.componentType,
            false));
    }
    return true;
}

std::vector<BufferViewRecord> ParseBufferViews(const json& root) {
    std::vector<BufferViewRecord> views;
    const json bufferViews = root.value("bufferViews", json::array());
    views.reserve(bufferViews.is_array() ? bufferViews.size() : 0);
    for (const json& value : bufferViews) {
        BufferViewRecord record;
        record.buffer = value.value("buffer", -1);
        record.byteOffset = value.value("byteOffset", static_cast<std::size_t>(0));
        record.byteLength = value.value("byteLength", static_cast<std::size_t>(0));
        record.byteStride = value.value("byteStride", static_cast<std::size_t>(0));
        views.push_back(record);
    }
    return views;
}

std::vector<AccessorRecord> ParseAccessors(const json& root) {
    std::vector<AccessorRecord> accessors;
    const json accessorValues = root.value("accessors", json::array());
    accessors.reserve(accessorValues.is_array() ? accessorValues.size() : 0);
    for (const json& value : accessorValues) {
        AccessorRecord record;
        record.bufferView = value.value("bufferView", -1);
        record.byteOffset = value.value("byteOffset", static_cast<std::size_t>(0));
        record.count = value.value("count", static_cast<std::size_t>(0));
        record.componentType = value.value("componentType", 5126);
        record.type = value.value("type", std::string("SCALAR"));
        record.normalized = value.value("normalized", false);
        accessors.push_back(record);
    }
    return accessors;
}

bool ParseGlbFile(
    const fs::path& filePath,
    json& outRoot,
    std::vector<unsigned char>& outBinChunk,
    std::string& errorMessage) {

    std::vector<unsigned char> bytes;
    if (!ReadBinaryFile(filePath, bytes) || bytes.size() < 20) {
        errorMessage = "Failed to read the binary glTF file.";
        return false;
    }

    if (ReadU32(bytes.data()) != kGlbMagic) {
        errorMessage = "The selected .glb file has an invalid header.";
        return false;
    }

    std::size_t offset = 12;
    std::string jsonText;
    while (offset + 8 <= bytes.size()) {
        const std::uint32_t chunkLength = ReadU32(bytes.data() + offset);
        const std::uint32_t chunkType = ReadU32(bytes.data() + offset + 4);
        offset += 8;
        if (offset + chunkLength > bytes.size()) {
            errorMessage = "The binary glTF file contains a truncated chunk.";
            return false;
        }
        if (chunkType == kGlbJsonChunk) {
            jsonText.assign(reinterpret_cast<const char*>(bytes.data() + offset), chunkLength);
        } else if (chunkType == kGlbBinChunk) {
            outBinChunk.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
        }
        offset += chunkLength;
    }

    if (jsonText.empty()) {
        errorMessage = "The binary glTF file does not contain a JSON chunk.";
        return false;
    }

    try {
        outRoot = json::parse(jsonText);
    } catch (const std::exception& exception) {
        errorMessage = std::string("Failed to parse the .glb JSON chunk: ") + exception.what();
        return false;
    }
    return true;
}

bool ParseGltfFile(
    const fs::path& filePath,
    json& outRoot,
    std::string& errorMessage) {

    std::string jsonText;
    if (!ReadTextFile(filePath, jsonText)) {
        errorMessage = "Failed to read the glTF JSON file.";
        return false;
    }

    try {
        outRoot = json::parse(jsonText);
    } catch (const std::exception& exception) {
        errorMessage = std::string("Failed to parse the glTF JSON: ") + exception.what();
        return false;
    }
    return true;
}

bool ResolveBuffers(
    const fs::path& sourcePath,
    const json& root,
    const std::vector<unsigned char>& glbBinChunk,
    std::vector<std::vector<unsigned char>>& outBuffers,
    std::string& errorMessage) {

    const json buffers = root.value("buffers", json::array());
    outBuffers.clear();
    outBuffers.reserve(buffers.is_array() ? buffers.size() : 0);
    for (std::size_t bufferIndex = 0; bufferIndex < buffers.size(); ++bufferIndex) {
        const json& bufferValue = buffers[bufferIndex];
        std::vector<unsigned char> bufferBytes;
        const std::string uri = bufferValue.value("uri", std::string());
        if (!uri.empty()) {
            if (IsDataUri(uri)) {
                bufferBytes = DecodeDataUri(uri);
            } else {
                ReadBinaryFile(sourcePath.parent_path() / fs::path(uri), bufferBytes);
            }
        } else if (bufferIndex == 0 && !glbBinChunk.empty()) {
            bufferBytes = glbBinChunk;
        }

        if (bufferBytes.empty() && bufferValue.value("byteLength", 0) > 0) {
            errorMessage = "A glTF buffer could not be loaded.";
            return false;
        }

        outBuffers.push_back(std::move(bufferBytes));
    }

    return true;
}

bool ResolveImages(
    const fs::path& sourcePath,
    const json& root,
    const std::vector<BufferViewRecord>& bufferViews,
    const std::vector<std::vector<unsigned char>>& buffers,
    std::vector<ImageRecord>& outImages,
    std::string& errorMessage) {

    const json images = root.value("images", json::array());
    outImages.clear();
    outImages.reserve(images.is_array() ? images.size() : 0);

    for (std::size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        const json& imageValue = images[imageIndex];
        ImageRecord record;
        record.name = imageValue.value("name", "Image " + std::to_string(imageIndex + 1));

        std::vector<unsigned char> encodedBytes;
        if (imageValue.contains("uri")) {
            record.sourceUri = imageValue.value("uri", std::string());
            if (IsDataUri(record.sourceUri)) {
                encodedBytes = DecodeDataUri(record.sourceUri);
                record.sourceKind = RenderImportedTextureSourceKind::EmbeddedAssetImage;
                record.sourcePath = sourcePath.string();
            } else {
                const fs::path resolvedPath = sourcePath.parent_path() / fs::path(record.sourceUri);
                record.sourcePath = resolvedPath.string();
                record.sourceKind = RenderImportedTextureSourceKind::ExternalFile;
                ReadBinaryFile(resolvedPath, encodedBytes);
            }
        } else if (imageValue.contains("bufferView")) {
            const int bufferViewIndex = imageValue.value("bufferView", -1);
            if (bufferViewIndex >= 0 && bufferViewIndex < static_cast<int>(bufferViews.size())) {
                const BufferViewRecord& view = bufferViews[static_cast<std::size_t>(bufferViewIndex)];
                if (view.buffer >= 0 && view.buffer < static_cast<int>(buffers.size())) {
                    const std::vector<unsigned char>& buffer = buffers[static_cast<std::size_t>(view.buffer)];
                    if (view.byteOffset + view.byteLength <= buffer.size()) {
                        encodedBytes.assign(
                            buffer.begin() + static_cast<std::ptrdiff_t>(view.byteOffset),
                            buffer.begin() + static_cast<std::ptrdiff_t>(view.byteOffset + view.byteLength));
                        record.sourceKind = RenderImportedTextureSourceKind::EmbeddedAssetImage;
                        record.sourcePath = sourcePath.string();
                        record.sourceUri = imageValue.value("mimeType", std::string());
                    }
                }
            }
        }

        if (encodedBytes.empty()) {
            continue;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* decodedPixels = stbi_load_from_memory(
            encodedBytes.data(),
            static_cast<int>(encodedBytes.size()),
            &width,
            &height,
            &channels,
            4);
        if (decodedPixels == nullptr || width <= 0 || height <= 0) {
            if (decodedPixels != nullptr) {
                stbi_image_free(decodedPixels);
            }
            continue;
        }

        record.width = width;
        record.height = height;
        record.channels = 4;
        record.pixels.assign(
            decodedPixels,
            decodedPixels + static_cast<std::ptrdiff_t>(width * height * 4));
        stbi_image_free(decodedPixels);
        outImages.push_back(std::move(record));
    }

    if (images.is_array() && !images.empty() && outImages.empty()) {
        errorMessage = "The glTF file references images, but none of them could be decoded.";
        return false;
    }
    return true;
}

std::vector<TextureRecord> ParseTextures(const json& root) {
    const json textureValues = root.value("textures", json::array());
    std::vector<TextureRecord> textures;
    textures.reserve(textureValues.is_array() ? textureValues.size() : 0);
    for (const json& value : textureValues) {
        TextureRecord texture;
        texture.sourceImage = value.value("source", -1);
        textures.push_back(texture);
    }
    return textures;
}

Vec3 ParseVec3(const json& value, const Vec3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return {
        value[0].get<float>(),
        value[1].get<float>(),
        value[2].get<float>()
    };
}

Vec3 ParseVec4Rgb(const json& value, const Vec3& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return {
        value[0].get<float>(),
        value[1].get<float>(),
        value[2].get<float>()
    };
}

int EnsureImportedTexture(
    int gltfTextureIndex,
    RenderTextureSemantic semantic,
    const std::vector<TextureRecord>& gltfTextures,
    const std::vector<ImageRecord>& images,
    std::vector<RenderImportedTexture>& importedTextures,
    std::map<std::pair<int, int>, int>& textureCache) {

    if (gltfTextureIndex < 0 || gltfTextureIndex >= static_cast<int>(gltfTextures.size())) {
        return -1;
    }

    const auto cacheKey = std::make_pair(gltfTextureIndex, static_cast<int>(semantic));
    const auto cached = textureCache.find(cacheKey);
    if (cached != textureCache.end()) {
        return cached->second;
    }

    const int sourceImage = gltfTextures[static_cast<std::size_t>(gltfTextureIndex)].sourceImage;
    if (sourceImage < 0 || sourceImage >= static_cast<int>(images.size())) {
        return -1;
    }

    const ImageRecord& image = images[static_cast<std::size_t>(sourceImage)];
    RenderImportedTexture texture;
    texture.name = image.name.empty() ? ("Texture " + std::to_string(importedTextures.size() + 1)) : image.name;
    texture.sourcePath = image.sourcePath;
    texture.sourceUri = image.sourceUri;
    texture.sourceKind = image.sourceKind;
    texture.semantic = semantic;
    texture.sourceAssetIndex = 0;
    texture.sourceImageIndex = sourceImage;
    texture.width = image.width;
    texture.height = image.height;
    texture.channels = image.channels;
    texture.pixels = image.pixels;
    importedTextures.push_back(std::move(texture));
    const int importedIndex = static_cast<int>(importedTextures.size()) - 1;
    textureCache.emplace(cacheKey, importedIndex);
    return importedIndex;
}

std::vector<Material> ParseMaterials(
    const json& root,
    const std::vector<TextureRecord>& gltfTextures,
    const std::vector<ImageRecord>& images,
    std::vector<RenderImportedTexture>& importedTextures) {

    const json materialValues = root.value("materials", json::array());
    std::vector<Material> materials;
    materials.reserve(materialValues.is_array() ? materialValues.size() : 1);
    std::map<std::pair<int, int>, int> textureCache;

    for (std::size_t materialIndex = 0; materialIndex < materialValues.size(); ++materialIndex) {
        const json& value = materialValues[materialIndex];
        Material material;
        material.name = value.value("name", "Imported Material " + std::to_string(materialIndex + 1));
        material.importedSource = true;
        material.sourceAssetIndex = 0;
        material.sourceMaterialName = material.name;

        const json pbr = value.value("pbrMetallicRoughness", json::object());
        material.baseColor = ParseVec4Rgb(pbr.value("baseColorFactor", json()), { 0.8f, 0.8f, 0.8f });
        material.metallic = pbr.value("metallicFactor", 0.0f);
        material.roughness = pbr.value("roughnessFactor", 1.0f);
        material.emissionColor = ParseVec3(value.value("emissiveFactor", json()), { 1.0f, 1.0f, 1.0f });

        if (const json transmissionExt = value.value("extensions", json::object()).value("KHR_materials_transmission", json::object());
            transmissionExt.is_object()) {
            material.transmission = transmissionExt.value("transmissionFactor", 0.0f);
        }
        if (const json iorExt = value.value("extensions", json::object()).value("KHR_materials_ior", json::object());
            iorExt.is_object()) {
            material.ior = iorExt.value("ior", 1.5f);
        }
        if (const json volumeExt = value.value("extensions", json::object()).value("KHR_materials_volume", json::object());
            volumeExt.is_object()) {
            material.absorptionColor = ParseVec3(volumeExt.value("attenuationColor", json()), { 1.0f, 1.0f, 1.0f });
            material.absorptionDistance = volumeExt.value("attenuationDistance", 1.0f);
        }
        if (const json emissiveStrengthExt = value.value("extensions", json::object()).value("KHR_materials_emissive_strength", json::object());
            emissiveStrengthExt.is_object()) {
            material.emissionStrength = emissiveStrengthExt.value("emissiveStrength", 0.0f);
        } else if (material.emissionColor.x > 0.001f || material.emissionColor.y > 0.001f || material.emissionColor.z > 0.001f) {
            material.emissionStrength = 1.0f;
        }
        if (const json clearCoatExt = value.value("extensions", json::object()).value("KHR_materials_clearcoat", json::object());
            clearCoatExt.is_object()) {
            material.clearCoat = clearCoatExt.value("clearcoatFactor", 0.0f);
        }

        if (const json baseColorTexture = pbr.value("baseColorTexture", json::object()); baseColorTexture.is_object()) {
            material.baseColorTexture.textureIndex = EnsureImportedTexture(
                baseColorTexture.value("index", -1),
                RenderTextureSemantic::BaseColor,
                gltfTextures,
                images,
                importedTextures,
                textureCache);
            material.baseColorTexture.uvSet = baseColorTexture.value("texCoord", 0);
        }
        if (const json metallicTexture = pbr.value("metallicRoughnessTexture", json::object()); metallicTexture.is_object()) {
            material.metallicRoughnessTexture.textureIndex = EnsureImportedTexture(
                metallicTexture.value("index", -1),
                RenderTextureSemantic::MetallicRoughness,
                gltfTextures,
                images,
                importedTextures,
                textureCache);
            material.metallicRoughnessTexture.uvSet = metallicTexture.value("texCoord", 0);
        }
        if (const json emissiveTexture = value.value("emissiveTexture", json::object()); emissiveTexture.is_object()) {
            material.emissiveTexture.textureIndex = EnsureImportedTexture(
                emissiveTexture.value("index", -1),
                RenderTextureSemantic::Emissive,
                gltfTextures,
                images,
                importedTextures,
                textureCache);
            material.emissiveTexture.uvSet = emissiveTexture.value("texCoord", 0);
        }
        if (const json normalTexture = value.value("normalTexture", json::object()); normalTexture.is_object()) {
            material.normalTexture.textureIndex = EnsureImportedTexture(
                normalTexture.value("index", -1),
                RenderTextureSemantic::Normal,
                gltfTextures,
                images,
                importedTextures,
                textureCache);
            material.normalTexture.uvSet = normalTexture.value("texCoord", 0);
        }

        if (material.emissionStrength > 0.001f) {
            material.baseMaterial = BaseMaterial::Emissive;
        } else if (material.transmission > 0.05f) {
            material.baseMaterial = BaseMaterial::Glass;
        } else if (material.metallic > 0.5f) {
            material.baseMaterial = BaseMaterial::Metal;
        } else {
            material.baseMaterial = BaseMaterial::Diffuse;
        }

        SyncMaterialLayersFromLegacy(material);
        if (material.clearCoat > 0.0001f) {
            if (Material::Layer* clearCoatLayer = FindMaterialLayer(material, MaterialLayerType::ClearCoat)) {
                const json clearCoatExt = value.value("extensions", json::object()).value("KHR_materials_clearcoat", json::object());
                if (clearCoatExt.is_object()) {
                    clearCoatLayer->roughness = clearCoatExt.value("clearcoatRoughnessFactor", clearCoatLayer->roughness);
                }
                SyncLegacyMaterialFromLayers(material);
            }
        }

        materials.push_back(std::move(material));
    }

    if (materials.empty()) {
        Material fallback;
        fallback.name = "Imported Material";
        fallback.importedSource = true;
        fallback.sourceAssetIndex = 0;
        fallback.sourceMaterialName = fallback.name;
        fallback.baseMaterial = BaseMaterial::Diffuse;
        fallback.baseColor = { 0.8f, 0.8f, 0.8f };
        SyncMaterialLayersFromLegacy(fallback);
        materials.push_back(std::move(fallback));
    }

    return materials;
}

bool ParseMeshTriangles(
    const json& primitiveValue,
    const std::vector<AccessorRecord>& accessors,
    const std::vector<BufferViewRecord>& bufferViews,
    const std::vector<std::vector<unsigned char>>& buffers,
    int fallbackMaterialIndex,
    std::vector<RenderMeshTriangle>& outTriangles,
    std::string& errorMessage) {

    const json attributes = primitiveValue.value("attributes", json::object());
    const int positionAccessorIndex = attributes.value("POSITION", -1);
    if (positionAccessorIndex < 0 || positionAccessorIndex >= static_cast<int>(accessors.size())) {
        errorMessage = "A glTF mesh primitive is missing POSITION data.";
        return false;
    }

    std::vector<float> positions;
    if (!DecodeAccessorFloats(accessors[static_cast<std::size_t>(positionAccessorIndex)], bufferViews, buffers, 3, positions, errorMessage)) {
        return false;
    }

    std::vector<float> normals;
    const int normalAccessorIndex = attributes.value("NORMAL", -1);
    if (normalAccessorIndex >= 0 && normalAccessorIndex < static_cast<int>(accessors.size())) {
        if (!DecodeAccessorFloats(accessors[static_cast<std::size_t>(normalAccessorIndex)], bufferViews, buffers, 3, normals, errorMessage)) {
            normals.clear();
        }
    }

    std::vector<float> uvs;
    const int texcoordAccessorIndex = attributes.value("TEXCOORD_0", -1);
    if (texcoordAccessorIndex >= 0 && texcoordAccessorIndex < static_cast<int>(accessors.size())) {
        if (!DecodeAccessorFloats(accessors[static_cast<std::size_t>(texcoordAccessorIndex)], bufferViews, buffers, 2, uvs, errorMessage)) {
            uvs.clear();
        }
    }

    std::vector<unsigned int> indices;
    const int indexAccessorIndex = primitiveValue.value("indices", -1);
    if (indexAccessorIndex >= 0 && indexAccessorIndex < static_cast<int>(accessors.size())) {
        if (!DecodeAccessorIndices(accessors[static_cast<std::size_t>(indexAccessorIndex)], bufferViews, buffers, indices, errorMessage)) {
            return false;
        }
    } else {
        indices.resize(positions.size() / 3u);
        for (std::size_t index = 0; index < indices.size(); ++index) {
            indices[index] = static_cast<unsigned int>(index);
        }
    }

    if ((indices.size() % 3u) != 0u) {
        errorMessage = "A glTF mesh primitive is not triangulated.";
        return false;
    }

    const int materialIndex = primitiveValue.value("material", fallbackMaterialIndex);
    const auto readVec3 = [&](const std::vector<float>& values, unsigned int elementIndex, const RenderFloat3& fallback) {
        const std::size_t offset = static_cast<std::size_t>(elementIndex) * 3u;
        if (offset + 2u >= values.size()) {
            return fallback;
        }
        return MakeRenderFloat3(values[offset + 0u], values[offset + 1u], values[offset + 2u]);
    };
    const auto readVec2 = [&](const std::vector<float>& values, unsigned int elementIndex, const RenderFloat2& fallback) {
        const std::size_t offset = static_cast<std::size_t>(elementIndex) * 2u;
        if (offset + 1u >= values.size()) {
            return fallback;
        }
        return MakeRenderFloat2(values[offset + 0u], values[offset + 1u]);
    };

    for (std::size_t triangleIndex = 0; triangleIndex < indices.size(); triangleIndex += 3u) {
        const unsigned int ia = indices[triangleIndex + 0u];
        const unsigned int ib = indices[triangleIndex + 1u];
        const unsigned int ic = indices[triangleIndex + 2u];

        RenderMeshTriangle triangle;
        triangle.name = "Triangle " + std::to_string(outTriangles.size() + 1);
        triangle.localA = readVec3(positions, ia, {});
        triangle.localB = readVec3(positions, ib, {});
        triangle.localC = readVec3(positions, ic, {});
        triangle.materialIndex = std::max(materialIndex, 0);
        triangle.uvA = readVec2(uvs, ia, MakeRenderFloat2(0.0f, 0.0f));
        triangle.uvB = readVec2(uvs, ib, MakeRenderFloat2(1.0f, 0.0f));
        triangle.uvC = readVec2(uvs, ic, MakeRenderFloat2(0.0f, 1.0f));
        triangle.albedoTint = MakeRenderFloat3(1.0f, 1.0f, 1.0f);

        if (!normals.empty()) {
            triangle.localNormalA = Normalize(readVec3(normals, ia, MakeRenderFloat3(0.0f, 1.0f, 0.0f)));
            triangle.localNormalB = Normalize(readVec3(normals, ib, MakeRenderFloat3(0.0f, 1.0f, 0.0f)));
            triangle.localNormalC = Normalize(readVec3(normals, ic, MakeRenderFloat3(0.0f, 1.0f, 0.0f)));
        } else {
            const RenderFloat3 geometricNormal = Normalize(Cross(
                Subtract(triangle.localB, triangle.localA),
                Subtract(triangle.localC, triangle.localA)));
            triangle.localNormalA = geometricNormal;
            triangle.localNormalB = geometricNormal;
            triangle.localNormalC = geometricNormal;
        }

        outTriangles.push_back(std::move(triangle));
    }

    return true;
}

std::vector<MeshRecord> ParseMeshes(
    const json& root,
    const std::vector<AccessorRecord>& accessors,
    const std::vector<BufferViewRecord>& bufferViews,
    const std::vector<std::vector<unsigned char>>& buffers,
    int fallbackMaterialIndex,
    std::string& errorMessage) {

    const json meshValues = root.value("meshes", json::array());
    if (!meshValues.is_array()) {
        return {};
    }

    std::vector<MeshRecord> meshes(meshValues.size());
    for (std::size_t meshIndex = 0; meshIndex < meshValues.size(); ++meshIndex) {
        const json& meshValue = meshValues[meshIndex];
        MeshRecord mesh;
        mesh.name = meshValue.value("name", "Mesh " + std::to_string(meshIndex + 1));
        const json primitiveValues = meshValue.value("primitives", json::array());
        for (const json& primitiveValue : primitiveValues) {
            if (primitiveValue.value("mode", 4) != 4) {
                continue;
            }
            if (!ParseMeshTriangles(
                    primitiveValue,
                    accessors,
                    bufferViews,
                    buffers,
                    fallbackMaterialIndex,
                    mesh.triangles,
                    errorMessage)) {
                return {};
            }
        }
        meshes[meshIndex] = std::move(mesh);
    }
    return meshes;
}

std::vector<NodeRecord> ParseNodes(const json& root) {
    const json nodeValues = root.value("nodes", json::array());
    std::vector<NodeRecord> nodes;
    nodes.reserve(nodeValues.is_array() ? nodeValues.size() : 0);
    for (std::size_t nodeIndex = 0; nodeIndex < nodeValues.size(); ++nodeIndex) {
        const json& nodeValue = nodeValues[nodeIndex];
        NodeRecord node;
        node.name = nodeValue.value("name", "Node " + std::to_string(nodeIndex + 1));
        node.mesh = nodeValue.value("mesh", -1);
        node.localMatrix = ParseNodeMatrix(nodeValue);
        const json childValues = nodeValue.value("children", json::array());
        if (childValues.is_array()) {
            node.children.reserve(childValues.size());
            for (const json& childValue : childValues) {
                node.children.push_back(childValue.get<int>());
            }
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

void AppendTransformedMeshTriangles(
    const MeshRecord& sourceMesh,
    const Mat4& worldMatrix,
    std::vector<RenderMeshTriangle>& outTriangles) {
    outTriangles.reserve(outTriangles.size() + sourceMesh.triangles.size());
    for (const RenderMeshTriangle& sourceTriangle : sourceMesh.triangles) {
        RenderMeshTriangle triangle = sourceTriangle;
        triangle.localA = TransformPoint(worldMatrix, sourceTriangle.localA);
        triangle.localB = TransformPoint(worldMatrix, sourceTriangle.localB);
        triangle.localC = TransformPoint(worldMatrix, sourceTriangle.localC);
        triangle.localNormalA = TransformNormalNoTranslation(worldMatrix, sourceTriangle.localNormalA);
        triangle.localNormalB = TransformNormalNoTranslation(worldMatrix, sourceTriangle.localNormalB);
        triangle.localNormalC = TransformNormalNoTranslation(worldMatrix, sourceTriangle.localNormalC);
        outTriangles.push_back(std::move(triangle));
    }
}

void ApplyUniformScale(std::vector<RenderMeshTriangle>& triangles, float scale) {
    if (NearlyEqual(scale, 1.0f)) {
        return;
    }

    for (RenderMeshTriangle& triangle : triangles) {
        triangle.localA = Scale(triangle.localA, scale);
        triangle.localB = Scale(triangle.localB, scale);
        triangle.localC = Scale(triangle.localC, scale);
    }
}

void ApplyOffset(std::vector<RenderMeshTriangle>& triangles, const RenderFloat3& offset) {
    if (Length(offset) <= 0.0001f) {
        return;
    }

    for (RenderMeshTriangle& triangle : triangles) {
        triangle.localA = Subtract(triangle.localA, offset);
        triangle.localB = Subtract(triangle.localB, offset);
        triangle.localC = Subtract(triangle.localC, offset);
    }
}

} // namespace

bool ImportGltfModel(
    const std::string& filePath,
    const ImportedModelOptions& options,
    ImportedModelResult& outResult,
    std::string& errorMessage) {

    outResult = {};
    errorMessage.clear();
    const fs::path sourcePath = fs::path(filePath);
    if (filePath.empty() || !fs::exists(sourcePath)) {
        errorMessage = "The selected model file does not exist.";
        return false;
    }

    json root;
    std::vector<unsigned char> glbBinChunk;
    const bool isBinaryContainer = sourcePath.extension() == ".glb";
    if (isBinaryContainer) {
        if (!ParseGlbFile(sourcePath, root, glbBinChunk, errorMessage)) {
            return false;
        }
    } else {
        if (!ParseGltfFile(sourcePath, root, errorMessage)) {
            return false;
        }
    }

    std::vector<std::vector<unsigned char>> buffers;
    if (!ResolveBuffers(sourcePath, root, glbBinChunk, buffers, errorMessage)) {
        return false;
    }

    const std::vector<BufferViewRecord> bufferViews = ParseBufferViews(root);
    const std::vector<AccessorRecord> accessors = ParseAccessors(root);

    std::vector<ImageRecord> images;
    if (!ResolveImages(sourcePath, root, bufferViews, buffers, images, errorMessage)) {
        return false;
    }
    const std::vector<TextureRecord> textures = ParseTextures(root);

    ImportedModelPayload& outPayload = outResult.payload;
    ImportedModelDiagnostics& diagnostics = outResult.diagnostics;
    outPayload.assetLabel = sourcePath.stem().string();
    diagnostics.assetLabel = outPayload.assetLabel;
    diagnostics.sourcePath = sourcePath.string();
    diagnostics.scaleMode = options.scaleMode;

    RenderImportedAsset importedAsset;
    importedAsset.name = outPayload.assetLabel.empty() ? "Imported Asset" : outPayload.assetLabel;
    importedAsset.sourcePath = sourcePath.string();
    importedAsset.binaryContainer = isBinaryContainer;
    outPayload.importedAssets.push_back(std::move(importedAsset));

    outPayload.materials = ParseMaterials(root, textures, images, outPayload.importedTextures);
    diagnostics.materialCount = static_cast<int>(outPayload.materials.size());
    diagnostics.textureCount = static_cast<int>(outPayload.importedTextures.size());
    const json imageValues = root.value("images", json::array());
    if (imageValues.is_array() && static_cast<int>(images.size()) < static_cast<int>(imageValues.size())) {
        diagnostics.warnings.push_back("Some referenced images could not be decoded and were skipped.");
    }

    const int fallbackMaterialIndex = 0;
    const std::vector<MeshRecord> parsedMeshes = ParseMeshes(root, accessors, bufferViews, buffers, fallbackMaterialIndex, errorMessage);
    if (!errorMessage.empty()) {
        return false;
    }
    const std::vector<NodeRecord> nodes = ParseNodes(root);

    std::vector<int> rootNodes;
    const json scenes = root.value("scenes", json::array());
    const int activeSceneIndex = root.value("scene", scenes.empty() ? -1 : 0);
    if (activeSceneIndex >= 0 && activeSceneIndex < static_cast<int>(scenes.size())) {
        const json& activeScene = scenes[static_cast<std::size_t>(activeSceneIndex)];
        const json sceneNodes = activeScene.value("nodes", json::array());
        for (const json& nodeValue : sceneNodes) {
            rootNodes.push_back(nodeValue.get<int>());
        }
    } else {
        rootNodes.resize(nodes.size());
        for (int nodeIndex = 0; nodeIndex < static_cast<int>(nodes.size()); ++nodeIndex) {
            rootNodes[static_cast<std::size_t>(nodeIndex)] = nodeIndex;
        }
    }

    std::vector<RenderMeshTriangle> combinedTriangles;
    int emittedPartCount = 0;
    std::function<void(int, const Mat4&)> emitNode = [&](int nodeIndex, const Mat4& parentMatrix) {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) {
            return;
        }
        const NodeRecord& node = nodes[static_cast<std::size_t>(nodeIndex)];
        const Mat4 worldMatrix = Multiply(parentMatrix, node.localMatrix);

        if (node.mesh >= 0 && node.mesh < static_cast<int>(parsedMeshes.size())) {
            const MeshRecord& sourceMesh = parsedMeshes[static_cast<std::size_t>(node.mesh)];
            if (!sourceMesh.triangles.empty()) {
                AppendTransformedMeshTriangles(sourceMesh, worldMatrix, combinedTriangles);
                emittedPartCount += 1;
            }
        }

        for (int childIndex : node.children) {
            emitNode(childIndex, worldMatrix);
        }
    };

    for (int rootNode : rootNodes) {
        emitNode(rootNode, IdentityMatrix());
    }

    if (combinedTriangles.empty()) {
        errorMessage = "The selected glTF scene does not contain any supported mesh geometry.";
        return false;
    }

    RenderMeshDefinition importedMesh = BuildRenderMeshDefinition(
        outPayload.assetLabel.empty() ? "Imported Mesh" : outPayload.assetLabel,
        std::move(combinedTriangles));
    importedMesh.sourceAssetIndex = 0;
    importedMesh.sourceMeshName = outPayload.assetLabel;

    RenderBounds importedBounds = importedMesh.localBounds;
    const RenderFloat3 extent = BoundsExtent(importedBounds);
    const float maxExtent = std::max({ extent.x, extent.y, extent.z, 0.0f });
    if (maxExtent <= 0.0001f) {
        errorMessage = "The selected glTF scene imported with near-zero bounds.";
        return false;
    }

    float appliedScale = 1.0f;
    if (options.scaleMode == ImportedModelScaleMode::AutoFit) {
        appliedScale = std::max(options.autoFitTargetExtent, 0.01f) / maxExtent;
        ApplyUniformScale(importedMesh.triangles, appliedScale);
        importedMesh = BuildRenderMeshDefinition(importedMesh.name, std::move(importedMesh.triangles));
        importedMesh.sourceAssetIndex = 0;
        importedMesh.sourceMeshName = outPayload.assetLabel;
        importedBounds = importedMesh.localBounds;
    }

    const RenderFloat3 scaledCenter = BoundsCentroid(importedBounds);
    const RenderFloat3 rebaseOffset = MakeRenderFloat3(
        scaledCenter.x,
        importedBounds.min.y,
        scaledCenter.z);
    ApplyOffset(importedMesh.triangles, rebaseOffset);
    importedMesh = BuildRenderMeshDefinition(importedMesh.name, std::move(importedMesh.triangles));
    importedMesh.sourceAssetIndex = 0;
    importedMesh.sourceMeshName = outPayload.assetLabel;

    diagnostics.partCount = emittedPartCount;
    diagnostics.triangleCount = static_cast<int>(importedMesh.triangles.size());
    diagnostics.appliedScale = appliedScale;
    diagnostics.localBoundsMin = { importedMesh.localBounds.min.x, importedMesh.localBounds.min.y, importedMesh.localBounds.min.z };
    diagnostics.localBoundsMax = { importedMesh.localBounds.max.x, importedMesh.localBounds.max.y, importedMesh.localBounds.max.z };

    outPayload.importedMeshes.push_back(std::move(importedMesh));

    Primitive primitive;
    primitive.name = outPayload.assetLabel.empty() ? "Imported Mesh" : outPayload.assetLabel;
    primitive.type = PrimitiveType::ImportedMesh;
    primitive.meshIndex = 0;
    primitive.materialId = 0;
    primitive.importedMaterialMode = ImportedMaterialMode::UseImported;
    primitive.importedMaterialBlend = 0.5f;
    primitive.colorTint = { 1.0f, 1.0f, 1.0f };
    primitive.transform.translation = { 0.0f, 0.0f, 0.0f };
    primitive.transform.rotationDegrees = { 0.0f, 0.0f, 0.0f };
    primitive.transform.scale = { 1.0f, 1.0f, 1.0f };
    primitive.visible = true;
    primitive.importedAssetLabel = diagnostics.assetLabel;
    primitive.importedPartCount = diagnostics.partCount;
    primitive.importedTriangleCount = diagnostics.triangleCount;
    primitive.importedMaterialCount = diagnostics.materialCount;
    primitive.importedAppliedScale = diagnostics.appliedScale;
    primitive.importedScaleMode = diagnostics.scaleMode;
    primitive.importedLocalBoundsMin = diagnostics.localBoundsMin;
    primitive.importedLocalBoundsMax = diagnostics.localBoundsMax;
    primitive.importedWarningText.clear();
    for (std::size_t warningIndex = 0; warningIndex < diagnostics.warnings.size(); ++warningIndex) {
        const std::string& warning = diagnostics.warnings[warningIndex];
        if (warning.empty()) {
            continue;
        }
        if (!primitive.importedWarningText.empty()) {
            primitive.importedWarningText += " | ";
        }
        primitive.importedWarningText += warning;
    }
    outPayload.primitives.push_back(std::move(primitive));

    return true;
}

} // namespace RenderFoundation
