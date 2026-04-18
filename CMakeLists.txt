cmake_minimum_required(VERSION 3.26)

project(
    UGCRendererLab
    VERSION 0.1.0
    DESCRIPTION "A D3D12-based real-time renderer prototype for render graph and lightweight UGC workflows."
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UGC_RENDERER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc /Zc:preprocessor /utf-8)
    if(UGC_RENDERER_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
endif()

add_subdirectory(engine)
add_subdirectory(sandbox)
add_subdirectory(tools)
