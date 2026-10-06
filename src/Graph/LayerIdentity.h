#pragma once

// Stable effect identities shared by graph records and execution adapters.
enum class LayerType {
    Brightness,
    Contrast,
    Saturation,
    Warmth,
    Sharpen,
    ColorGrade,
    HDR,
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
    OrderedDither8x8,
    ErrorDiffusionDither,
    WhiteNoiseDither,
    OrderedDither4x4,
    OrderedDither2x2,
    InterleavedGradientDither,
    DctCompression,
    ChromaSubsampleCompression,
    WaveletCompression,
    CellShading,
    HeatwaveDistortion,
    RippleDistortion,
    PaletteReconstructor,
    EdgeOverlay,
    EdgeSaturationMask,
    AiryBloom,
    ImageBreaks,
    AnalogVideo,
    BilateralFilter,
    ClassicalRgbDenoise,
    SceneDenoise,
    LinearRgbNeuralDenoise,
    NonLocalMeansDenoise,
    MedianDenoise,
    MeanDenoise,
    Halftoning,
    HankelBlur,
    GlareRays,
    JpegBlocks,
    Pixelation,
    ColorBleed,
    AlphaHandling,
    BackgroundPatcher,
    Expander,
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

