# Geometry-only routing backend; pin the source and ship it as a replaceable DLL.
FetchContent_Declare(stack_adaptagrams
    GIT_REPOSITORY https://github.com/mjwybrow/adaptagrams.git
    GIT_TAG 840ebcff20dbba36ad03a2160edf7cbaf9859984
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(stack_adaptagrams)
set(STACK_AVOID_DIR "${stack_adaptagrams_SOURCE_DIR}/cola/libavoid")
file(GLOB STACK_AVOID_SOURCES CONFIGURE_DEPENDS "${STACK_AVOID_DIR}/*.cpp")
add_library(stack_graph_routing_backend SHARED ${STACK_AVOID_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/GraphRoutingBackend/LibavoidBridge.cpp")
set_target_properties(stack_graph_routing_backend PROPERTIES OUTPUT_NAME libavoid
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/graph-routing-runtime")
target_include_directories(stack_graph_routing_backend PUBLIC "${stack_adaptagrams_SOURCE_DIR}/cola")
target_compile_definitions(stack_graph_routing_backend PRIVATE STACK_AVOID_BUILD LIBAVOID_EXPORTS NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(${PROJECT_NAME} PRIVATE stack_graph_routing_backend)
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:libavoid.dll")
    target_link_libraries(${PROJECT_NAME} PRIVATE delayimp)
endif()
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:stack_graph_routing_backend>"
        "${STACK_DEPLOY_DIR}/Stack/App/Runtime/libavoid.dll"
    VERBATIM)
