#include "WorkspacePreviewCache.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>

namespace Stack::Renderer {

const WorkspacePreviewCache::Entry* WorkspacePreviewCache::Find(
    std::uint64_t workspace, int rootView) const {
    if (!workspace || rootView < 0) return nullptr;
    for (const auto& entry : entries)
        if (entry.workspace == workspace && entry.rootView == rootView && entry.texture)
            return &entry;
    return nullptr;
}

const WorkspacePreviewCache::Entry* WorkspacePreviewCache::Use(
    std::uint64_t workspace, int rootView) {
    for (auto& entry : entries) {
        if (workspace && entry.workspace == workspace && entry.rootView == rootView && entry.texture) {
            entry.used = ++clock;
            return &entry;
        }
    }
    return nullptr;
}

void WorkspacePreviewCache::Capture(unsigned int sourceFramebuffer,
    std::uint64_t workspace, int rootView, int left, int bottom, int right, int top) {
    const int sourceWidth = right - left, sourceHeight = top - bottom;
    if (!sourceFramebuffer || !workspace || rootView < 0 || sourceWidth <= 0 || sourceHeight <= 0)
        return;

    Entry* entry = nullptr;
    for (auto& candidate : entries)
        if (candidate.workspace == workspace) { entry = &candidate; break; }
    if (!entry) {
        entry = &*std::min_element(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.used < b.used; });
    }
    const float scale = std::min(1.f,
        float(MaximumEdge) / float(std::max(sourceWidth, sourceHeight)));
    const int w = std::max(1, int(std::lround(sourceWidth * scale)));
    const int h = std::max(1, int(std::lround(sourceHeight * scale)));
    if (entry->width != w || entry->height != h || !entry->framebuffer) {
        Release(*entry);
        entry->texture = GLHelpers::CreateTextureFromPixels(nullptr, w, h, 4, false);
        entry->framebuffer = entry->texture ? GLHelpers::CreateFBO(entry->texture) : 0;
        if (!entry->framebuffer) { Release(*entry); return; }
        entry->width = w;
        entry->height = h;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, entry->framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(left, bottom, right, top, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    entry->workspace = workspace;
    entry->rootView = rootView;
    entry->sourceWidth = sourceWidth;
    entry->sourceHeight = sourceHeight;
    entry->used = ++clock;
}

void WorkspacePreviewCache::Release(Entry& entry) {
    if (entry.framebuffer) glDeleteFramebuffers(1, &entry.framebuffer);
    if (entry.texture) glDeleteTextures(1, &entry.texture);
    entry = {};
}

void WorkspacePreviewCache::Forget(std::uint64_t workspace) {
    for (auto& entry : entries)
        if (entry.workspace == workspace) Release(entry);
}

void WorkspacePreviewCache::Clear() {
    for (auto& entry : entries) Release(entry);
    clock = 0;
}

} // namespace Stack::Renderer
