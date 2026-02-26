// Multi-Fractal Support for StepMarch
// Defines all fractal types and their specific parameters

#ifndef FRACTAL_TYPES_H
#define FRACTAL_TYPES_H

// Fractal type enumeration
enum FractalType {
    FRACTAL_MANDELBROT_3D = 0,  // Original 3D Mandelbrot (z^2 + c)
    FRACTAL_JULIA_3D,           // 3D Julia set
    FRACTAL_MANDELBULB,         // Mandelbulb (power 8)
    FRACTAL_MANDELBOX,          // Mandelbox
    FRACTAL_APOLLONIAN,         // Apollonian Gasket
    FRACTAL_MENGER,             // Menger Sponge
    FRACTAL_SIERPINSKI,         // Sierpinski Pyramid/Tetrahedron
    FRACTAL_COUNT
};

// Fractal-specific parameters (in addition to base Params)
typedef struct {
    // Fractal type
    int fractal_type;
    
    // Julia parameters (for Julia sets)
    float julia_cx, julia_cy, julia_cz;
    
    // Mandelbulb power
    float mandelbulb_power;
    
    // Mandelbox parameters
    float mandelbox_scale;
    float mandelbox_folding_limit;
    float mandelbox_min_radius;
    float mandelbox_fixed_radius;
    
    // Apollonian parameters
    float apollonian_scale;
    int apollonian_iterations;
    
    // Menger parameters
    float menger_scale;
    int menger_iterations;
    
    // Sierpinski parameters
    float sierpinski_scale;
    int sierpinski_iterations;
    
} FractalParams;

// Default fractal parameters
inline FractalParams default_fractal_params() {
    FractalParams fp = {};
    fp.fractal_type = FRACTAL_MANDELBROT_3D;
    
    // Julia defaults
    fp.julia_cx = -0.8f;
    fp.julia_cy = 0.0f;
    fp.julia_cz = 0.0f;
    
    // Mandelbulb defaults
    fp.mandelbulb_power = 8.0f;
    
    // Mandelbox defaults
    fp.mandelbox_scale = 2.0f;
    fp.mandelbox_folding_limit = 1.0f;
    fp.mandelbox_min_radius = 0.5f;
    fp.mandelbox_fixed_radius = 1.0f;
    
    // Apollonian defaults
    fp.apollonian_scale = 3.0f;
    fp.apollonian_iterations = 10;
    
    // Menger defaults
    fp.menger_scale = 3.0f;
    fp.menger_iterations = 5;
    
    // Sierpinski defaults
    fp.sierpinski_scale = 2.0f;
    fp.sierpinski_iterations = 10;
    
    return fp;
}

// Fractal type names for GUI
inline const char* get_fractal_name(int type) {
    switch (type) {
        case FRACTAL_MANDELBROT_3D: return "3D Mandelbrot";
        case FRACTAL_JULIA_3D:      return "3D Julia";
        case FRACTAL_MANDELBULB:    return "Mandelbulb";
        case FRACTAL_MANDELBOX:     return "Mandelbox";
        case FRACTAL_APOLLONIAN:    return "Apollonian Gasket";
        case FRACTAL_MENGER:        return "Menger Sponge";
        case FRACTAL_SIERPINSKI:    return "Sierpinski Pyramid";
        default: return "Unknown";
    }
}

// Get fractal type count
inline int get_fractal_count() {
    return FRACTAL_COUNT;
}

#endif // FRACTAL_TYPES_H
