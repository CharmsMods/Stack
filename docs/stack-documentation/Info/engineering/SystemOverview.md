# Stack System Architecture Overview

This document provides a technical overview of the "Stack" modular studio application. It is designed to serve as a foundation for deep research and the creation of implementation plans by AI systems.

## 1. Project Philosophy
**Stack** is a high-performance, modular creative studio built in C++. It follows a multi-context approach, allowing users to move between asset management (Library), non-destructive image processing (Editor), path-traced rendering (Render), and layer-based composition (Composite).

## 2. Core Components

### AppShell (`src/App/AppShell.h`)
The central coordinator and entry point. It manages the lifecycle of all modules, handles tab switching, and coordinates file-drop events. It owns instances of the five primary modules.

### Library Module (`src/Library/`)
- **Purpose**: Asset and project management.
- **Key Files**: `LibraryModule`, `LibraryManager`.
- **Functionality**: Browsing local projects, importing external assets, and managing a central "Library Bundle" which is a serialized container for metadata and thumbnails.

### Editor Module (`src/Editor/`)
- **Purpose**: Non-destructive, layer-based image editing.
- **Architecture**: 
    - **Layers**: Abstracted through `LayerBase`. Includes over 40 types of image processing filters (e.g., `ColorGrade`, `HankelBlur`, `AiryBloom`).
    - **RenderPipeline**: A GPU-accelerated (OpenGL) pipeline that executes the layer stack in sequence.
    - **Node Graph**: The Editor UI is now driven by `src/Editor/NodeGraph`, with typed sockets for image, mask, value, and analysis flows. Scope/preview links are analysis-only and should not activate or replace the final render source.
    - **State**: Projects are saved as JSON-serialized pipelines with embedded/linked source images and node graph metadata.
    - **Current Stability Notes**: Graph preview/scope reads should evaluate isolated render snapshots instead of mutating the viewport pipeline. File drops into the graph must account for the live graph pan/zoom transform. Layer-node sync should remove stale links after load/delete repairs.

### Composite Module (`src/Composite/`)
- **Purpose**: A spatial canvas for arranging multiple assets (images, shapes, text, and Editor projects).
- **Architecture**:
    - **Layers**: Spatial objects with properties like position, rotation, scale, opacity, and blend modes.
    - **Rendering**: Uses a specialized GPU-backed stage preview and a CPU/GPU rasterizer for final export.
    - **Interactivity**: Supports snapping, grouping, and canvas manipulation.

### Render Tab (`src/RenderTab/`)
- **Purpose**: High-end path tracing and final asset rendering.
- **Architecture**:
    - **Contracts**: Decouples the UI from the rendering backends (`RenderDelegator`, `SceneCompiler`).
    - **Backends**: Likely includes a path tracer for physically-based rendering.

### Bundler (`src/Bundler/`)
- **Purpose**: Packaging and distribution of projects.

## 3. Data Persistence (`src/Persistence/StackBinaryFormat.h`)
The system uses a custom binary format (`.stack` or similar) that encapsulates:
- **ProjectMetadata**: Identification and dimensions.
- **Thumbnails**: Optimized previews for the library.
- **Source Data**: Embedded PNG/RAW bytes.
- **Pipeline Data**: JSON-serialized state of the specific module (e.g., Editor layer stack or Composite layer arrangement).

## 4. Technology Stack
- **Languages**: C++17/20.
- **Graphics**: OpenGL 4.3+ (Core Profile).
- **UI**: Dear ImGui (Docking Branch).
- **Serialization**: nlohmann/json.
- **Utilities**: stb (image, image_write, truetype), GLFW.

## 5. Potential for Extension (AI Research Topics)
- **Animation System**: Introducing time-based keyframes to the Composite module.
- **Video Rendering**: Integrating FFmpeg to pipe frame sequences from the Composite stage to MP4 containers.
- **AI-Assisted Masking**: Adding neural-network-based background removal or segmentation to the Editor module.
- **Cloud Sync**: Implementing a remote asset backend for the Library module.
