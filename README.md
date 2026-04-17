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

Project planning and repository bootstrap are in progress.

The next development step is to scaffold:

- `CMakeLists.txt`
- `engine/` core library
- `sandbox/` executable
- Windows window creation
- D3D12 device and swapchain initialization

## License

No license has been selected yet.
