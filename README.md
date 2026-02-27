# StepMarch v1.0.3

**GPU-Accelerated Multi-Fractal 3D Renderer with Animation System**

![Version](https://img.shields.io/badge/version-1.0.3-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![OpenCL](https://img.shields.io/badge/GPU-OpenCL-orange)

## 🎯 Overview

StepMarch is a professional-grade fractal renderer featuring 7 different 3D fractal types, a Houdini-style animation system, and high-quality rendering with supersampling anti-aliasing, soft shadows, ambient occlusion, depth of field, and filmic tone mapping.

## ✨ Features

### 7 Fractal Types
| Fractal | Description | Special Parameters |
|---------|-------------|-------------------|
| **3D Mandelbrot** | Classic z² + c in 3D | Standard iteration |
| **3D Julia** | Julia set with configurable seed | Julia CX, CY, CZ |
| **Mandelbulb** | Power-8 spherical fractal | Power (2-20) |
| **Mandelbox** | Box folding + sphere folding | Scale, Folding Limit |
| **Apollonian Gasket** | Sphere packing fractal | Scale |
| **Menger Sponge** | 3D cube subdivision | Scale |
| **Sierpinski Pyramid** | Tetrahedron subdivision | Scale |

### Rendering Features
- **Supersampling Anti-Aliasing** - 1x to 4x for clean edges
- **Soft Shadows** - Configurable samples (1-64) for natural shadows
- **Ambient Occlusion** - Contact shadows for depth
- **Depth of Field** - Camera bokeh with adjustable aperture
- **Filmic Tone Mapping** - ACES with exposure and contrast controls
- **Color Ramps** - Customizable 8-stop gradients with interpolation
- **Material System** - Specular, shininess, metallic options

### Animation System
- **Keyframe-based** animation with smooth interpolation
- **Timeline** with playback controls (24 FPS)
- **Batch rendering** to PNG sequences
- **MP4 export** via FFmpeg integration
- **Project save/load** with full state preservation

## 🚀 Quick Start

### Dependencies
```bash
# Ubuntu/Debian
sudo apt-get install build-essential libsdl2-dev libpng-dev ocl-icd-opencl-dev

# Arch Linux
sudo pacman -S base-devel sdl2 libpng opencl-headers

# macOS (with Homebrew)
brew install sdl2 libpng
```

### Build
```bash
cd stepmarch
make multi
./stepmarch_multi
```

### Basic Usage
1. **Explore** - Use Alt+LMB to orbit, Alt+MMB to pan, Alt+Scroll to zoom
2. **Adjust** - Modify fractal parameters in the right panel
3. **Animate** - Add keyframes on the timeline
4. **Render** - Click "Render Still" for high-quality output

## 🎮 Controls

| Action | Control |
|--------|---------|
| Orbit Camera | Alt + Left Mouse |
| Pan Camera | Alt + Middle Mouse |
| Zoom | Alt + Scroll or Scroll |
| Play/Pause Animation | Play button or Space |
| Add Keyframe | "+" button on timeline |
| Delete Keyframe | Right-click on keyframe |

## 🎨 High-Quality Render Settings

Access via **"Render Still"** button:

| Setting | Options | Effect |
|---------|---------|--------|
| Anti-Aliasing | 1x, 2x, 3x, 4x | Edge smoothness |
| Shadow Quality | Hard to Maximum | Shadow softness |
| Ambient Occlusion | Off to High | Contact shadow detail |
| Depth of Field | On/Off + aperture | Bokeh effect |
| Exposure | 0.1 - 5.0 | Brightness |
| Contrast | 0.5 - 2.0 | Punchiness |

## 📁 File Structure

```
stepmarch/
├── src/
│   ├── multi_fractal.cl      # Preview render kernel
│   ├── high_quality.cl       # High-quality render kernel
│   ├── path_trace.cl         # Path tracing (experimental)
│   ├── gui_multi_fractal.cpp # Main GUI application
│   ├── animation.h           # Animation system
│   └── ...
├── obj/                      # Build objects
├── output/                   # Default render output
├── Makefile
└── README.md
```

## 💾 Project Files

Save/load projects with `.step` extension:
- All parameters and keyframes
- Color ramp presets
- Camera positions
- Render settings

## 🎬 Animation Export

### PNG Sequence
Renders frames to `output/frame_####.png`

### MP4 (requires FFmpeg)
```bash
# High quality H.264
ffmpeg -framerate 24 -i "output/frame_%04d.png" \
       -c:v libx264 -preset slow -crf 17 -pix_fmt yuv420p \
       output.mp4
```

## 🔧 Configuration

Default scene auto-loads from:
```
~/.config/stepmarch/default.scene
```

Color ramp presets stored in:
```
~/.config/stepmarch/presets/
```

## 🖥️ System Requirements

- **GPU**: OpenCL 1.2+ capable (NVIDIA, AMD, Intel)
- **RAM**: 4GB minimum, 8GB recommended
- **OS**: Linux, macOS, Windows (with WSL/MinGW)

## 🐛 Troubleshooting

### OpenCL not found
```bash
# Check available platforms
clinfo

# Install drivers
# NVIDIA: nvidia-opencl-icd
# AMD: rocm-opencl
# Intel: intel-opencl-icd
```

### Slow performance
- Reduce "Max Iterations" for preview
- Lower shadow/ AO samples temporarily
- Use "Fast Render" for testing

## 📝 License

MIT License - See LICENSE file for details

## 🙏 Credits

Created by Jeff Abraham with OpenClaw assistance

---

**Version 1.0.3** - UI Polish Release
