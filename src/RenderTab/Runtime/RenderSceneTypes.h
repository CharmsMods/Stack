#pragma once

enum class RenderBackgroundMode {
    Gradient = 0,
    Checker,
    Grid,
    Black
};

enum class RenderValidationSceneId {
    SphereStudy = 0,
    TriangleCluster,
    MixedDebug,
    MeshInstancing,
    SunSkyStudy,
    EmissiveShowcase,
    DepthOfFieldStudy,
    GlassSlabStudy,
    WindowPaneStudy,
    GlassSphereStudy,
    TintedThicknessRamp,
    FrostedPanelStudy,
    FogBeamStudy,
    CornellBox,
    Custom
};
