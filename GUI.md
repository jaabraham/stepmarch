# StepMarch GUI

A real-time interactive fractal renderer with Houdini-style interface.

## Features

- **Left Panel**: Real-time preview window (adjustable resolution)
- **Right Panel**: Parameter controls with sliders and inputs
- **Live Updates**: Change parameters and see results instantly
- **High-Res Render**: Export final image at full resolution

## Build Requirements

- SDL2
- OpenGL
- Dear ImGui (included)
- OpenCL (already configured)
- libpng

## Build

```bash
cd stepmarch
make gui
```

## Controls

| Control | Description |
|---------|-------------|
| **Preview** | Render at preview resolution (fast) |
| **Render** | Export high-resolution image |
| **Sliders** | Adjust any parameter in real-time |
| **Input fields** | Type exact values |

## Architecture

```
+-------------------------------------------+
|  StepMarch GUI                            |
|  +------------------+  +----------------+ |
|  |                  |  | Parameters     | |
|  |   OpenGL         |  | - Camera       | |
|  |   Preview        |  | - Light        | |
|  |   Window         |  | - Fractal      | |
|  |                  |  | - Floor        | |
|  |                  |  |                | |
|  |  [Preview Btn]   |  | [Render Btn]   | |
|  +------------------+  +----------------+ |
+-------------------------------------------+
           |                    |
           v                    v
    OpenCL Kernel         PNG Export
    (GPU Compute)         (High Res)
```
