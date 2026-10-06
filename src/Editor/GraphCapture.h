#pragma once

#include "App/settings/AppearanceTheme.h"
#include "Utils/SavePathConfirmation.h"

#include <cstdint>
#include <string>
#include <vector>

namespace EditorNodeGraph {
class Graph;
}

namespace Stack::EditorGraphCapture {

enum class Scope {
    VisibleView = 0,
    EntireGraph = 1
};

enum class Background {
    CurrentAppearance = 0,
    SolidTheme = 1,
    Transparent = 2
};

enum class NodeState {
    AsShown = 0,
    ExpandAll = 1,
    CollapseAll = 2
};

enum class Format {
    Png = 0,
    Bmp = 1
};

enum class ResolutionDriver {
    Width = 0,
    Height = 1
};

using GraphCaptureScope = Scope;
using GraphCaptureBackground = Background;
using GraphCaptureNodeState = NodeState;
using GraphCaptureFormat = Format;
using GraphCaptureResolutionDriver = ResolutionDriver;

struct Settings {
    Scope scope = Scope::EntireGraph;
    Background background = Background::CurrentAppearance;
    NodeState nodeState = NodeState::AsShown;
    Format format = Format::Png;
    ResolutionDriver resolutionDriver = ResolutionDriver::Width;
    int width = 7680;
    int height = 4320;
    bool resolutionInitialized = false;
    bool showGrid = true;
    float paddingPercent = 5.0f;
    // Empty means the current, possibly edited, working theme.
    std::string themePresetId;
};

struct GraphViewportSnapshot {
    float logicalWidth = 0.0f;
    float logicalHeight = 0.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;

    bool IsValid() const {
        return logicalWidth > 1.0f && logicalHeight > 1.0f && zoom > 0.0f;
    }

    float AspectRatio() const {
        return IsValid() ? logicalWidth / logicalHeight : 1.0f;
    }
};

struct Request {
    Settings settings;
    GraphViewportSnapshot viewport;
    std::string targetPath;
    Stack::FileSave::TargetApproval targetApproval;
    StackAppearance::ThemeDefinition theme;
};

struct Result {
    bool fileSaved = false;
    bool clipboardRequested = false;
    bool clipboardDibV5Published = false;
    bool clipboardPngPublished = false;
    std::string targetPath;
    std::string message;

    bool ClipboardSucceeded() const {
        return clipboardDibV5Published && clipboardPngPublished;
    }

    bool ClipboardPartiallySucceeded() const {
        return clipboardDibV5Published || clipboardPngPublished;
    }
};

using GraphCaptureSettings = Settings;
using GraphCaptureRequest = Request;
using GraphCaptureResult = Result;

struct ResolutionLimits {
    int maxEdge = 16384;
    std::uint64_t maxPixels = 100000000ull;
    int minDrivenEdge = 256;
    int minDerivedEdge = 64;
};

struct FloatBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool valid = false;
};

struct CameraTransform {
    float panX = 0.0f;
    float panY = 0.0f;
    float zoom = 1.0f;
};

void InitializeEightKLongEdge(Settings& settings, float aspectRatio);
void ResolveLinkedResolution(Settings& settings, float aspectRatio);
void ApplyNodeStatePreset(EditorNodeGraph::Graph& graph, NodeState nodeState);
bool ValidateResolution(
    int width,
    int height,
    const ResolutionLimits& limits,
    std::string* errorMessage = nullptr);
CameraTransform FitBoundsToCanvas(
    const FloatBounds& bounds,
    float canvasWidth,
    float canvasHeight,
    float paddingPercent,
    float minZoom = 0.01f,
    float maxZoom = 4.5f);

// Converts OpenGL bottom-left RGBA readback into top-left straight-alpha RGBA.
bool NormalizeReadbackRgba(
    std::vector<unsigned char>& pixels,
    int width,
    int height,
    bool unpremultiplyAlpha);
bool EncodePng(
    const std::vector<unsigned char>& rgba,
    int width,
    int height,
    std::vector<unsigned char>& encoded);
bool EncodeBmp(
    const std::vector<unsigned char>& rgba,
    int width,
    int height,
    std::vector<unsigned char>& encoded);
bool WriteEncodedFile(
    const std::string& path,
    const std::vector<unsigned char>& encoded,
    std::string* errorMessage = nullptr);

} // namespace Stack::EditorGraphCapture
