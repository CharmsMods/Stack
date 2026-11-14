# The authored shader is the source of truth. Configure-time embedding is tracked
# by CMake so edits regenerate the header without runtime asset/path dependencies.
set(STACK_GLASS_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Glass/Shaders/material.frag")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${STACK_GLASS_SHADER}")
file(READ "${STACK_GLASS_SHADER}" STACK_GLASS_MATERIAL_SOURCE)
# MSVC limits each individual string literal, even when the resulting shader
# fits in a concatenated constant. Split the embedding without changing bytes.
set(stack_glass_remaining "${STACK_GLASS_MATERIAL_SOURCE}")
set(STACK_GLASS_MATERIAL_SOURCE "")
string(LENGTH "${stack_glass_remaining}" stack_glass_length)
while(stack_glass_length GREATER 12000)
    string(SUBSTRING "${stack_glass_remaining}" 0 12000 stack_glass_chunk)
    string(APPEND STACK_GLASS_MATERIAL_SOURCE "${stack_glass_chunk})GLASS\"\nR\"GLASS(")
    string(SUBSTRING "${stack_glass_remaining}" 12000 -1 stack_glass_remaining)
    string(LENGTH "${stack_glass_remaining}" stack_glass_length)
endwhile()
string(APPEND STACK_GLASS_MATERIAL_SOURCE "${stack_glass_remaining}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Glass/Shaders/EmbeddedMaterial.h.in"
    "${CMAKE_BINARY_DIR}/generated/src/Renderer/Glass/EmbeddedMaterial.h" @ONLY)

if (BUILD_TESTING)
    foreach(suite IN ITEMS profile frame_contract material_gl presets)
        add_executable(StackGlass_${suite} "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/glass/test_${suite}.cpp")
        target_include_directories(StackGlass_${suite} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_features(StackGlass_${suite} PRIVATE cxx_std_17)
    endforeach()
    add_executable(StackGlass_motion "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/glass/test_motion.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/Glass/ProximityJoins.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Animation/AnimationTypes.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Animation/ScalarChannel.cpp")
    target_include_directories(StackGlass_motion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_features(StackGlass_motion PRIVATE cxx_std_17)
    add_test(NAME StackGlassMotion COMMAND StackGlass_motion)
    add_test(NAME StackGlassProfile COMMAND StackGlass_profile)
    add_test(NAME StackGlassFrameContract COMMAND StackGlass_frame_contract)
    add_test(NAME StackGlassMaterialBinding COMMAND StackGlass_material_gl "${STACK_GLASS_SHADER}")
    add_test(NAME StackGlassPreset COMMAND StackGlass_presets "${CMAKE_CURRENT_SOURCE_DIR}/tools/tests/glass/clear-control-refined.json")
endif()
