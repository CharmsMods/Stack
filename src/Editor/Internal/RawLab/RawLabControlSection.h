#pragma once

namespace Stack::Editor::RawLabInternal {

// Presentation only. Both hosts edit the same operation through its context.
enum class RawLabControlSection { All, Graph, Settings };

inline bool ShowsRawLabGraph(RawLabControlSection section) {
    return section != RawLabControlSection::Settings;
}
inline bool ShowsRawLabSettings(RawLabControlSection section) {
    return section != RawLabControlSection::Graph;
}

} // namespace Stack::Editor::RawLabInternal
