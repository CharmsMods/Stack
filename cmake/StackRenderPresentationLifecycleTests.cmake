set(render_presentation_test "${CMAKE_CURRENT_BINARY_DIR}/render_presentation_lifecycle_tests.cpp")
add_custom_command(OUTPUT "${render_presentation_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/render_presentation_lifecycle_tests.py"
        --output "${render_presentation_test}"
    DEPENDS tools/tests/render_presentation_lifecycle_tests.py
        src/Editor/RenderPresentationState.h
        src/Editor/EditorModule.h
        src/Editor/EditorModule.cpp
        src/Editor/Internal/EditorModuleRenderPresentationState.cpp
        src/Editor/Internal/EditorModuleGraphRendering.cpp
        src/Editor/Internal/EditorModuleViewportPreview.cpp
        src/Editor/Internal/EditorModuleRenderAdoption.cpp
        src/Editor/Internal/EditorModuleRendering.cpp
        src/Editor/Internal/EditorModuleRenderScheduling.cpp
        src/Queue/QueueRenderCoordinator.cpp
    VERBATIM)
add_executable(StackRenderPresentationLifecycleTests "${render_presentation_test}")
target_include_directories(StackRenderPresentationLifecycleTests PRIVATE src)
add_test(NAME StackEditor.RenderPresentationLifecycle COMMAND StackRenderPresentationLifecycleTests)
set_tests_properties(StackEditor.RenderPresentationLifecycle PROPERTIES LABELS "editor;render;async;cpu" TIMEOUT 30)
