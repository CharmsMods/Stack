# Stack source organization lives here so the root CMakeLists stays focused on
# targets, dependencies, and platform wiring.

file(GLOB_RECURSE STACK_APP_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

if (WIN32)
    list(APPEND STACK_APP_SOURCE_FILES "${CMAKE_BINARY_DIR}/generated/src/resources.rc")
endif()

# Legacy aggregate layer implementations were replaced by split, registry-backed
# nodes. Keep the files in the tree for migration reference, but do not compile them.
list(FILTER STACK_APP_SOURCE_FILES EXCLUDE REGEX "/src/Editor/Layers/(AdjustmentsLayer|BlurLayer|CompressionLayer|CorruptionLayer|CropTransformLayer|DenoisingLayer|DitherLayer|EdgeEffectsLayer|HeatwaveLayer)\\.(cpp|h)$")

set(STACK_GRAPH_BEHAVIOR_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/graph_behavior_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Color/LutCreator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Color/LutImporter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBaseLocalSuggestions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBaseNoiseDetail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageAnalysis.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawRuntime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawDecoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspaceManagedGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspaceProjects.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/StackBinaryFormat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RenderTiling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RawPreviewProxy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ThirdParty/stb_image_impl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NeuralDenoise/NeuralDenoiseTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphDevelopSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphLutSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp"
)
