# CPU-only panorama geometry, pinned for portable builds.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(BUILD_LIST core,imgproc,features2d,flann,calib3d,stitching CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(option BUILD_TESTS BUILD_PERF_TESTS BUILD_EXAMPLES BUILD_opencv_apps
    BUILD_JAVA BUILD_opencv_python2 BUILD_opencv_python3 BUILD_opencv_world
    WITH_IPP WITH_OPENCL WITH_CUDA WITH_TBB WITH_OPENMP WITH_FFMPEG WITH_MSMF
    WITH_GSTREAMER WITH_ITT WITH_PROTOBUF WITH_LAPACK WITH_EIGEN WITH_VTK
    WITH_OPENEXR WITH_JASPER WITH_WEBP WITH_AVIF WITH_TIFF WITH_JPEG WITH_PNG
    OPENCV_ENABLE_NONFREE OPENCV_GENERATE_SETUPVARS)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(BUILD_WITH_STATIC_CRT ON CACHE BOOL "" FORCE)
set(OPENCV_SKIP_PYTHON_LOADER ON CACHE BOOL "" FORCE)
FetchContent_Declare(stack_opencv
    URL https://github.com/opencv/opencv/archive/2e1f8da65e4f9fa1a98423e6ac223187438a4db8.tar.gz)
FetchContent_MakeAvailable(stack_opencv)
target_link_libraries(${PROJECT_NAME} PRIVATE opencv_stitching opencv_calib3d opencv_features2d opencv_imgproc opencv_core)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${CMAKE_BINARY_DIR}
    ${stack_opencv_BINARY_DIR}
    ${stack_opencv_SOURCE_DIR}/modules/core/include
    ${stack_opencv_SOURCE_DIR}/modules/imgproc/include
    ${stack_opencv_SOURCE_DIR}/modules/features2d/include
    ${stack_opencv_SOURCE_DIR}/modules/flann/include
    ${stack_opencv_SOURCE_DIR}/modules/calib3d/include
    ${stack_opencv_SOURCE_DIR}/modules/stitching/include)
