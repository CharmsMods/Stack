#pragma once
#include "Editor/NodeGraph/NodeGraphPayloads.h"

namespace EditorNodeGraph {
// A recognized managed image retains its original bytes, a display-linear
// master, and an SDR rendition used by ordinary graph previews.
bool DecodeManagedImagePayload(const std::vector<unsigned char>& bytes, ImagePayload& payload);
bool ReadImageFilePayload(const std::string& path, ImagePayload& payload);
}
