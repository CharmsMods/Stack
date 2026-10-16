# StackBurst is the sole shipping owner of burst reconstruction. The desktop
# links only protocol/data code and starts this executable for processing.
file(GLOB_RECURSE STACK_BURST_PROCESSING_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Bracketing/*.cpp")

set(STACK_BURST_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/burst/StackBurstMain.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Protocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/WireData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Worker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/GpuContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Selection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Batch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/LaunchInput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/WindowStyle.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Standalone.cpp"
    ${STACK_BURST_PROCESSING_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/Contracts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/Preparation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/NoiseModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/SameCfaSampler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/GlobalRegistration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/LocalMotion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/GpuLocalMotion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/Reliability.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/MultiFrameDenoise/MemoryPolicy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawImageData.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawMetadataSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawCaptureCompatibility.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawProcessingMath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawTechnicalEvidence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawRuntime.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/LibRawDecoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/FloatMosaicImport.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/DngMetadataSupplement.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/Internal/DngTiffReader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/ReconstructionDepth.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGallerySimilarity.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGalleryThumbnailPixels.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawColorTransform.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Raw/RawGpuMemoryBudget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/ContractTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMath/Uuid.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GLHelpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GLLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GpuMemoryBudget.cpp"
)

if (WIN32)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tools/burst/StackBurst.rc.in"
        "${CMAKE_BINARY_DIR}/generated/StackBurst.rc" @ONLY)
    list(APPEND STACK_BURST_SOURCE_FILES "${CMAKE_BINARY_DIR}/generated/StackBurst.rc")
endif()

file(GLOB STACK_BURST_OUTPUT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstEngine/Output*.cpp")
add_executable(StackBurst WIN32 ${STACK_BURST_SOURCE_FILES} ${STACK_BURST_OUTPUT_SOURCES})
set_target_properties(StackBurst PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${STACK_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STACK_DEPLOY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STACK_DEPLOY_DIR}")
target_include_directories(StackBurst PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ThirdParty"
    "${CMAKE_BINARY_DIR}/generated/src"
    "${CMAKE_BINARY_DIR}/generated/src/App")
target_include_directories(StackBurst SYSTEM PRIVATE
    ${CMAKE_BINARY_DIR} ${stack_opencv_BINARY_DIR}
    ${stack_opencv_SOURCE_DIR}/modules/core/include
    ${stack_opencv_SOURCE_DIR}/modules/imgproc/include
    ${stack_opencv_SOURCE_DIR}/modules/features2d/include
    ${stack_opencv_SOURCE_DIR}/modules/flann/include
    ${stack_opencv_SOURCE_DIR}/modules/calib3d/include
    ${stack_opencv_SOURCE_DIR}/modules/stitching/include)
target_link_libraries(StackBurst PRIVATE glfw opencv_stitching opencv_calib3d
    opencv_features2d opencv_imgproc opencv_core)
if (STACK_ENABLE_LIBRAW)
    target_compile_definitions(StackBurst PRIVATE STACK_ENABLE_LIBRAW=1)
    target_link_libraries(StackBurst PRIVATE stack_libraw)
    if (WIN32 AND MSVC)
        target_link_options(StackBurst PRIVATE "/DELAYLOAD:libraw.dll")
        target_link_libraries(StackBurst PRIVATE delayimp)
    endif()
endif()
if (MSVC)
    target_compile_options(StackBurst PRIVATE "/MP${STACK_MSVC_COMPILE_JOBS}" "/utf-8")
    target_link_options(StackBurst PRIVATE "/STACK:2097152" "/ENTRY:wmainCRTStartup")
endif()
if (WIN32)
    target_link_libraries(StackBurst PRIVATE opengl32 shell32 ole32 comdlg32
        comctl32 shlwapi dxgi bcrypt windowscodecs mscms)
elseif (APPLE)
    target_link_libraries(StackBurst PRIVATE "-framework OpenGL")
else()
    target_link_libraries(StackBurst PRIVATE GL)
endif()
add_dependencies(StackBurst StackRuntimePayload)
