# UGC Renderer Lab

`UGC Renderer Lab` is a modern real-time renderer prototype for learning and demonstrating game rendering engine development skills, with a long-term focus on lightweight UGC editor workflows.

## Project Goal

This project is designed as a portfolio-grade rendering engine prototype for game rendering engine and UGC engine development roles.

The first milestone is not to build a full commercial game engine. Instead, the project focuses on:

- A clean C++20 engine architecture.
- A Direct3D 12 rendering backend.
- A Render Graph based rendering pipeline.
- PBR, shadows, post-processing, and profiling.
- A lightweight editor workflow for scene assembly and UGC-style content creation.

## Recommended GitHub Repository

- Repository name: `ugc-renderer-lab`
- Description: `A D3D12-based real-time renderer prototype focused on Render Graph, PBR, profiling, and lightweight UGC editor workflow.`

## Planned Features

- C++20 + CMake project structure.
- Windows platform layer.
- Direct3D 12 RHI.
- Render Graph.
- glTF 2.0 asset loading.
- Metallic-Roughness PBR.
- IBL and shadow mapping.
- Bloom, tone mapping, and anti-aliasing.
- CPU/GPU profiling.
- Shader hot reload.
- ImGui-based editor.
- Scene hierarchy, inspector, viewport, gizmo, prefab, and serialization.

## Directory Layout

```text
.
├── README.md
├── PROJECT_PLAN.md
├── docs/
├── engine/
├── sandbox/
├── tools/
├── assets/
├── third_party/
└── scripts/
```

## Current Status

Repository bootstrap is complete.

Current milestone:

- `CMake` project scaffold
- `engine/` static library
- `sandbox/` Win32 executable
- Windows window creation and message loop
- D3D12 device, swapchain, RTV heap, and frame clear path
- Runtime HLSL compilation via `D3DCompileFromFile`
- A first colored triangle pipeline and vertex buffer
- A reusable `ShaderCompiler` helper for runtime shader loading
- A first `GpuBuffer` abstraction with default-heap upload staging
- Descriptor heap allocation for RTV and CBV paths
- A constant buffer driven MVP transform for animated rendering
- Indexed drawing with a first `Mesh` abstraction
- Shared mesh rendering with multiple material-driven render items
- Perspective camera, view-projection transform, and depth-buffered rendering
- Keyboard orbit camera controls for interactive scene inspection
- WIC-based external image loading, texture asset caching, and SRV-based textured material sampling

## Build Requirements

- Windows 10/11
- CMake 3.26+
- Visual Studio 2022 Build Tools or Visual Studio 2022 with C++ workload
- Windows SDK with D3D12 headers and libraries

## Build

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug
```

Output executable:

```text
build/vs2022-debug/sandbox/Debug/ugc_renderer_sandbox.exe
```

## Near-Term Roadmap

- Introduce core utility modules and app lifecycle cleanup
- Start the D3D12 abstraction layer for resources and descriptors
- Add ImGui integration for runtime inspection
- Add descriptor allocation and a first constant-buffer path
- Add shader reload and pipeline reload workflow
- Add a mesh/material resource split suitable for glTF import
- Add camera controls and a reusable scene graph transform path
- Add camera-relative movement and editor-style gizmo interaction
- Add material parameter blocks and prepare for glTF texture slots

## Controls

- `A / D`: orbit camera left / right
- `W / S`: orbit camera up / down
- `Q / E`: zoom in / out
- `R`: reset camera

## License

No license has been selected yet.
