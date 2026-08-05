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
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/graph_scale_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_layout_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/GraphCapture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Internal/EditorGraphSnapshotLookup.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Internal/EditorRenderWorkerTileGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Async/TaskSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Color/LutCreator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Color/LutImporter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawProcessingMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/Contracts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBaseLocalSuggestions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBaseNoiseDetail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoStartPoint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawRestormerAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerPackage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerTiling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageAnalysis.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawRuntime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawDecoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspaceManagedGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspaceProjects.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/RawProjectModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/ProjectStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/StackBinaryFormat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RenderTiling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Internal/RenderPipelineGraphSchedule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RawPreviewProxy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ThirdParty/stb_image_impl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NeuralDenoise/NeuralDenoiseTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorCompoundDefinitions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphCompound.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphDevelopSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphLutSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphSemanticMutation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphCompletedChains.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphReferenceTraversal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphSocketTraversal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Timeline/TimelineAnimation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Timeline/TimelineFrameProducer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Timeline/TimelinePersistence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Timeline/TimelinePlayback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ChannelImageSemantics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/OutputInspection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/RegionPlanning.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ReductionMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/GeometryMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SpecializedPlanning.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SourceColorMetadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/TechnicalImageMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/FirstClassValue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/CompoundDefinition.cpp"
)

set(STACK_NODE_MATH_REFERENCE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_reference_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_reference_harness.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GLLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GLHelpers.cpp"
)

set(STACK_NODE_MATH_CONTRACT_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_contract_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/NodeDefinition.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorPropagation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ProjectSchema.cpp"
)

set(STACK_NODE_MATH_PHASE2_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_phase2_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SourceColorMetadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/TechnicalImageMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/OutputInspection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SemanticSpine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/PngMetadataWriter.cpp"
)

set(STACK_NODE_MATH_PHASE3_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_phase3_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/FirstClassValue.cpp"
)

set(STACK_NODE_MATH_PHASE4_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_phase4_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/PointwiseIR.cpp"
)

set(STACK_NODE_MATH_PHASE5_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_phase5_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/CompoundDefinition.cpp"
)

set(STACK_NODE_MATH_PHASE6_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_phase6_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/RegionPlanning.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ReductionMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/GeometryMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SpecializedPlanning.cpp"
)

set(STACK_NODE_MATH_CHANNEL_IMAGE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/node_math_channel_image_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorPropagation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ChannelImageSemantics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/OutputInspection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/TechnicalImageMath.cpp"
)

set(STACK_RAW_EVIDENCE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/raw_evidence_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawProcessingMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
)

set(STACK_RENDERED_FEATURE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/rendered_feature_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
)

set(STACK_PRECISE_CANDIDATE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/precise_candidate_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseCandidateEngine.cpp"
)

set(STACK_OPTIMIZER_SELECTION_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/optimizer_selection_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawOptimizerSelection.cpp"
)

set(STACK_PRECISE_DRY_RUN_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/precise_dry_run_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseCandidateEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawOptimizerSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseDryRun.cpp"
)

set(STACK_PRECISE_INTEGRATION_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/precise_integration_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseCandidateEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawOptimizerSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseDryRun.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseIntegration.cpp"
)
