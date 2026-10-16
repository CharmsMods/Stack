#pragma once
#include "Assets/CreatedAsset.h"
#include "Renderer/ScopedGLObjects.h"
#include <string>
#include <unordered_map>

namespace Stack::Editor {
// Bounded display previews only. This cache never keeps a capture's full raster
// alive or changes the numerical data used by the graph.
class RawAssetPreviewCache {
public:
    void Synchronize(const std::string& document, const Assets::Library& assets);
    unsigned int Texture(const Assets::Record& asset);
    void Clear() { m_Entries.clear(); m_Document.clear(); }
private:
    struct Entry {
        std::weak_ptr<const Assets::FloatData> data;
        Assets::Kind kind{};
        int workingSpace = 0;
        std::uint64_t lastUse = 0;
        Renderer::ScopedGLTexture texture;
    };
    std::string m_Document;
    std::uint64_t m_Use = 0;
    std::unordered_map<std::string,Entry> m_Entries;
};
}
