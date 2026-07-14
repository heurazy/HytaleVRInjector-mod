cmake_minimum_required(VERSION 3.20)

project(hytale_camera_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HYTALE_BUILD_EXPERIMENTAL_PROBE "Build the crash-prone OpenGL probe" OFF)

add_subdirectory(src/ui_scale_hook)

function(hytale_target_defaults target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
endfunction()

if(HYTALE_BUILD_EXPERIMENTAL_PROBE)
    add_library(hytale_camera_probe SHARED
        src/probe.cpp
    )
    target_compile_definitions(hytale_camera_probe PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        HYTALE_CAMERA_PROBE_DLL
    )
endif()

add_library(hytale_vr_camera_hook SHARED
    src/vr_camera_hook.cpp
)

set(HYTALE_VR_BINDING_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_bindings_oculus_touch.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_bindings_knuckles.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_bindings_vive_controller.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_bindings_holographic_controller.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_bindings_hpmotioncontroller.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_bindings_vive_cosmos_controller.json"
)

set_target_properties(hytale_vr_camera_hook PROPERTIES OUTPUT_NAME "hytale_vr_camera_hook_v122_floor_aligned")

add_custom_command(TARGET hytale_vr_camera_hook POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr.vrmanifest"
        "$<TARGET_FILE_DIR:hytale_vr_camera_hook>/hytalevr.vrmanifest"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_actions.json"
        "$<TARGET_FILE_DIR:hytale_vr_camera_hook>/hytalevr_actions.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HYTALE_VR_BINDING_FILES}
        "$<TARGET_FILE_DIR:hytale_vr_camera_hook>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:hytale_vr_camera_hook>/assets"
)

target_compile_definitions(hytale_vr_camera_hook PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(hytale_vr_camera_hook PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/headers"
)

target_link_libraries(hytale_vr_camera_hook PRIVATE
    opengl32
    gdiplus
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib"
)

add_executable(hytale_camera_dashboard WIN32
    src/player_teleporter.cpp
)
add_dependencies(hytale_camera_dashboard HytaleUIScaleHook)

add_executable(hytale_viewtarget_writer
    src/viewtarget_writer.cpp
)

add_executable(hytale_vr_hook_smoketest
    src/vr_hook_smoketest.cpp
)

add_library(hytale_noesis_probe SHARED
    src/noesis_probe.cpp
)

add_executable(hytale_noesis_probe_tool
    src/noesis_probe_tool.cpp
)

add_executable(hytale_player_teleporter WIN32
    src/player_teleporter.cpp
)

target_compile_definitions(hytale_camera_dashboard PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    HYTALE_CAMERA_MODE
)

target_compile_definitions(hytale_viewtarget_writer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_compile_definitions(hytale_vr_hook_smoketest PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_compile_definitions(hytale_noesis_probe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_compile_definitions(hytale_noesis_probe_tool PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_compile_definitions(hytale_player_teleporter PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    HYTALE_CAMERA_MODE
)

target_link_libraries(hytale_camera_dashboard PRIVATE
    comctl32
    shell32
    gdiplus
    dwmapi
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib"
)

target_include_directories(hytale_camera_dashboard PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/headers"
)

target_link_libraries(hytale_player_teleporter PRIVATE
    comctl32
    shell32
    gdiplus
    dwmapi
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib"
)

target_include_directories(hytale_player_teleporter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/headers"
)

add_custom_command(TARGET hytale_camera_dashboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll"
        "$<TARGET_FILE_DIR:hytale_camera_dashboard>/openvr_api.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:HytaleUIScaleHook>"
        "$<TARGET_FILE_DIR:hytale_camera_dashboard>/HytaleUIScaleHook.dll"
)

set(hytale_runtime_targets
    hytale_vr_camera_hook
    hytale_camera_dashboard
    hytale_viewtarget_writer
    hytale_vr_hook_smoketest
    hytale_noesis_probe
    hytale_noesis_probe_tool
    hytale_player_teleporter
)
if(HYTALE_BUILD_EXPERIMENTAL_PROBE)
    list(APPEND hytale_runtime_targets hytale_camera_probe)
endif()

foreach(target IN LISTS hytale_runtime_targets)
    hytale_target_defaults(${target})
endforeach()

set_target_properties(${hytale_runtime_targets} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(hytale_vr_math_tests tests/vr_math_tests.cpp)
    target_include_directories(hytale_vr_math_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(hytale_vr_math_tests PRIVATE user32)
    hytale_target_defaults(hytale_vr_math_tests)
    add_test(NAME vr_math COMMAND hytale_vr_math_tests)
    add_test(NAME vr_bindings
        COMMAND ${CMAKE_COMMAND}
            "-DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_vr_bindings.cmake"
    )
endif()

install(TARGETS
    HytaleUIScaleHook
    hytale_vr_camera_hook
    hytale_camera_dashboard
    hytale_player_teleporter
    hytale_viewtarget_writer
    hytale_vr_hook_smoketest
    hytale_noesis_probe
    hytale_noesis_probe_tool
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr.vrmanifest"
    "${CMAKE_CURRENT_SOURCE_DIR}/hytalevr_actions.json"
    ${HYTALE_VR_BINDING_FILES}
    DESTINATION .
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION .
)
install(DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    DESTINATION .
)
