#pragma once

namespace Stack::RawWorkspace { struct SourceRecord; }
namespace Raw { struct RawMetadata; }

namespace Stack::Editor::RawLabInternal {

// Presentation only. Thumbnail loading prepares the immutable camera metadata.
class GalleryDetails {
public:
    void Render(const RawWorkspace::SourceRecord* source, const Raw::RawMetadata* metadata);
};

} // namespace Stack::Editor::RawLabInternal
