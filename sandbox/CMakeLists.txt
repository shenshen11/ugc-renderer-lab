add_executable(
    ugc_renderer_sandbox
    WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(
    ugc_renderer_sandbox
    PRIVATE
        ugc_renderer_engine
)

add_custom_command(
    TARGET ugc_renderer_sandbox
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/engine/shaders
            $<TARGET_FILE_DIR:ugc_renderer_sandbox>/shaders
)
