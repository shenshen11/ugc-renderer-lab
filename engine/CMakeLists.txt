set(UGC_RENDERER_ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(UGC_RENDERER_ENGINE_SOURCES
    ${UGC_RENDERER_ENGINE_ROOT}/src/app/application.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/asset/gltf_loader.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/asset/gltf_mesh_builder.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/asset/gltf_scene_builder.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/asset/json_parser.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/core/logger.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/platform/window.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/constant_buffer.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/descriptor_allocator.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/gpu_buffer.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/image_loader.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/material_manager.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/mesh.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/d3d12_renderer.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/shader_compiler.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/texture_manager.cpp
    ${UGC_RENDERER_ENGINE_ROOT}/src/render/texture2d.cpp
)

add_library(ugc_renderer_engine STATIC ${UGC_RENDERER_ENGINE_SOURCES})

target_include_directories(
    ugc_renderer_engine
    PUBLIC
        ${UGC_RENDERER_ENGINE_ROOT}/include
)

target_compile_definitions(
    ugc_renderer_engine
    PUBLIC
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_link_libraries(
    ugc_renderer_engine
    PUBLIC
        d3d12
        d3dcompiler
        dxgi
        dxguid
        user32
        gdi32
        windowscodecs
)
