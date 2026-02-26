# StepMarch v1.0.0 - Release Notes

## 🎉 Official Release

**Version:** 1.0.0  
**Date:** February 26, 2026  
**Status:** Production Ready

---

## 📦 What's Included

### Source Code
- `src/gui_multi_fractal.cpp` - Main application (18,000+ lines)
- `src/multi_fractal.cl` - Preview rendering kernel
- `src/high_quality.cl` - High-quality rendering kernel
- `src/animation.h` - Animation system with keyframe support
- `src/fractal_types.h` - Fractal type definitions
- `src/png_writer.c/h` - PNG export utility
- `src/imgui/` - Dear ImGui library

### Documentation
- `README.md` - Comprehensive user guide
- `CHANGELOG.md` - Version history and future plans
- `LICENSE` - MIT License
- `.openclaw.json` - OpenClaw manifest for future updates

### Build System
- `Makefile` - Multi-target build system
- `.gitignore` - Git ignore rules

---

## 🚀 Quick Start

```bash
# Build
cd stepmarch
make multi

# Run
./stepmarch_multi
```

---

## ✨ Key Features

1. **7 Fractal Types** - Mandelbrot, Julia, Mandelbulb, Mandelbox, Apollonian, Menger, Sierpinski
2. **Animation System** - Keyframes, timeline, 24 FPS playback
3. **High-Quality Rendering** - AA, soft shadows, AO, DOF, tone mapping
4. **Color Ramps** - 8-stop gradients with interpolation
5. **Project System** - Save/load full state
6. **Export** - PNG sequences and MP4 via FFmpeg

---

## 🐛 Known Limitations

- Path tracing mode removed in favor of enhanced ray marching
- No environment/HDRI lighting yet
- Single GPU support only
- Linux/macOS primary (Windows via WSL)

---

## 🔮 Future Roadmap

See `CHANGELOG.md` for planned features including:
- More fractal types
- Volumetric effects
- Scripting API
- Multi-GPU support

---

## 📞 Support

- GitHub Issues: Report bugs and request features
- OpenClaw: Use `.openclaw.json` manifest for AI-assisted updates

---

**Built with ❤️ by Jeff Abraham & OpenClaw**
