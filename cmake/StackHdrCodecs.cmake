# Use the upstream portable reference encoder with Stack's pinned static JPEG.
FetchContent_Declare(stack_ultrahdr
    URL https://github.com/google/libultrahdr/archive/d52a0d13814ca399fc8a07e23de1d2c63f0e8404.tar.gz
    SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(stack_ultrahdr)
file(GLOB hdr_codec_sources "${stack_ultrahdr_SOURCE_DIR}/lib/src/*.cpp")
file(GLOB hdr_metadata_sources "${stack_ultrahdr_SOURCE_DIR}/third_party/image_io/src/*/*.cc")
add_library(StackHdrCodec STATIC ${hdr_codec_sources} ${hdr_metadata_sources})
target_include_directories(StackHdrCodec PUBLIC "${stack_ultrahdr_SOURCE_DIR}" PRIVATE
    "${stack_ultrahdr_SOURCE_DIR}/lib/include"
    "${stack_ultrahdr_SOURCE_DIR}/third_party/image_io/includes"
    "${stack_ultrahdr_SOURCE_DIR}/third_party/image_io/src/modp_b64"
    "${stack_ultrahdr_SOURCE_DIR}/third_party/image_io/src/modp_b64/modp_b64"
    "${stack_opencv_SOURCE_DIR}/3rdparty/libjpeg-turbo/src"
    "${stack_opencv_BINARY_DIR}/3rdparty/libjpeg-turbo")
# The codec defaults to an 8192-pixel edge, below the native width of many cameras.
set_property(SOURCE "${stack_ultrahdr_SOURCE_DIR}/lib/src/jpegdecoderhelper.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS UHDR_MAX_DIMENSION=16384)
target_compile_definitions(StackHdrCodec PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX UHDR_WRITE_ISO UHDR_WRITE_XMP)
target_link_libraries(StackHdrCodec PRIVATE libjpeg-turbo)
target_link_libraries(${PROJECT_NAME} PRIVATE StackHdrCodec)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE dxgi d3d11 dcomp runtimeobject windowsapp)
endif()
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/Hdr/HdrPng.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${stack_opencv_SOURCE_DIR}/3rdparty/zlib" "${stack_opencv_BINARY_DIR}/3rdparty/zlib")
# Embedded color profiles need an extended-range floating-point destination.
FetchContent_Declare(stack_lcms
    URL https://github.com/mm2/Little-CMS/archive/7717fce0d85447ef3b2df0cca2e99207c15eeecd.tar.gz
    SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(stack_lcms)
file(GLOB stack_lcms_sources "${stack_lcms_SOURCE_DIR}/src/*.c")
add_library(StackSourceProfiles STATIC ${stack_lcms_sources})
target_include_directories(StackSourceProfiles PUBLIC "${stack_lcms_SOURCE_DIR}/include")
target_link_libraries(${PROJECT_NAME} PRIVATE StackSourceProfiles)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/Hdr/RasterSource.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${stack_opencv_SOURCE_DIR}/3rdparty/zlib" "${stack_opencv_BINARY_DIR}/3rdparty/zlib")
target_compile_definitions(${PROJECT_NAME} PRIVATE STACK_HDR_FILE_INPUT=1)
