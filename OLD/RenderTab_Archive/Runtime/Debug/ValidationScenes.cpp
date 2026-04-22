#include "ValidationScenes.h"

#include <array>
#include <string>

namespace {

const std::vector<RenderValidationSceneOption>& GetSceneOptionsInternal() {
    static const std::vector<RenderValidationSceneOption> options = {
        {
            RenderValidationSceneId::SphereStudy,
            "Sphere Study",
            "Analytic spheres over a triangle floor for ray-sphere validation.",
            RenderBackgroundMode::Gradient,
            true
        },
        {
            RenderValidationSceneId::TriangleCluster,
            "Triangle Cluster",
            "Colorful triangle-only scene for triangle intersection and bounds checks.",
            RenderBackgroundMode::Checker,
            true
        },
        {
            RenderValidationSceneId::MixedDebug,
            "Mixed Debug",
            "Combined spheres and triangles for BVH traversal and mixed primitive validation.",
            RenderBackgroundMode::Grid,
            true
        },
        {
            RenderValidationSceneId::MeshInstancing,
            "Mesh Instancing",
            "Reusable mesh definitions with multiple transformed instances flattened into the shared BVH/debug path.",
            RenderBackgroundMode::Grid,
            true
        },
        {
            RenderValidationSceneId::SunSkyStudy,
            "Sun / Sky Study",
            "Open world-lit validation scene for checking the baseline sun, sky, and outdoor-material response.",
            RenderBackgroundMode::Gradient,
            true
        },
        {
            RenderValidationSceneId::EmissiveShowcase,
            "Emissive Showcase",
            "Open box scene with emissive area lighting and glossy-metal material checks for baseline path-trace realism work.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::DepthOfFieldStudy,
            "Depth Of Field Study",
            "Layered emissive-box scene with foreground, focus-plane, and background subjects for thin-lens camera validation.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::GlassSlabStudy,
            "Glass Slab",
            "Thick clear-glass slab in front of colored wall markers for first smooth-dielectric refraction checks.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::WindowPaneStudy,
            "Window Pane",
            "Thin solid glass pane with emissive-box lighting for self-intersection and flat-interface sanity checks.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::GlassSphereStudy,
            "Glass Sphere Study",
            "Clear-glass sphere over a patterned backdrop for inversion, edge behavior, and total-internal-reflection checks.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::TintedThicknessRamp,
            "Tinted Thickness Ramp",
            "Tinted solid-glass blocks with different thicknesses for Beer-Lambert absorption validation.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::FrostedPanelStudy,
            "Frosted Panel",
            "Rough dielectric panel in front of colored markers for frosted-glass transport validation.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::FogBeamStudy,
            "Fog Beam",
            "Scene-wide fog with a strong sun beam and emissive accents for first homogeneous-medium validation.",
            RenderBackgroundMode::Black,
            false
        },
        {
            RenderValidationSceneId::CornellBox,
            "Cornell Box",
            "Classic Cornell Box with emissive ceiling, red/green walls, a tall box, and a sphere for path trace validation.",
            RenderBackgroundMode::Black,
            false
        }
    };

    return options;
}

RenderTriangle MakeTriangle(
    const char* name,
    RenderFloat3 a,
    RenderFloat3 b,
    RenderFloat3 c,
    int materialIndex,
    RenderFloat3 albedoTint) {
    const RenderFloat3 centroid = MakeRenderFloat3(
        (a.x + b.x + c.x) / 3.0f,
        (a.y + b.y + c.y) / 3.0f,
        (a.z + b.z + c.z) / 3.0f);

    RenderTriangle triangle;
    triangle.name = name;
    triangle.transform.translation = centroid;
    triangle.localA = MakeRenderFloat3(a.x - centroid.x, a.y - centroid.y, a.z - centroid.z);
    triangle.localB = MakeRenderFloat3(b.x - centroid.x, b.y - centroid.y, b.z - centroid.z);
    triangle.localC = MakeRenderFloat3(c.x - centroid.x, c.y - centroid.y, c.z - centroid.z);
    triangle.materialIndex = materialIndex;
    triangle.albedoTint = albedoTint;
    return triangle;
}

RenderMeshTriangle MakeMeshTriangle(
    const char* name,
    RenderFloat3 a,
    RenderFloat3 b,
    RenderFloat3 c,
    int materialIndex,
    RenderFloat3 albedoTint) {
    RenderMeshTriangle triangle;
    triangle.name = name;
    triangle.localA = a;
    triangle.localB = b;
    triangle.localC = c;
    triangle.materialIndex = materialIndex;
    triangle.albedoTint = albedoTint;
    return triangle;
}

RenderSphere MakeSphere(const char* name, RenderFloat3 center, float radius, int materialIndex, RenderFloat3 albedoTint) {
    RenderSphere sphere;
    sphere.name = name;
    sphere.transform.translation = center;
    sphere.localCenter = MakeRenderFloat3(0.0f, 0.0f, 0.0f);
    sphere.radius = radius;
    sphere.materialIndex = materialIndex;
    sphere.albedoTint = albedoTint;
    return sphere;
}

void AppendQuad(
    std::vector<RenderTriangle>& triangles,
    const char* name,
    RenderFloat3 a,
    RenderFloat3 b,
    RenderFloat3 c,
    RenderFloat3 d,
    int materialIndex,
    RenderFloat3 albedoTint) {
    const std::string baseName(name);
    triangles.push_back(MakeTriangle((baseName + " A").c_str(), a, b, c, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " B").c_str(), c, b, d, materialIndex, albedoTint));
}

void AppendBox(
    std::vector<RenderTriangle>& triangles,
    const char* name,
    RenderFloat3 minimumBounds,
    RenderFloat3 maximumBounds,
    int materialIndex,
    RenderFloat3 albedoTint) {
    const RenderFloat3 p000 = MakeRenderFloat3(minimumBounds.x, minimumBounds.y, minimumBounds.z);
    const RenderFloat3 p001 = MakeRenderFloat3(minimumBounds.x, minimumBounds.y, maximumBounds.z);
    const RenderFloat3 p010 = MakeRenderFloat3(minimumBounds.x, maximumBounds.y, minimumBounds.z);
    const RenderFloat3 p011 = MakeRenderFloat3(minimumBounds.x, maximumBounds.y, maximumBounds.z);
    const RenderFloat3 p100 = MakeRenderFloat3(maximumBounds.x, minimumBounds.y, minimumBounds.z);
    const RenderFloat3 p101 = MakeRenderFloat3(maximumBounds.x, minimumBounds.y, maximumBounds.z);
    const RenderFloat3 p110 = MakeRenderFloat3(maximumBounds.x, maximumBounds.y, minimumBounds.z);
    const RenderFloat3 p111 = MakeRenderFloat3(maximumBounds.x, maximumBounds.y, maximumBounds.z);

    const std::string baseName(name);
    triangles.push_back(MakeTriangle((baseName + " -X A").c_str(), p000, p001, p010, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " -X B").c_str(), p001, p011, p010, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " +X A").c_str(), p100, p110, p101, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " +X B").c_str(), p101, p110, p111, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " -Y A").c_str(), p000, p100, p001, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " -Y B").c_str(), p001, p100, p101, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " +Y A").c_str(), p010, p011, p110, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " +Y B").c_str(), p011, p111, p110, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " -Z A").c_str(), p000, p010, p100, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " -Z B").c_str(), p010, p110, p100, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " +Z A").c_str(), p001, p101, p011, materialIndex, albedoTint));
    triangles.push_back(MakeTriangle((baseName + " +Z B").c_str(), p011, p101, p111, materialIndex, albedoTint));
}

RenderMaterial MakeSurfaceMaterial(const char* name, RenderFloat3 baseColor, float roughness, float metallic) {
    RenderMaterial material = BuildRenderMaterial(name, baseColor, 0.0f, MakeRenderFloat3(1.0f, 1.0f, 1.0f), roughness, metallic);
    material.surfacePreset = metallic > 0.5f ? RenderSurfacePreset::Metal : RenderSurfacePreset::Diffuse;
    ApplyRenderSurfacePreset(material, material.surfacePreset);
    return material;
}

RenderMaterial MakeDiffuseMaterial(const char* name, RenderFloat3 baseColor) {
    return MakeSurfaceMaterial(name, baseColor, 1.0f, 0.0f);
}

RenderMaterial MakeEmissiveMaterial(
    const char* name,
    RenderFloat3 baseColor,
    RenderFloat3 emissionColor,
    float emissionStrength) {
    RenderMaterial material = BuildRenderMaterial(name, baseColor, emissionStrength, emissionColor, 1.0f, 0.0f);
    material.surfacePreset = RenderSurfacePreset::Emissive;
    ApplyRenderSurfacePreset(material, material.surfacePreset);
    return material;
}

RenderMaterial MakeGlassMaterial(
    const char* name,
    float ior,
    bool thinWalled = false,
    float transmissionRoughness = 0.0f,
    RenderFloat3 absorptionColor = RenderFloat3 { 1.0f, 1.0f, 1.0f },
    float absorptionDistance = 1.0f) {
    RenderMaterial material = BuildRenderMaterial(
        name,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f),
        0.0f,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f),
        0.0f,
        0.0f,
        1.0f,
        ior);
    material.surfacePreset = RenderSurfacePreset::Glass;
    material.thinWalled = thinWalled;
    material.transmissionRoughness = transmissionRoughness;
    material.absorptionColor = absorptionColor;
    material.absorptionDistance = absorptionDistance;
    ApplyRenderSurfacePreset(material, RenderSurfacePreset::Glass);
    material.thinWalled = thinWalled;
    material.transmissionRoughness = transmissionRoughness;
    material.absorptionColor = absorptionColor;
    material.absorptionDistance = absorptionDistance;
    return material;
}

RenderLight MakeSunLight(const char* name, RenderFloat3 rotationDegrees, float intensity, RenderFloat3 color) {
    RenderLight light;
    light.name = name;
    light.type = RenderLightType::Sun;
    light.transform.rotationDegrees = rotationDegrees;
    light.intensity = intensity;
    light.color = color;
    light.range = 10000.0f;
    return light;
}

std::vector<RenderMaterial> BuildGlassStudyMaterials(float ior = 1.5f) {
    return {
        MakeSurfaceMaterial("Study White", MakeRenderFloat3(0.84f, 0.84f, 0.82f), 1.0f, 0.0f),
        MakeSurfaceMaterial("Study Red", MakeRenderFloat3(0.84f, 0.24f, 0.20f), 0.95f, 0.0f),
        MakeSurfaceMaterial("Study Green", MakeRenderFloat3(0.26f, 0.76f, 0.34f), 0.95f, 0.0f),
        MakeEmissiveMaterial("Study Area Light", MakeRenderFloat3(1.0f, 1.0f, 1.0f), MakeRenderFloat3(1.0f, 0.98f, 0.94f), 12.0f),
        MakeGlassMaterial("Clear Glass", ior)
    };
}

void AppendGlassStudyRoom(std::vector<RenderTriangle>& triangles) {
    AppendQuad(
        triangles,
        "Floor",
        MakeRenderFloat3(-1.8f, -0.2f, -2.4f),
        MakeRenderFloat3(3.2f, -0.2f, -2.4f),
        MakeRenderFloat3(-1.8f, -0.2f, 2.4f),
        MakeRenderFloat3(3.2f, -0.2f, 2.4f),
        0,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(
        triangles,
        "Ceiling",
        MakeRenderFloat3(-1.8f, 3.9f, -2.4f),
        MakeRenderFloat3(-1.8f, 3.9f, 2.4f),
        MakeRenderFloat3(3.2f, 3.9f, -2.4f),
        MakeRenderFloat3(3.2f, 3.9f, 2.4f),
        0,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(
        triangles,
        "Left Wall",
        MakeRenderFloat3(-1.8f, -0.2f, -2.4f),
        MakeRenderFloat3(-1.8f, 3.9f, -2.4f),
        MakeRenderFloat3(3.2f, -0.2f, -2.4f),
        MakeRenderFloat3(3.2f, 3.9f, -2.4f),
        1,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(
        triangles,
        "Right Wall",
        MakeRenderFloat3(-1.8f, -0.2f, 2.4f),
        MakeRenderFloat3(3.2f, -0.2f, 2.4f),
        MakeRenderFloat3(-1.8f, 3.9f, 2.4f),
        MakeRenderFloat3(3.2f, 3.9f, 2.4f),
        2,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    const float backWallX = 3.05f;
    const float bottomY = 0.18f;
    const float topY = 3.45f;
    AppendQuad(
        triangles,
        "Backdrop White",
        MakeRenderFloat3(backWallX, bottomY, -2.0f),
        MakeRenderFloat3(backWallX, topY, -2.0f),
        MakeRenderFloat3(backWallX, bottomY, -1.0f),
        MakeRenderFloat3(backWallX, topY, -1.0f),
        0,
        MakeRenderFloat3(0.95f, 0.95f, 0.95f));
    AppendQuad(
        triangles,
        "Backdrop Warm",
        MakeRenderFloat3(backWallX, bottomY, -1.0f),
        MakeRenderFloat3(backWallX, topY, -1.0f),
        MakeRenderFloat3(backWallX, bottomY, 0.0f),
        MakeRenderFloat3(backWallX, topY, 0.0f),
        0,
        MakeRenderFloat3(0.98f, 0.72f, 0.38f));
    AppendQuad(
        triangles,
        "Backdrop Blue",
        MakeRenderFloat3(backWallX, bottomY, 0.0f),
        MakeRenderFloat3(backWallX, topY, 0.0f),
        MakeRenderFloat3(backWallX, bottomY, 1.0f),
        MakeRenderFloat3(backWallX, topY, 1.0f),
        0,
        MakeRenderFloat3(0.38f, 0.62f, 0.98f));
    AppendQuad(
        triangles,
        "Backdrop Green",
        MakeRenderFloat3(backWallX, bottomY, 1.0f),
        MakeRenderFloat3(backWallX, topY, 1.0f),
        MakeRenderFloat3(backWallX, bottomY, 2.0f),
        MakeRenderFloat3(backWallX, topY, 2.0f),
        0,
        MakeRenderFloat3(0.34f, 0.88f, 0.54f));

    AppendQuad(
        triangles,
        "Ceiling Light",
        MakeRenderFloat3(0.20f, 3.84f, -0.80f),
        MakeRenderFloat3(0.20f, 3.84f, 0.80f),
        MakeRenderFloat3(1.80f, 3.84f, -0.80f),
        MakeRenderFloat3(1.80f, 3.84f, 0.80f),
        3,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(
        triangles,
        "Backdrop Light Strip",
        MakeRenderFloat3(3.00f, 1.10f, -0.16f),
        MakeRenderFloat3(3.00f, 2.55f, -0.16f),
        MakeRenderFloat3(2.98f, 1.10f, 0.16f),
        MakeRenderFloat3(2.98f, 2.55f, 0.16f),
        3,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
}

RenderMeshInstance MakeMeshInstance(
    const char* name,
    int meshIndex,
    RenderFloat3 translation,
    RenderFloat3 rotationDegrees,
    RenderFloat3 scale,
    RenderFloat3 colorTint) {
    RenderMeshInstance meshInstance;
    meshInstance.name = name;
    meshInstance.meshIndex = meshIndex;
    meshInstance.transform.translation = translation;
    meshInstance.transform.rotationDegrees = rotationDegrees;
    meshInstance.transform.scale = scale;
    meshInstance.colorTint = colorTint;
    return meshInstance;
}

RenderMeshDefinition BuildPyramidMesh() {
    return BuildRenderMeshDefinition("Pyramid", {
        MakeMeshTriangle("Front Face",
            MakeRenderFloat3(-0.75f, 0.0f, 0.75f),
            MakeRenderFloat3(0.0f, 1.4f, 0.0f),
            MakeRenderFloat3(0.75f, 0.0f, 0.75f),
            0,
            MakeRenderFloat3(0.92f, 0.52f, 0.34f)),
        MakeMeshTriangle("Right Face",
            MakeRenderFloat3(0.75f, 0.0f, 0.75f),
            MakeRenderFloat3(0.0f, 1.4f, 0.0f),
            MakeRenderFloat3(0.75f, 0.0f, -0.75f),
            0,
            MakeRenderFloat3(0.84f, 0.72f, 0.28f)),
        MakeMeshTriangle("Back Face",
            MakeRenderFloat3(0.75f, 0.0f, -0.75f),
            MakeRenderFloat3(0.0f, 1.4f, 0.0f),
            MakeRenderFloat3(-0.75f, 0.0f, -0.75f),
            0,
            MakeRenderFloat3(0.34f, 0.82f, 0.56f)),
        MakeMeshTriangle("Left Face",
            MakeRenderFloat3(-0.75f, 0.0f, -0.75f),
            MakeRenderFloat3(0.0f, 1.4f, 0.0f),
            MakeRenderFloat3(-0.75f, 0.0f, 0.75f),
            0,
            MakeRenderFloat3(0.34f, 0.58f, 0.90f)),
        MakeMeshTriangle("Base A",
            MakeRenderFloat3(-0.75f, 0.0f, 0.75f),
            MakeRenderFloat3(0.75f, 0.0f, 0.75f),
            MakeRenderFloat3(-0.75f, 0.0f, -0.75f),
            0,
            MakeRenderFloat3(0.24f, 0.26f, 0.30f)),
        MakeMeshTriangle("Base B",
            MakeRenderFloat3(0.75f, 0.0f, 0.75f),
            MakeRenderFloat3(0.75f, 0.0f, -0.75f),
            MakeRenderFloat3(-0.75f, 0.0f, -0.75f),
            0,
            MakeRenderFloat3(0.18f, 0.20f, 0.24f))
    });
}

RenderMeshDefinition BuildDiamondMesh() {
    return BuildRenderMeshDefinition("Diamond", {
        MakeMeshTriangle("Top Front",
            MakeRenderFloat3(0.0f, 0.85f, 0.0f),
            MakeRenderFloat3(-0.6f, 0.0f, 0.6f),
            MakeRenderFloat3(0.6f, 0.0f, 0.6f),
            0,
            MakeRenderFloat3(0.92f, 0.42f, 0.64f)),
        MakeMeshTriangle("Top Right",
            MakeRenderFloat3(0.0f, 0.85f, 0.0f),
            MakeRenderFloat3(0.6f, 0.0f, 0.6f),
            MakeRenderFloat3(0.6f, 0.0f, -0.6f),
            0,
            MakeRenderFloat3(0.82f, 0.38f, 0.88f)),
        MakeMeshTriangle("Top Back",
            MakeRenderFloat3(0.0f, 0.85f, 0.0f),
            MakeRenderFloat3(0.6f, 0.0f, -0.6f),
            MakeRenderFloat3(-0.6f, 0.0f, -0.6f),
            0,
            MakeRenderFloat3(0.48f, 0.52f, 0.92f)),
        MakeMeshTriangle("Top Left",
            MakeRenderFloat3(0.0f, 0.85f, 0.0f),
            MakeRenderFloat3(-0.6f, 0.0f, -0.6f),
            MakeRenderFloat3(-0.6f, 0.0f, 0.6f),
            0,
            MakeRenderFloat3(0.34f, 0.72f, 0.96f)),
        MakeMeshTriangle("Bottom Front",
            MakeRenderFloat3(0.0f, -0.85f, 0.0f),
            MakeRenderFloat3(0.6f, 0.0f, 0.6f),
            MakeRenderFloat3(-0.6f, 0.0f, 0.6f),
            0,
            MakeRenderFloat3(0.90f, 0.54f, 0.74f)),
        MakeMeshTriangle("Bottom Right",
            MakeRenderFloat3(0.0f, -0.85f, 0.0f),
            MakeRenderFloat3(0.6f, 0.0f, -0.6f),
            MakeRenderFloat3(0.6f, 0.0f, 0.6f),
            0,
            MakeRenderFloat3(0.80f, 0.46f, 0.94f)),
        MakeMeshTriangle("Bottom Back",
            MakeRenderFloat3(0.0f, -0.85f, 0.0f),
            MakeRenderFloat3(-0.6f, 0.0f, -0.6f),
            MakeRenderFloat3(0.6f, 0.0f, -0.6f),
            0,
            MakeRenderFloat3(0.56f, 0.60f, 0.98f)),
        MakeMeshTriangle("Bottom Left",
            MakeRenderFloat3(0.0f, -0.85f, 0.0f),
            MakeRenderFloat3(-0.6f, 0.0f, 0.6f),
            MakeRenderFloat3(-0.6f, 0.0f, -0.6f),
            0,
            MakeRenderFloat3(0.40f, 0.78f, 0.98f))
    });
}

RenderValidationSceneTemplate BuildSphereStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::SphereStudy;
    scene.label = "Sphere Study";
    scene.description = "Analytic spheres with a triangle floor for ray-sphere testing before transport features exist.";
    scene.defaultBackground = RenderBackgroundMode::Gradient;
    scene.defaultEnvironmentEnabled = true;
    scene.materials = {
        MakeDiffuseMaterial("Diffuse White", MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.spheres = {
        MakeSphere("Hero Sphere", MakeRenderFloat3(0.0f, 0.75f, 0.0f), 0.75f, 0, MakeRenderFloat3(0.88f, 0.44f, 0.38f)),
        MakeSphere("Left Sphere", MakeRenderFloat3(-1.65f, 0.45f, -0.55f), 0.45f, 0, MakeRenderFloat3(0.32f, 0.72f, 0.92f)),
        MakeSphere("Right Sphere", MakeRenderFloat3(1.4f, 0.55f, 0.4f), 0.55f, 0, MakeRenderFloat3(0.92f, 0.82f, 0.38f))
    };

    scene.triangles = {
        MakeTriangle("Floor A",
            MakeRenderFloat3(-4.0f, 0.0f, -4.0f),
            MakeRenderFloat3(4.0f, 0.0f, -4.0f),
            MakeRenderFloat3(-4.0f, 0.0f, 4.0f),
            0,
            MakeRenderFloat3(0.40f, 0.44f, 0.46f)),
        MakeTriangle("Floor B",
            MakeRenderFloat3(4.0f, 0.0f, -4.0f),
            MakeRenderFloat3(4.0f, 0.0f, 4.0f),
            MakeRenderFloat3(-4.0f, 0.0f, 4.0f),
            0,
            MakeRenderFloat3(0.34f, 0.38f, 0.42f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildTriangleClusterScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::TriangleCluster;
    scene.label = "Triangle Cluster";
    scene.description = "Triangle-only debug scene for ray-triangle intersection and triangle-heavy BVH checks.";
    scene.defaultBackground = RenderBackgroundMode::Checker;
    scene.defaultEnvironmentEnabled = true;
    scene.materials = {
        MakeDiffuseMaterial("Diffuse White", MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.triangles = {
        MakeTriangle("Floor A",
            MakeRenderFloat3(-4.0f, -0.1f, -4.0f),
            MakeRenderFloat3(4.0f, -0.1f, -4.0f),
            MakeRenderFloat3(-4.0f, -0.1f, 4.0f),
            0,
            MakeRenderFloat3(0.28f, 0.32f, 0.36f)),
        MakeTriangle("Floor B",
            MakeRenderFloat3(4.0f, -0.1f, -4.0f),
            MakeRenderFloat3(4.0f, -0.1f, 4.0f),
            MakeRenderFloat3(-4.0f, -0.1f, 4.0f),
            0,
            MakeRenderFloat3(0.22f, 0.26f, 0.30f)),
        MakeTriangle("Center Front",
            MakeRenderFloat3(-1.0f, -0.1f, 0.7f),
            MakeRenderFloat3(0.0f, 1.25f, 0.1f),
            MakeRenderFloat3(1.0f, -0.1f, 0.7f),
            0,
            MakeRenderFloat3(0.86f, 0.38f, 0.34f)),
        MakeTriangle("Center Back",
            MakeRenderFloat3(-1.0f, -0.1f, 0.7f),
            MakeRenderFloat3(0.0f, 1.25f, 0.1f),
            MakeRenderFloat3(0.0f, -0.1f, -1.0f),
            0,
            MakeRenderFloat3(0.32f, 0.76f, 0.48f)),
        MakeTriangle("Center Right",
            MakeRenderFloat3(1.0f, -0.1f, 0.7f),
            MakeRenderFloat3(0.0f, 1.25f, 0.1f),
            MakeRenderFloat3(0.0f, -0.1f, -1.0f),
            0,
            MakeRenderFloat3(0.28f, 0.52f, 0.86f)),
        MakeTriangle("Rear Accent",
            MakeRenderFloat3(-1.8f, 0.0f, -1.6f),
            MakeRenderFloat3(-0.6f, 1.0f, -2.0f),
            MakeRenderFloat3(0.3f, 0.1f, -1.5f),
            0,
            MakeRenderFloat3(0.84f, 0.78f, 0.32f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildMixedDebugScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::MixedDebug;
    scene.label = "Mixed Debug";
    scene.description = "Mixed primitives with overlapping spatial hierarchy to validate BVH traversal order and debug views.";
    scene.defaultBackground = RenderBackgroundMode::Grid;
    scene.defaultEnvironmentEnabled = true;
    scene.materials = {
        MakeDiffuseMaterial("Diffuse White", MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.spheres = {
        MakeSphere("Blue Sphere", MakeRenderFloat3(-1.25f, 0.55f, -0.35f), 0.55f, 0, MakeRenderFloat3(0.28f, 0.58f, 0.92f)),
        MakeSphere("Orange Sphere", MakeRenderFloat3(1.15f, 0.8f, 0.25f), 0.8f, 0, MakeRenderFloat3(0.92f, 0.56f, 0.24f))
    };

    scene.triangles = {
        MakeTriangle("Floor A",
            MakeRenderFloat3(-5.0f, -0.2f, -5.0f),
            MakeRenderFloat3(5.0f, -0.2f, -5.0f),
            MakeRenderFloat3(-5.0f, -0.2f, 5.0f),
            0,
            MakeRenderFloat3(0.26f, 0.28f, 0.32f)),
        MakeTriangle("Floor B",
            MakeRenderFloat3(5.0f, -0.2f, -5.0f),
            MakeRenderFloat3(5.0f, -0.2f, 5.0f),
            MakeRenderFloat3(-5.0f, -0.2f, 5.0f),
            0,
            MakeRenderFloat3(0.20f, 0.24f, 0.28f)),
        MakeTriangle("Front Wedge",
            MakeRenderFloat3(-0.5f, -0.2f, 1.6f),
            MakeRenderFloat3(0.8f, 1.1f, 1.1f),
            MakeRenderFloat3(1.8f, -0.2f, 1.7f),
            0,
            MakeRenderFloat3(0.86f, 0.28f, 0.42f)),
        MakeTriangle("Rear Wedge",
            MakeRenderFloat3(-1.8f, -0.2f, -1.4f),
            MakeRenderFloat3(-0.3f, 1.4f, -1.0f),
            MakeRenderFloat3(1.3f, -0.2f, -1.8f),
            0,
            MakeRenderFloat3(0.34f, 0.84f, 0.58f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildMeshInstancingScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::MeshInstancing;
    scene.label = "Mesh Instancing";
    scene.description = "Validation scene for reusable mesh definitions, transformed mesh instances, and flattened multi-model BVH input.";
    scene.defaultBackground = RenderBackgroundMode::Grid;
    scene.defaultEnvironmentEnabled = true;
    scene.materials = {
        MakeDiffuseMaterial("Diffuse White", MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.meshes = {
        BuildPyramidMesh(),
        BuildDiamondMesh()
    };

    scene.meshInstances = {
        MakeMeshInstance(
            "Blue Pyramid",
            0,
            MakeRenderFloat3(-1.75f, -0.15f, 0.55f),
            MakeRenderFloat3(0.0f, 18.0f, 0.0f),
            MakeRenderFloat3(1.00f, 1.10f, 1.00f),
            MakeRenderFloat3(0.58f, 0.78f, 1.00f)),
        MakeMeshInstance(
            "Gold Pyramid",
            0,
            MakeRenderFloat3(1.85f, -0.15f, -0.55f),
            MakeRenderFloat3(0.0f, -34.0f, 0.0f),
            MakeRenderFloat3(0.85f, 0.95f, 0.85f),
            MakeRenderFloat3(1.00f, 0.90f, 0.54f)),
        MakeMeshInstance(
            "Accent Diamond",
            1,
            MakeRenderFloat3(0.15f, 0.95f, 1.15f),
            MakeRenderFloat3(16.0f, 36.0f, -8.0f),
            MakeRenderFloat3(0.78f, 0.78f, 0.78f),
            MakeRenderFloat3(1.00f, 0.72f, 0.92f))
    };

    scene.spheres = {
        MakeSphere("Reference Sphere", MakeRenderFloat3(-0.15f, 0.45f, -1.35f), 0.45f, 0, MakeRenderFloat3(0.32f, 0.60f, 0.94f))
    };

    scene.triangles = {
        MakeTriangle("Floor A",
            MakeRenderFloat3(-5.0f, -0.2f, -5.0f),
            MakeRenderFloat3(5.0f, -0.2f, -5.0f),
            MakeRenderFloat3(-5.0f, -0.2f, 5.0f),
            0,
            MakeRenderFloat3(0.22f, 0.24f, 0.28f)),
        MakeTriangle("Floor B",
            MakeRenderFloat3(5.0f, -0.2f, -5.0f),
            MakeRenderFloat3(5.0f, -0.2f, 5.0f),
            MakeRenderFloat3(-5.0f, -0.2f, 5.0f),
            0,
            MakeRenderFloat3(0.18f, 0.20f, 0.24f)),
        MakeTriangle("Rear Wedge",
            MakeRenderFloat3(-1.1f, -0.2f, -2.0f),
            MakeRenderFloat3(0.2f, 1.05f, -1.7f),
            MakeRenderFloat3(1.5f, -0.2f, -2.2f),
            0,
            MakeRenderFloat3(0.84f, 0.34f, 0.44f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildEmissiveShowcaseScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::EmissiveShowcase;
    scene.label = "Emissive Showcase";
    scene.description = "Open Cornell-style box with an emissive ceiling light plus diffuse and metallic materials for the first non-debug path-trace realism showcase.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = {
        MakeSurfaceMaterial("Matte White", MakeRenderFloat3(0.84f, 0.84f, 0.82f), 1.0f, 0.0f),
        MakeSurfaceMaterial("Matte Red", MakeRenderFloat3(0.84f, 0.24f, 0.20f), 0.95f, 0.0f),
        MakeSurfaceMaterial("Matte Green", MakeRenderFloat3(0.26f, 0.76f, 0.34f), 0.95f, 0.0f),
        MakeSurfaceMaterial("Polished Blue Metal", MakeRenderFloat3(0.28f, 0.56f, 0.92f), 0.14f, 1.0f),
        MakeSurfaceMaterial("Brushed Gold", MakeRenderFloat3(0.92f, 0.74f, 0.36f), 0.24f, 1.0f),
        MakeEmissiveMaterial("Warm Area Light", MakeRenderFloat3(1.0f, 1.0f, 1.0f), MakeRenderFloat3(1.0f, 0.96f, 0.88f), 8.0f)
    };

    scene.spheres = {
        MakeSphere("Blue Sphere", MakeRenderFloat3(0.55f, 0.65f, 0.95f), 0.65f, 3, MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeSphere("Gold Sphere", MakeRenderFloat3(1.55f, 0.95f, -0.75f), 0.95f, 4, MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.triangles = {
        MakeTriangle("Floor A",
            MakeRenderFloat3(-1.6f, -0.2f, -2.5f),
            MakeRenderFloat3(2.8f, -0.2f, -2.5f),
            MakeRenderFloat3(-1.6f, -0.2f, 2.5f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Floor B",
            MakeRenderFloat3(2.8f, -0.2f, -2.5f),
            MakeRenderFloat3(2.8f, -0.2f, 2.5f),
            MakeRenderFloat3(-1.6f, -0.2f, 2.5f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ceiling A",
            MakeRenderFloat3(-1.6f, 3.8f, -2.5f),
            MakeRenderFloat3(-1.6f, 3.8f, 2.5f),
            MakeRenderFloat3(2.8f, 3.8f, -2.5f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ceiling B",
            MakeRenderFloat3(2.8f, 3.8f, -2.5f),
            MakeRenderFloat3(-1.6f, 3.8f, 2.5f),
            MakeRenderFloat3(2.8f, 3.8f, 2.5f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Back Wall A",
            MakeRenderFloat3(2.8f, -0.2f, -2.5f),
            MakeRenderFloat3(2.8f, 3.8f, -2.5f),
            MakeRenderFloat3(2.8f, -0.2f, 2.5f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Back Wall B",
            MakeRenderFloat3(2.8f, 3.8f, -2.5f),
            MakeRenderFloat3(2.8f, 3.8f, 2.5f),
            MakeRenderFloat3(2.8f, -0.2f, 2.5f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Left Wall A",
            MakeRenderFloat3(-1.6f, -0.2f, -2.5f),
            MakeRenderFloat3(-1.6f, 3.8f, -2.5f),
            MakeRenderFloat3(2.8f, -0.2f, -2.5f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Left Wall B",
            MakeRenderFloat3(-1.6f, 3.8f, -2.5f),
            MakeRenderFloat3(2.8f, 3.8f, -2.5f),
            MakeRenderFloat3(2.8f, -0.2f, -2.5f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Right Wall A",
            MakeRenderFloat3(-1.6f, -0.2f, 2.5f),
            MakeRenderFloat3(2.8f, -0.2f, 2.5f),
            MakeRenderFloat3(-1.6f, 3.8f, 2.5f),
            2,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Right Wall B",
            MakeRenderFloat3(-1.6f, 3.8f, 2.5f),
            MakeRenderFloat3(2.8f, -0.2f, 2.5f),
            MakeRenderFloat3(2.8f, 3.8f, 2.5f),
            2,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ceiling Light A",
            MakeRenderFloat3(0.15f, 3.74f, -0.65f),
            MakeRenderFloat3(0.15f, 3.74f, 0.65f),
            MakeRenderFloat3(1.65f, 3.74f, -0.65f),
            5,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ceiling Light B",
            MakeRenderFloat3(1.65f, 3.74f, -0.65f),
            MakeRenderFloat3(0.15f, 3.74f, 0.65f),
            MakeRenderFloat3(1.65f, 3.74f, 0.65f),
            5,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildSunSkyStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::SunSkyStudy;
    scene.label = "Sun / Sky Study";
    scene.description = "Open outdoor-style scene for validating scene-owned environment lighting with an explicit sun light.";
    scene.defaultBackground = RenderBackgroundMode::Gradient;
    scene.defaultEnvironmentEnabled = true;
    scene.materials = {
        MakeSurfaceMaterial("Ground Concrete", MakeRenderFloat3(0.58f, 0.57f, 0.54f), 0.96f, 0.0f),
        MakeSurfaceMaterial("Warm Plaster", MakeRenderFloat3(0.84f, 0.78f, 0.72f), 0.92f, 0.0f),
        MakeSurfaceMaterial("Blue Car Paint", MakeRenderFloat3(0.22f, 0.48f, 0.88f), 0.18f, 0.82f),
        MakeSurfaceMaterial("Bronze Metal", MakeRenderFloat3(0.78f, 0.54f, 0.28f), 0.24f, 1.0f),
        MakeSurfaceMaterial("White Ceramic", MakeRenderFloat3(0.90f, 0.90f, 0.88f), 0.30f, 0.0f)
    };

    scene.spheres = {
        MakeSphere("Blue Hero Sphere", MakeRenderFloat3(-0.95f, 0.72f, 0.05f), 0.72f, 2, MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeSphere("Ceramic Sphere", MakeRenderFloat3(1.15f, 0.56f, -0.85f), 0.56f, 4, MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.triangles = {
        MakeTriangle("Ground A",
            MakeRenderFloat3(-6.0f, -0.2f, -6.0f),
            MakeRenderFloat3(6.0f, -0.2f, -6.0f),
            MakeRenderFloat3(-6.0f, -0.2f, 6.0f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ground B",
            MakeRenderFloat3(6.0f, -0.2f, -6.0f),
            MakeRenderFloat3(6.0f, -0.2f, 6.0f),
            MakeRenderFloat3(-6.0f, -0.2f, 6.0f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Rear Wall A",
            MakeRenderFloat3(4.2f, -0.2f, -3.2f),
            MakeRenderFloat3(4.2f, 3.2f, -3.2f),
            MakeRenderFloat3(4.2f, -0.2f, 2.4f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Rear Wall B",
            MakeRenderFloat3(4.2f, 3.2f, -3.2f),
            MakeRenderFloat3(4.2f, 3.2f, 2.4f),
            MakeRenderFloat3(4.2f, -0.2f, 2.4f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Accent Bronze Wedge",
            MakeRenderFloat3(-0.25f, -0.2f, 1.45f),
            MakeRenderFloat3(0.95f, 1.55f, 1.05f),
            MakeRenderFloat3(1.85f, -0.2f, 1.85f),
            3,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.lights = {
        MakeSunLight("Outdoor Sun", MakeRenderFloat3(0.0f, 146.0f, 53.0f), 1.2f, MakeRenderFloat3(4.2f, 3.8f, 3.4f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildDepthOfFieldStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::DepthOfFieldStudy;
    scene.label = "Depth Of Field Study";
    scene.description = "Open Cornell-style box with layered subjects meant for focus-distance and aperture-radius checks in the baseline thin-lens camera path.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = {
        MakeSurfaceMaterial("Focus White", MakeRenderFloat3(0.84f, 0.84f, 0.82f), 1.0f, 0.0f),
        MakeSurfaceMaterial("Focus Red", MakeRenderFloat3(0.84f, 0.24f, 0.20f), 0.95f, 0.0f),
        MakeSurfaceMaterial("Focus Green", MakeRenderFloat3(0.26f, 0.76f, 0.34f), 0.95f, 0.0f),
        MakeSurfaceMaterial("Soft Gold", MakeRenderFloat3(0.92f, 0.76f, 0.40f), 0.32f, 1.0f),
        MakeSurfaceMaterial("Soft Blue Metal", MakeRenderFloat3(0.34f, 0.58f, 0.94f), 0.20f, 1.0f),
        MakeEmissiveMaterial("Neutral Area Light", MakeRenderFloat3(1.0f, 1.0f, 1.0f), MakeRenderFloat3(1.0f, 0.98f, 0.94f), 7.5f)
    };

    scene.spheres = {
        MakeSphere("Foreground Gold Sphere", MakeRenderFloat3(-1.05f, 0.48f, 1.12f), 0.48f, 3, MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeSphere("Focus Blue Sphere", MakeRenderFloat3(0.20f, 0.70f, 0.10f), 0.70f, 4, MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeSphere("Background White Sphere", MakeRenderFloat3(1.55f, 0.98f, -0.92f), 0.98f, 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.triangles = {
        MakeTriangle("Floor A",
            MakeRenderFloat3(-1.8f, -0.2f, -2.6f),
            MakeRenderFloat3(3.0f, -0.2f, -2.6f),
            MakeRenderFloat3(-1.8f, -0.2f, 2.6f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Floor B",
            MakeRenderFloat3(3.0f, -0.2f, -2.6f),
            MakeRenderFloat3(3.0f, -0.2f, 2.6f),
            MakeRenderFloat3(-1.8f, -0.2f, 2.6f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ceiling A",
            MakeRenderFloat3(-1.8f, 3.9f, -2.6f),
            MakeRenderFloat3(-1.8f, 3.9f, 2.6f),
            MakeRenderFloat3(3.0f, 3.9f, -2.6f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ceiling B",
            MakeRenderFloat3(3.0f, 3.9f, -2.6f),
            MakeRenderFloat3(-1.8f, 3.9f, 2.6f),
            MakeRenderFloat3(3.0f, 3.9f, 2.6f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Back Wall A",
            MakeRenderFloat3(3.0f, -0.2f, -2.6f),
            MakeRenderFloat3(3.0f, 3.9f, -2.6f),
            MakeRenderFloat3(3.0f, -0.2f, 2.6f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Back Wall B",
            MakeRenderFloat3(3.0f, 3.9f, -2.6f),
            MakeRenderFloat3(3.0f, 3.9f, 2.6f),
            MakeRenderFloat3(3.0f, -0.2f, 2.6f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Left Wall A",
            MakeRenderFloat3(-1.8f, -0.2f, -2.6f),
            MakeRenderFloat3(-1.8f, 3.9f, -2.6f),
            MakeRenderFloat3(3.0f, -0.2f, -2.6f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Left Wall B",
            MakeRenderFloat3(-1.8f, 3.9f, -2.6f),
            MakeRenderFloat3(3.0f, 3.9f, -2.6f),
            MakeRenderFloat3(3.0f, -0.2f, -2.6f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Right Wall A",
            MakeRenderFloat3(-1.8f, -0.2f, 2.6f),
            MakeRenderFloat3(3.0f, -0.2f, 2.6f),
            MakeRenderFloat3(-1.8f, 3.9f, 2.6f),
            2,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Right Wall B",
            MakeRenderFloat3(-1.8f, 3.9f, 2.6f),
            MakeRenderFloat3(3.0f, -0.2f, 2.6f),
            MakeRenderFloat3(3.0f, 3.9f, 2.6f),
            2,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Focus Marker A",
            MakeRenderFloat3(0.76f, -0.18f, -0.42f),
            MakeRenderFloat3(0.76f, 1.05f, -0.42f),
            MakeRenderFloat3(0.76f, -0.18f, 0.42f),
            0,
            MakeRenderFloat3(0.96f, 0.96f, 0.96f)),
        MakeTriangle("Focus Marker B",
            MakeRenderFloat3(0.76f, 1.05f, -0.42f),
            MakeRenderFloat3(0.76f, 1.05f, 0.42f),
            MakeRenderFloat3(0.76f, -0.18f, 0.42f),
            0,
            MakeRenderFloat3(0.96f, 0.96f, 0.96f)),
        MakeTriangle("Ceiling Light A",
            MakeRenderFloat3(0.05f, 3.84f, -0.85f),
            MakeRenderFloat3(0.05f, 3.84f, 0.85f),
            MakeRenderFloat3(1.95f, 3.84f, -0.85f),
            5,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Ceiling Light B",
            MakeRenderFloat3(1.95f, 3.84f, -0.85f),
            MakeRenderFloat3(0.05f, 3.84f, 0.85f),
            MakeRenderFloat3(1.95f, 3.84f, 0.85f),
            5,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildGlassSlabStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::GlassSlabStudy;
    scene.label = "Glass Slab";
    scene.description = "Thick clear-glass slab in front of colored wall markers for first smooth-dielectric refraction and angle-dependent reflection checks.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = BuildGlassStudyMaterials(1.5f);

    AppendGlassStudyRoom(scene.triangles);
    AppendBox(
        scene.triangles,
        "Glass Slab",
        MakeRenderFloat3(0.55f, 0.24f, -0.72f),
        MakeRenderFloat3(0.95f, 2.22f, 0.72f),
        4,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    return scene;
}

RenderValidationSceneTemplate BuildWindowPaneStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::WindowPaneStudy;
    scene.label = "Window Pane";
    scene.description = "Thin solid glass pane with emissive-box lighting for flat-interface sanity checks and self-intersection regression testing.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = BuildGlassStudyMaterials(1.5f);

    AppendGlassStudyRoom(scene.triangles);
    AppendBox(
        scene.triangles,
        "Window Pane",
        MakeRenderFloat3(0.72f, 0.18f, -1.05f),
        MakeRenderFloat3(0.80f, 2.58f, 1.05f),
        4,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    return scene;
}

RenderValidationSceneTemplate BuildGlassSphereStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::GlassSphereStudy;
    scene.label = "Glass Sphere Study";
    scene.description = "Clear-glass sphere over a patterned backdrop for inversion, edge behavior, and total-internal-reflection checks.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = BuildGlassStudyMaterials(1.5f);

    AppendGlassStudyRoom(scene.triangles);
    scene.spheres = {
        MakeSphere("Glass Sphere", MakeRenderFloat3(0.84f, 0.58f, 0.0f), 0.78f, 4, MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    return scene;
}

RenderValidationSceneTemplate BuildTintedThicknessRampScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::TintedThicknessRamp;
    scene.label = "Tinted Thickness Ramp";
    scene.description = "Three tinted-glass blocks with increasing thickness over a patterned backdrop for Beer-Lambert absorption checks.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = {
        MakeSurfaceMaterial("Study White", MakeRenderFloat3(0.84f, 0.84f, 0.82f), 1.0f, 0.0f),
        MakeEmissiveMaterial("Study Area Light", MakeRenderFloat3(1.0f, 1.0f, 1.0f), MakeRenderFloat3(1.0f, 0.98f, 0.94f), 12.0f),
        MakeGlassMaterial("Tinted Glass", 1.5f, false, 0.0f, MakeRenderFloat3(0.58f, 0.88f, 0.72f), 0.45f)
    };

    AppendGlassStudyRoom(scene.triangles);
    AppendBox(
        scene.triangles,
        "Thin Tint Block",
        MakeRenderFloat3(0.55f, 0.18f, -1.20f),
        MakeRenderFloat3(0.95f, 1.90f, -0.70f),
        2,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendBox(
        scene.triangles,
        "Medium Tint Block",
        MakeRenderFloat3(0.55f, 0.18f, -0.30f),
        MakeRenderFloat3(0.95f, 1.90f, 0.40f),
        2,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendBox(
        scene.triangles,
        "Thick Tint Block",
        MakeRenderFloat3(0.55f, 0.18f, 0.85f),
        MakeRenderFloat3(0.95f, 1.90f, 1.95f),
        2,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    return scene;
}

RenderValidationSceneTemplate BuildFrostedPanelStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::FrostedPanelStudy;
    scene.label = "Frosted Panel";
    scene.description = "Rough dielectric panel with colored backdrop stripes for frosted-glass blur and energy checks.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = {
        MakeSurfaceMaterial("Study White", MakeRenderFloat3(0.84f, 0.84f, 0.82f), 1.0f, 0.0f),
        MakeSurfaceMaterial("Study Red", MakeRenderFloat3(0.84f, 0.24f, 0.20f), 0.95f, 0.0f),
        MakeSurfaceMaterial("Study Green", MakeRenderFloat3(0.26f, 0.76f, 0.34f), 0.95f, 0.0f),
        MakeEmissiveMaterial("Study Area Light", MakeRenderFloat3(1.0f, 1.0f, 1.0f), MakeRenderFloat3(1.0f, 0.98f, 0.94f), 12.0f),
        MakeGlassMaterial("Frosted Glass", 1.5f, false, 0.35f)
    };

    AppendGlassStudyRoom(scene.triangles);
    AppendBox(
        scene.triangles,
        "Frosted Panel",
        MakeRenderFloat3(0.72f, 0.18f, -1.05f),
        MakeRenderFloat3(0.82f, 2.58f, 1.05f),
        4,
        MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    return scene;
}

RenderValidationSceneTemplate BuildFogBeamStudyScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::FogBeamStudy;
    scene.label = "Fog Beam";
    scene.description = "Scene-wide fog with an explicit sun light and emissive strip to validate first homogeneous-medium atmosphere.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = true;
    scene.defaultFogEnabled = true;
    scene.defaultFogColor = MakeRenderFloat3(0.84f, 0.90f, 0.98f);
    scene.defaultFogDensity = 0.18f;
    scene.defaultFogAnisotropy = 0.45f;
    scene.materials = {
        MakeSurfaceMaterial("Fog White", MakeRenderFloat3(0.84f, 0.84f, 0.82f), 1.0f, 0.0f),
        MakeSurfaceMaterial("Fog Warm Wall", MakeRenderFloat3(0.78f, 0.66f, 0.56f), 0.92f, 0.0f),
        MakeSurfaceMaterial("Fog Bronze", MakeRenderFloat3(0.78f, 0.54f, 0.28f), 0.22f, 1.0f),
        MakeEmissiveMaterial("Accent Strip", MakeRenderFloat3(1.0f, 1.0f, 1.0f), MakeRenderFloat3(0.90f, 0.98f, 1.0f), 10.0f)
    };

    scene.triangles = {
        MakeTriangle("Floor A",
            MakeRenderFloat3(-4.0f, -0.2f, -4.0f),
            MakeRenderFloat3(4.0f, -0.2f, -4.0f),
            MakeRenderFloat3(-4.0f, -0.2f, 4.0f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Floor B",
            MakeRenderFloat3(4.0f, -0.2f, -4.0f),
            MakeRenderFloat3(4.0f, -0.2f, 4.0f),
            MakeRenderFloat3(-4.0f, -0.2f, 4.0f),
            0,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Rear Wall A",
            MakeRenderFloat3(3.8f, -0.2f, -2.2f),
            MakeRenderFloat3(3.8f, 3.2f, -2.2f),
            MakeRenderFloat3(3.8f, -0.2f, 2.2f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Rear Wall B",
            MakeRenderFloat3(3.8f, 3.2f, -2.2f),
            MakeRenderFloat3(3.8f, 3.2f, 2.2f),
            MakeRenderFloat3(3.8f, -0.2f, 2.2f),
            1,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Accent Strip A",
            MakeRenderFloat3(3.72f, 0.75f, -0.14f),
            MakeRenderFloat3(3.72f, 2.55f, -0.14f),
            MakeRenderFloat3(3.70f, 0.75f, 0.14f),
            3,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f)),
        MakeTriangle("Accent Strip B",
            MakeRenderFloat3(3.70f, 0.75f, 0.14f),
            MakeRenderFloat3(3.72f, 2.55f, -0.14f),
            MakeRenderFloat3(3.70f, 2.55f, 0.14f),
            3,
            MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    scene.spheres = {
        MakeSphere("Bronze Sphere", MakeRenderFloat3(0.20f, 0.62f, 0.10f), 0.62f, 2, MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };
    scene.lights = {
        MakeSunLight("Fog Sun", MakeRenderFloat3(0.0f, 136.0f, 18.0f), 2.2f, MakeRenderFloat3(4.2f, 3.8f, 3.4f))
    };
    return scene;
}

RenderValidationSceneTemplate BuildCornellBoxScene() {
    RenderValidationSceneTemplate scene;
    scene.id = RenderValidationSceneId::CornellBox;
    scene.label = "Cornell Box";
    scene.description = "Classic Cornell Box with emissive ceiling light, red/green walls, a tall box, and a sphere for path trace validation.";
    scene.defaultBackground = RenderBackgroundMode::Black;
    scene.defaultEnvironmentEnabled = false;
    scene.materials = {
        MakeDiffuseMaterial("White", MakeRenderFloat3(0.73f, 0.73f, 0.73f)),
        MakeDiffuseMaterial("Red", MakeRenderFloat3(0.65f, 0.05f, 0.05f)),
        MakeDiffuseMaterial("Green", MakeRenderFloat3(0.12f, 0.45f, 0.15f)),
        MakeEmissiveMaterial("Ceiling Light", MakeRenderFloat3(1.0f, 1.0f, 1.0f), MakeRenderFloat3(1.0f, 0.96f, 0.90f), 15.0f),
        MakeSurfaceMaterial("Glossy White", MakeRenderFloat3(0.73f, 0.73f, 0.73f), 0.35f, 0.0f)
    };

    // Room dimensions: X = [-2.75, 2.75], Y = [0, 5.5], Z = [-2.75, 2.75]
    const float L = -2.75f, R = 2.75f, B = 0.0f, T = 5.5f, F = -2.75f, K = 2.75f;

    scene.triangles = {};
    // Floor (material 0 white)
    AppendQuad(scene.triangles, "Floor",
        MakeRenderFloat3(L, B, F), MakeRenderFloat3(R, B, F),
        MakeRenderFloat3(L, B, K), MakeRenderFloat3(R, B, K), 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    // Ceiling (material 0 white)
    AppendQuad(scene.triangles, "Ceiling",
        MakeRenderFloat3(L, T, F), MakeRenderFloat3(L, T, K),
        MakeRenderFloat3(R, T, F), MakeRenderFloat3(R, T, K), 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    // Back wall (material 0 white)
    AppendQuad(scene.triangles, "Back Wall",
        MakeRenderFloat3(L, B, K), MakeRenderFloat3(R, B, K),
        MakeRenderFloat3(L, T, K), MakeRenderFloat3(R, T, K), 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    // Left wall (material 1 red)
    AppendQuad(scene.triangles, "Left Wall",
        MakeRenderFloat3(L, B, F), MakeRenderFloat3(L, B, K),
        MakeRenderFloat3(L, T, F), MakeRenderFloat3(L, T, K), 1, MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    // Right wall (material 2 green)
    AppendQuad(scene.triangles, "Right Wall",
        MakeRenderFloat3(R, B, K), MakeRenderFloat3(R, B, F),
        MakeRenderFloat3(R, T, K), MakeRenderFloat3(R, T, F), 2, MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    // Ceiling light panel (material 3 emissive) — centered, slightly below ceiling
    AppendQuad(scene.triangles, "Ceiling Light",
        MakeRenderFloat3(-0.65f, T - 0.01f, -0.55f), MakeRenderFloat3(0.65f, T - 0.01f, -0.55f),
        MakeRenderFloat3(-0.65f, T - 0.01f, 0.55f), MakeRenderFloat3(0.65f, T - 0.01f, 0.55f), 3, MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    // Tall box (material 0 white) — rotated ~15 degrees placed on the left side
    const float bx = -1.0f, bz = 0.8f;
    const float bw = 1.0f, bh = 3.3f, bd = 1.0f;
    const float ca = 0.9659f, sa = 0.2588f; // cos(15), sin(15)
    auto rotPt = [&](float lx, float ly, float lz) -> RenderFloat3 {
        return MakeRenderFloat3(bx + lx * ca - lz * sa, ly, bz + lx * sa + lz * ca);
    };
    const RenderFloat3 b000 = rotPt(-bw * 0.5f, B, -bd * 0.5f);
    const RenderFloat3 b100 = rotPt(bw * 0.5f, B, -bd * 0.5f);
    const RenderFloat3 b010 = rotPt(-bw * 0.5f, B + bh, -bd * 0.5f);
    const RenderFloat3 b110 = rotPt(bw * 0.5f, B + bh, -bd * 0.5f);
    const RenderFloat3 b001 = rotPt(-bw * 0.5f, B, bd * 0.5f);
    const RenderFloat3 b101 = rotPt(bw * 0.5f, B, bd * 0.5f);
    const RenderFloat3 b011 = rotPt(-bw * 0.5f, B + bh, bd * 0.5f);
    const RenderFloat3 b111 = rotPt(bw * 0.5f, B + bh, bd * 0.5f);
    AppendQuad(scene.triangles, "Tall Box Front", b000, b100, b010, b110, 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(scene.triangles, "Tall Box Back", b101, b001, b111, b011, 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(scene.triangles, "Tall Box Left", b001, b000, b011, b010, 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(scene.triangles, "Tall Box Right", b100, b101, b110, b111, 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));
    AppendQuad(scene.triangles, "Tall Box Top", b010, b110, b011, b111, 0, MakeRenderFloat3(1.0f, 1.0f, 1.0f));

    // Sphere on the right side (material 4 glossy)
    scene.spheres = {
        MakeSphere("Right Sphere", MakeRenderFloat3(1.15f, 0.80f, -0.65f), 0.80f, 4, MakeRenderFloat3(1.0f, 1.0f, 1.0f))
    };

    return scene;
}

} // namespace

const std::vector<RenderValidationSceneOption>& GetRenderValidationSceneOptions() {
    return GetSceneOptionsInternal();
}

const char* GetRenderValidationSceneLabel(RenderValidationSceneId id) {
    if (id == RenderValidationSceneId::Custom) {
        return "Custom";
    }

    for (const RenderValidationSceneOption& option : GetSceneOptionsInternal()) {
        if (option.id == id) {
            return option.label;
        }
    }

    return "Unknown Validation Scene";
}

RenderValidationSceneTemplate BuildRenderValidationScene(RenderValidationSceneId id) {
    switch (id) {
    case RenderValidationSceneId::SphereStudy:
        return BuildSphereStudyScene();
    case RenderValidationSceneId::TriangleCluster:
        return BuildTriangleClusterScene();
    case RenderValidationSceneId::MixedDebug:
        return BuildMixedDebugScene();
    case RenderValidationSceneId::MeshInstancing:
        return BuildMeshInstancingScene();
    case RenderValidationSceneId::SunSkyStudy:
        return BuildSunSkyStudyScene();
    case RenderValidationSceneId::EmissiveShowcase:
        return BuildEmissiveShowcaseScene();
    case RenderValidationSceneId::DepthOfFieldStudy:
        return BuildDepthOfFieldStudyScene();
    case RenderValidationSceneId::GlassSlabStudy:
        return BuildGlassSlabStudyScene();
    case RenderValidationSceneId::WindowPaneStudy:
        return BuildWindowPaneStudyScene();
    case RenderValidationSceneId::GlassSphereStudy:
        return BuildGlassSphereStudyScene();
    case RenderValidationSceneId::TintedThicknessRamp:
        return BuildTintedThicknessRampScene();
    case RenderValidationSceneId::FrostedPanelStudy:
        return BuildFrostedPanelStudyScene();
    case RenderValidationSceneId::FogBeamStudy:
        return BuildFogBeamStudyScene();
    case RenderValidationSceneId::CornellBox:
        return BuildCornellBoxScene();
    case RenderValidationSceneId::Custom:
        break;
    }

    return BuildMixedDebugScene();
}
