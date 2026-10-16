# Thin native COM adapter only. No image, renderer, UI or engine dependencies may
# enter Explorer through this target; processing is launched out of process.
if (NOT WIN32)
    return()
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/tools/release/products.json" STACK_PRODUCT_MANIFEST)
string(JSON STACK_SHELL_CLSID GET "${STACK_PRODUCT_MANIFEST}" products Stack shellClsid)
string(JSON STACK_TEST_SHELL_CLSID GET "${STACK_PRODUCT_MANIFEST}" products Stack localTestShellClsid)
string(JSON BURST_SHELL_CLSID GET "${STACK_PRODUCT_MANIFEST}" products Burst shellClsid)
string(JSON BURST_TEST_SHELL_CLSID GET "${STACK_PRODUCT_MANIFEST}" products Burst localTestShellClsid)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tools/burst_shell/ShellIdentities.h.in"
    "${CMAKE_BINARY_DIR}/generated/burst_shell/ShellIdentities.h" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tools/burst_shell/StackBurstShell.rc.in"
    "${CMAKE_BINARY_DIR}/generated/burst_shell/StackBurstShell.rc" @ONLY)

add_library(StackBurstShell SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/burst_shell/ExplorerCommand.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/burst_shell/SelectionLaunch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/burst_shell/StackBurstShell.def"
    "${CMAKE_BINARY_DIR}/generated/burst_shell/StackBurstShell.rc")
target_compile_features(StackBurstShell PRIVATE cxx_std_17)
target_compile_definitions(StackBurstShell PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(StackBurstShell PRIVATE "${CMAKE_BINARY_DIR}/generated/burst_shell")
target_link_libraries(StackBurstShell PRIVATE shell32 ole32 shlwapi user32)
set_target_properties(StackBurstShell PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${STACK_DEPLOY_DIR}/Stack/App/Runtime"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STACK_DEPLOY_DIR}/Stack/App/Runtime"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${STACK_DEPLOY_DIR}/Stack/App/Runtime")
if (TARGET StackBurst)
    add_dependencies(StackBurst StackBurstShell)
endif()
