#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Stack::Renderer {

// Presentation-only GPU snapshots. Entries never retain editors, project
// documents, processing jobs or references to the source framebuffer.
class WorkspacePreviewCache {
public:
    WorkspacePreviewCache() = default;
    WorkspacePreviewCache(const WorkspacePreviewCache&) = delete;
    WorkspacePreviewCache& operator=(const WorkspacePreviewCache&) = delete;
    struct Entry {
        std::uint64_t workspace = 0;
        int rootView = -1;
        unsigned int texture = 0, framebuffer = 0;
        int width = 0, height = 0;
        int sourceWidth = 0, sourceHeight = 0;
        std::uint64_t used = 0;
    };

    const Entry* Find(std::uint64_t workspace, int rootView) const;
    const Entry* Use(std::uint64_t workspace, int rootView);
    // Coordinates are framebuffer pixels with a bottom-left origin. The
    // calling compositor restores GL bindings after capture.
    void Capture(unsigned int sourceFramebuffer, std::uint64_t workspace,
        int rootView, int left, int bottom, int right, int top);
    void Forget(std::uint64_t workspace);
    void Clear();

private:
    static constexpr std::size_t Capacity = 6;
    static constexpr int MaximumEdge = 1600;
    static void Release(Entry& entry);
    std::array<Entry, Capacity> entries{};
    std::uint64_t clock = 0;
};

} // namespace Stack::Renderer
