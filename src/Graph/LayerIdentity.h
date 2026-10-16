#pragma once

// Stable effect identities shared by graph records and execution adapters.
enum class LayerType {
    Brightness,
    Contrast,
    Saturation,
    Warmth,
    Sharpen,
    ColorGrade,
    ToneMapper,
    ToneCurve,
    ShadowsHighlights,
    ToneEqualizer,
    ViewTransform,
    Crop,
    Rotate,
    Flip,
    BoxBlur,
    GaussianBlur,
    Noise,
    Vignette,
    ChromaticAberration,
    LensDistortion,
    TiltShiftBlur,
    CellShading,
    PaletteReconstructor,
    AiryBloom,
    BilateralFilter,
    ClassicalRgbDenoise,
    SceneDenoise,
    NonLocalMeansDenoise,
    MedianDenoise,
    MeanDenoise,
    Halftoning,
    HankelBlur,
    GlareRays,
    Pixelation,
    AlphaHandling,
    BackgroundPatcher,
    TextOverlay
};

enum class LayerLifecycleStatus {
    Stable,
    NeedsFix,
    Experimental,
    Deprecated,
    Hidden
};

enum class LayerChannelPolicy {
    ChannelSafe,
    ChannelUsefulWithWarning,
    FullImagePreferred,
    FullImageOnly,
    ReworkBeforeExpose
};

