set(render_worker_failure_test "${CMAKE_CURRENT_BINARY_DIR}/render_worker_failure_tests.cpp")
add_custom_command(OUTPUT "${render_worker_failure_test}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/render_worker_failure_tests.py"
        --output "${render_worker_failure_test}"
    DEPENDS tools/tests/render_worker_failure_tests.py
        src/Editor/EditorRenderWorker.cpp
        src/Editor/EditorRenderWorker.h
        src/Editor/Internal/EditorRenderResultRetirement.h
        src/Editor/Internal/EditorRenderWorkerSessions.cpp
        src/Editor/Internal/EditorRenderWorkerViewportTiming.cpp
        src/Editor/RawRenderService.h
        src/Editor/RawRenderService.cpp
        src/Editor/RawRenderPlanning.h
        src/Editor/Internal/EditorModuleRenderAdoption.cpp
        src/Editor/Internal/EditorModulePersistence.cpp
        src/Editor/Internal/EditorModuleRenderScheduling.cpp
        src/Editor/Internal/EditorModulePreviewPublication.cpp
        src/Editor/Internal/RawLab/EditorModuleRawAssets.cpp
        src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp
    VERBATIM)
add_executable(StackRenderWorkerFailureTests "${render_worker_failure_test}")
target_include_directories(StackRenderWorkerFailureTests PRIVATE src)
target_link_libraries(StackRenderWorkerFailureTests PRIVATE Threads::Threads)
add_test(NAME StackEditor.RenderWorkerFailure COMMAND StackRenderWorkerFailureTests)
set_tests_properties(StackEditor.RenderWorkerFailure PROPERTIES LABELS "editor;render;async;cpu" TIMEOUT 30)
