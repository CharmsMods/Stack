#pragma once

#include <string>
#include <vector>

enum class RenderImportedTextureSourceKind {
    None = 0,
    ExternalFile,
    EmbeddedAssetImage
};

enum class RenderTextureSemantic {
    BaseColor = 0,
    MetallicRoughness,
    Emissive,
    Normal
};

struct RenderMaterialTextureRef {
    int textureIndex = -1;
    int uvSet = 0;

    bool IsValid() const { return textureIndex >= 0; }
};

struct RenderImportedTexture {
    std::string name;
    std::string sourcePath;
    std::string sourceUri;
    RenderImportedTextureSourceKind sourceKind = RenderImportedTextureSourceKind::None;
    RenderTextureSemantic semantic = RenderTextureSemantic::BaseColor;
    int sourceAssetIndex = -1;
    int sourceImageIndex = -1;
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<unsigned char> pixels;
};

struct RenderImportedAsset {
    std::string name;
    std::string sourcePath;
    bool binaryContainer = false;
};
