#pragma once

#include "RenderContracts.h"

#include "RenderTab/Foundation/RenderFoundationTypes.h"
#include "RenderTab/PathTrace/PathTraceTypes.h"
#include "RenderTab/Runtime/RenderCamera.h"
#include "RenderTab/Runtime/RenderScene.h"
#include "RenderTab/Runtime/RenderSettings.h"

#include <string>

struct RenderValidationSceneTemplate;

namespace RenderContracts {

struct CompiledScene {
    RenderScene scene {};
    RenderCamera camera {};
    RenderSettings settings {};
    RenderPathTrace::CompiledPathTraceScene pathTraceScene {};
    bool valid = false;
};

class SceneCompiler {
public:
    bool Compile(
        const SceneSnapshot& snapshot,
        const RenderFoundation::Settings& settings,
        CompiledScene& outCompiledScene,
        std::string& errorMessage) const;

    bool Compile(
        const SceneSnapshot& snapshot,
        const RenderFoundation::Camera& cameraOverride,
        const RenderFoundation::Settings& settings,
        CompiledScene& outCompiledScene,
        std::string& errorMessage) const;

    bool CompileValidationScene(
        const RenderValidationSceneTemplate& sceneTemplate,
        const RenderFoundation::Camera& camera,
        const RenderFoundation::Settings& settings,
        CompiledScene& outCompiledScene,
        std::string& errorMessage) const;
};

} // namespace RenderContracts
