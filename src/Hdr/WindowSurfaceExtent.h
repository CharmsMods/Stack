#pragma once

namespace Stack::Hdr {
enum class WindowSurfaceChange { Suspended, Unchanged, Resized };

// A zero-sized drawable is a temporary suspension, not a new image extent or
// loss of HDR capability. Retain the last allocated size until a real drawable
// returns; restoring to that size must not resize or destroy its resources.
inline WindowSurfaceChange UpdateWindowSurfaceExtent(
    int requestedWidth, int requestedHeight, int& width, int& height) {
    if (requestedWidth < 1 || requestedHeight < 1)
        return WindowSurfaceChange::Suspended;
    const bool resized = requestedWidth != width || requestedHeight != height;
    width = requestedWidth;
    height = requestedHeight;
    return resized ? WindowSurfaceChange::Resized : WindowSurfaceChange::Unchanged;
}
}
