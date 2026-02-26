# Changelog

All notable changes to StepMarch will be documented in this file.

## [1.0.0] - 2026-02-26

### Added
- Initial release with 7 fractal types (3D Mandelbrot, Julia, Mandelbulb, Mandelbox, Apollonian, Menger, Sierpinski)
- GPU-accelerated rendering via OpenCL
- Houdini-style animation system with keyframes and timeline
- High-quality render mode with:
  - Supersampling anti-aliasing (1x-4x)
  - Configurable soft shadows (1-64 samples)
  - Ambient occlusion (0-32 samples)
  - Depth of field with adjustable aperture
  - ACES filmic tone mapping
  - Exposure and contrast controls
- Color ramp system with 8 stops and interpolation modes
- Material system (specular, shininess, metallic)
- Project save/load (.step files)
- PNG sequence and MP4 export (via FFmpeg)
- Default scene auto-save/load
- Color ramp preset system

### Technical
- OpenCL kernel for preview rendering
- Separate high-quality render kernel
- ImGui-based user interface
- SDL2 for windowing and input
- libpng for image export

## Future Ideas

### Rendering
- [ ] Environment lighting / HDRI support
- [ ] Subsurface scattering for organic fractals
- [ ] Volumetric fog/atmosphere
- [ ] Screen-space reflections
- [ ] Motion blur for animation

### Features
- [ ] More fractal types (KIFS, IFS, etc.)
- [ ] Voxel-based export for 3D printing
- [ ] Network/distributed rendering
- [ ] Python scripting API
- [ ] Real-time collaborative editing

### Performance
- [ ] Adaptive sampling for high-quality renders
- [ ] BVH acceleration structures
- [ ] Multi-GPU support
