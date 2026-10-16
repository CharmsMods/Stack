add_executable(StackGpuUploadTests
    tools/tests/gpu_upload_tests.cpp
    src/Renderer/GLLoader.cpp
    src/Renderer/GLHelpers.cpp
    src/Raw/RawGpuPipeline.cpp
    src/Raw/RawColorTransform.cpp
    src/Raw/Demosaic/DemosaicTexture.cpp
    src/Raw/Demosaic/GpuDemosaic.cpp
    src/Raw/Demosaic/GpuDcb.cpp
    src/Raw/Demosaic/GpuClassic.cpp
    src/Raw/Demosaic/ClassicDemosaic.cpp
    src/Raw/Demosaic/FloatDemosaic.cpp
    src/Raw/Demosaic/DirectionalDemosaic.cpp
    src/Raw/Demosaic/LmmseDemosaic.cpp
    src/Raw/Demosaic/DcbDemosaic.cpp
    src/Raw/RawGpuPreprocessor.cpp
    src/Raw/RawProcessingMath.cpp
    src/Raw/RawImageData.cpp)
target_include_directories(StackGpuUploadTests PRIVATE src)
target_link_libraries(StackGpuUploadTests PRIVATE glfw)
if(WIN32)
    target_link_libraries(StackGpuUploadTests PRIVATE opengl32)
elseif(APPLE)
    target_link_libraries(StackGpuUploadTests PRIVATE "-framework OpenGL")
else()
    target_link_libraries(StackGpuUploadTests PRIVATE GL)
endif()
add_test(NAME StackRenderer.TextureUploads COMMAND StackGpuUploadTests)
set_tests_properties(StackRenderer.TextureUploads PROPERTIES LABELS "renderer;raw;gpu")
