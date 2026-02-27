# Changelog

All notable changes to StepMarch will be documented in this file.

## [1.0.2] - 2026-02-26

### Changed
- **Escape key behavior**: No longer exits the program
  - ESC now releases mouse capture if the cursor is "lost" during camera navigation
  - Visual indicator shown in red when mouse is captured: "MOUSE CAPTURED - Press ESC to release"
  - Program exit moved to File > Exit menu and window close button only

## [1.0.1] - 2026-02-26

### Fixed
- **Animation interpolation bug**: Fixed fractal_type (and other integer parameters) jumping between keyframes during playback
  - Added explicit copy constructor to `Params` struct to ensure all fields are correctly copied during keyframe interpolation
  - Added explicit safeguard to preserve integer parameters (fractal_type, max_iter, hollow, seed_offset, floor_enable, metallic, color_mode) from first keyframe
  - Integer parameters are now correctly treated as discrete values that should not be interpolated

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
