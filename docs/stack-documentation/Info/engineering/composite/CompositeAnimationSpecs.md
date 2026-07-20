# Deep Research: Animation & Video Export in Composite Module

This document provides a technical deep-dive into the Composite module's architecture to support the implementation of keyframe animation and MP4 export.

## 1. Current State Analysis

### Data Structure: `CompositeLayer` (`src/Composite/CompositeModule.h`)
Layers currently have static spatial properties:
```cpp
float x, y;
float scaleX, scaleY;
float rotation;
float opacity;
```
These properties are manipulated directly via ImGui widgets and mouse interactions in `CompositeModule::RenderStage()`.

### Rendering Pipeline: `BuildExportRaster` (`src/Composite/CompositeModule.cpp`)
The export process is currently a single-frame operation:
1. `BuildExportRaster` calculates world bounds.
2. `RasterizeLayersToTopLeftRgba` iterates through layers (sorted by Z).
3. `SampleLayerPixelBilinear` and `CompositeSourceOver` are used to blend pixels on the CPU.
4. `stbi_write_png` saves the final buffer.

## 2. Research Goals for Animation Implementation

### A. Keyframe Engine
- **Interpolation**: Research efficient ways to handle linear and easing (cubic/bezier) interpolation between `CompositeKeyframe` points.
- **Track Management**: How to efficiently store and query keyframes. A `std::map<int, float>` (where int is frame number) might be sufficient for small projects, but a sorted `std::vector` with binary search is better for performance.
- **Serialization**: The `nlohmann::json` persistence logic in `BuildProjectDocumentForSave` must be updated to handle nested keyframe arrays for each animatable property.

### B. Timeline UI
- **Playhead Logic**: Synchronization between system time (using `ImGui::GetTime()` or `glfwGetTime()`) and the discrete frame count.
- **Interactive Scrubbing**: Handling real-time updates of the `CompositeLayer` properties as the user drags the playhead.
- **Performance**: Ensuring that `UpdateAnimation()` doesn't cause stutter during playback.

### C. FFmpeg Integration
- **Pipe Interface**: Using `_popen` (Windows) to open a write-pipe to `ffmpeg.exe`.
- **Command String Construction**: 
    `ffmpeg -y -f rawvideo -pix_fmt rgba -s {W}x{H} -r {FPS} -i - -c:v libx264 -pix_fmt yuv420p {Output}.mp4`
- **Synchronization**: How to handle the blocking nature of the pipe write. Implementing a progress modal that prevents UI interaction during export is recommended.

## 3. Implementation Companion Plan Outline

1. **Phase 1: Foundation**
    - Modify `CompositeLayer` struct.
    - Implement `GetInterpolatedValue()`.
    - Implement `UpdateAnimation(int frame)`.

2. **Phase 2: Timeline UI**
    - Create `RenderTimelineWindow()`.
    - Implement basic play/pause/stop and scrubbing.
    - Add keyframe "diamonds" in the UI.

3. **Phase 3: Video Backend**
    - Implement the frame-by-frame loop for export.
    - Implement the FFmpeg pipe logic.
    - Add error handling for "FFmpeg not found".

## 4. References
- **ImGui**: For timeline widget patterns.
- **stb_image_write.h**: For reference on pixel buffer formats.
- **StackBinaryFormat.h**: For serialization patterns.
