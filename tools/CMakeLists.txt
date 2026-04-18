add_executable(
    ugc_renderer_gltf_probe
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gltf_probe.cpp
)

target_link_libraries(
    ugc_renderer_gltf_probe
    PRIVATE
        ugc_renderer_engine
)

add_custom_command(
    TARGET ugc_renderer_gltf_probe
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:ugc_renderer_gltf_probe>/assets
)
