#pragma once

namespace Stack::Workspace {
enum class Presentation { Interactive, Preview };
// UI-thread scoped presentation policy. Background workers never consult it.
inline thread_local Presentation presentation = Presentation::Interactive;
inline bool IsPreview() { return presentation == Presentation::Preview; }
class PresentationScope {
public:
    explicit PresentationScope(bool preview) : previous(presentation) {
        presentation = preview ? Presentation::Preview : Presentation::Interactive;
    }
    ~PresentationScope() { presentation = previous; }
private:
    Presentation previous;
};
}
