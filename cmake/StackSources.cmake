# Stack source organization lives here so the root CMakeLists stays focused on
# targets, dependencies, and platform wiring.

file(GLOB_RECURSE STACK_APP_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# The shipping desktop is a client of StackBurst.exe. Keep only serializable
# recipes, identities and result ownership here; computation belongs to the worker.
set(STACK_BURST_CONTRACT_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/Recipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/GroupSuggestion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/SourceSetIdentity.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/Identity.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/ResourcePolicy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/ResultTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/ResultSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/Panorama/Contracts.cpp"
)
set(STACK_BURST_INTERNAL_VALIDATION_NAMES
    BracketingDepthValidation BracketingEvidenceExport BracketingUpgradeComparison
    BracketingProjectDiagnostic BracketingPresentationBenchmark BracketingUiValidation
    BracketingValidation PanoramaValidation PanoramaSessionsValidation
    ProcessingPresentationFocusedValidation SuperResolutionValidation
)
if (NOT STACK_BUILD_BURST_DIAGNOSTICS)
    list(FILTER STACK_APP_SOURCE_FILES EXCLUDE REGEX "/src/Raw/Bracketing/.*\\.cpp$")
    list(APPEND STACK_APP_SOURCE_FILES ${STACK_BURST_CONTRACT_SOURCE_FILES})
    foreach(suite IN LISTS STACK_BURST_INTERNAL_VALIDATION_NAMES)
        list(FILTER STACK_APP_SOURCE_FILES EXCLUDE REGEX "/App/Validation/Suites/${suite}\\.cpp$")
    endforeach()
endif()
# Worker entry points are never linked into the desktop process.
list(FILTER STACK_APP_SOURCE_FILES EXCLUDE REGEX "/src/BurstEngine/.*\\.cpp$")
list(FILTER STACK_APP_SOURCE_FILES EXCLUDE REGEX "/src/GraphRoutingBackend/.*\\.cpp$")
list(APPEND STACK_APP_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Client.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Protocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/WireData.cpp")

if (WIN32)
    list(APPEND STACK_APP_SOURCE_FILES "${CMAKE_BINARY_DIR}/generated/src/resources.rc")
endif()

set(STACK_GRAPH_BEHAVIOR_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_behavior_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/compound_lowering_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/raw_layer_stack_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Graph/GraphDocumentRules.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGraphOperation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGraphParameters.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerRouting.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawProjectGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerCoverageBindings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerStack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Assets/CreatedAsset.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerSourceTransactions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerStackValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerStackPersistence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/RawLayerStackSnapshot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/GraphSnapshotBuilder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/GraphSnapshotConversions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/GraphImageContracts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/GraphImagePayload.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/NodeDependencyGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/project_gallery_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/raw_workspace_storage_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/persistence_binary_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/raw_viewport_timing_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_render_policy_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_output_semantics_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_wire_routing_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_node_notes_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_channel_outputs_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/ChannelOutputs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/AppPaths.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/graph_scale_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_layout_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/GraphCapture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Utils/AtomicExportFile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/RawAttributeTargetProject.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/MultiFrameResultCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Project/GraphSnapshotLookup.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Internal/EditorRenderWorkerTileGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Async/TaskSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Color/LutCreator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Color/LutImporter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawProcessingMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/Contracts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameHdr/Contracts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBaseLocalSuggestions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoBaseNoiseDetail.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAutoStartPoint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseControlMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseBandSchedule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawAssetCapture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawStrokeCapture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Tone/SceneTone.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Detail/DetailContrast.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawColorCalibration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaMask.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRasterizer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/ImageGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawViewportTimingHistory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawViewportPreferences.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawViewportController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawColorWarpAnalysis.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawColorWarpMask.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawEditAttributes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawRestormerAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerPackage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Restormer/RestormerTiling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageAnalysis.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawSourceMetadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawRuntime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawDecoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/FloatMosaicImport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/DngMetadataSupplement.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Internal/DngTiffReader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGalleryFileActions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGalleryFilmstripOrganization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGallerySimilarity.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGpuMemoryBudget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspace.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawSourceFolders.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGalleryThumbnailPixels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspaceStorage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspaceManagedGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawWorkspaceProjects.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/RawProjectModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawCaptureCompatibility.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/Recipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/RawProjectEditPipeline.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/RawProjectAttributeTransfer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/MultiFrameGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/ProjectStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/ProjectIndex.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/BracketingResultEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/ProjectSaveCoordinator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/ProjectSessionController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Queue/RenderQueueModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/StackBinaryFormat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Persistence/CreatedAssetStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RenderTiling.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GraphViewportPlanning.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Internal/RenderPipelineGraphSchedule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/RawPreviewProxy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ThirdParty/stb_image_impl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Graph/LayerOperationParameters.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ImageOperationInput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ToneCurveParameters.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/Timeline/TimelineParameterEdits.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorCompoundDefinitions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/CompoundRecipes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/CompoundRepacking.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphCompound.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/GraphExpansionLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/GraphRoomLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Layout/GraphFlowLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/GraphWireRouting.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/GraphWireRoutingRuntime.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/GraphConnectionLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/CompoundExecutionLowering.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/NativeNodeDecomposition.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/OperationInventory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/OperationMetadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/OperationParameters.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/OperationIdentity.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/OperationContracts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/SourceOperations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/ImageEffectOperations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Definitions/FieldOperations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphReadTransaction.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphDevelopSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphLutSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawMetadataSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphSemanticMutation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphCompletedChains.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphReferenceTraversal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/EditorNodeGraphSocketTraversal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/GraphOutputSemantics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor/NodeGraph/Model/GraphOutputRules.cpp"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/Uuid.cpp"
)

set(STACK_NODE_MATH_REFERENCE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_reference_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_reference_harness.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GLLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GLHelpers.cpp"
)

set(STACK_NODE_MATH_CONTRACT_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_contract_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/NodeDefinition.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorPropagation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ProjectSchema.cpp"
)

set(STACK_NODE_MATH_PHASE2_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_phase2_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SourceColorMetadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/TechnicalImageMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/OutputInspection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SemanticSpine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/PngMetadataWriter.cpp"
)

set(STACK_NODE_MATH_PHASE3_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_phase3_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/FirstClassValue.cpp"
)

set(STACK_NODE_MATH_PHASE4_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_phase4_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/PointwiseIR.cpp"
)

set(STACK_NODE_MATH_PHASE5_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_phase5_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/CompoundDefinition.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/Uuid.cpp"
)

set(STACK_NODE_MATH_PHASE6_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_phase6_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/RegionPlanning.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ReductionMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/GeometryMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/SpecializedPlanning.cpp"
)

set(STACK_NODE_MATH_CHANNEL_IMAGE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/node_math_channel_image_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/DescriptorPropagation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ChannelImageSemantics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/OutputInspection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/TechnicalImageMath.cpp"
)

set(STACK_RAW_EVIDENCE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/raw_evidence_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawProcessingMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
)

set(STACK_RENDERED_FEATURE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/rendered_feature_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
)

set(STACK_PRECISE_CANDIDATE_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/precise_candidate_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseControlMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseBandSchedule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Tone/SceneTone.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Detail/DetailContrast.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawColorCalibration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaMask.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRasterizer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/ImageGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseCandidateEngine.cpp"
)

set(STACK_OPTIMIZER_SELECTION_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/optimizer_selection_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawOptimizerSelection.cpp"
)

set(STACK_PRECISE_DRY_RUN_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/precise_dry_run_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseControlMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseBandSchedule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Tone/SceneTone.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Detail/DetailContrast.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawColorCalibration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaMask.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRasterizer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/ImageGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseCandidateEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawOptimizerSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseDryRun.cpp"
)

set(STACK_PRECISE_INTEGRATION_TEST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/precise_integration_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseControlMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Denoise/RawDenoiseBandSchedule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawDevelopmentRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Tone/SceneTone.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Detail/DetailContrast.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawColorCalibration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRecipe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaMask.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaRasterizer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/ImageGuidance.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawZoneAreaGain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RenderedFeatureEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseCandidateEngine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawOptimizerSelection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseDryRun.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawPreciseIntegration.cpp"
)
