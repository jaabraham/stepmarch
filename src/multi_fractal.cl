// Multi-Fractal Ray Marching Kernel
// Supports: 3D Mandelbrot, 3D Julia, Mandelbulb, Mandelbox, 
//           Apollonian Gasket, Menger Sponge, Sierpinski Pyramid

// Math constants
#define PI 3.14159265359f
#define EPSILON 0.001f
#define MAX_DIST 100.0f
#define MAX_SHADOW_STEPS 20000
#define BISECT_STEPS 10
#define ABSOLUTE_MAX_STEPS 50000  // Hard limit for array sizes

// Fractal type constants (must match FractalType enum)
#define FRACTAL_MANDELBROT_3D 0
#define FRACTAL_JULIA_3D 1
#define FRACTAL_MANDELBULB 2
#define FRACTAL_MANDELBOX 3
#define FRACTAL_APOLLONIAN 4
#define FRACTAL_MENGER 5
#define FRACTAL_SIERPINSKI 6

// Structure to hold fractal data
typedef struct {
    float iteration;
    float trap;
} FractalData;

// ============================================
// UTILITY FUNCTIONS
// ============================================

inline float length3(float3 v) {
    return sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
}

inline float3 normalize3(float3 v) {
    float len = length3(v);
    if (len > 0.0f) {
        return (float3)(v.x/len, v.y/len, v.z/len);
    }
    return v;
}

inline float dot3(float3 a, float3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

inline float3 cross3(float3 a, float3 b) {
    return (float3)(
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    );
}

inline float3 mix3(float3 a, float3 b, float t) {
    return (float3)(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

// Color ramp sampling (matches CPU implementation)
#define MAX_RAMP_STOPS 8

// Helper to build arrays from individual kernel args
inline void build_ramp_arrays(
    float ramp_pos0, float ramp_pos1, float ramp_pos2, float ramp_pos3,
    float ramp_pos4, float ramp_pos5, float ramp_pos6, float ramp_pos7,
    float ramp_r0, float ramp_r1, float ramp_r2, float ramp_r3,
    float ramp_r4, float ramp_r5, float ramp_r6, float ramp_r7,
    float ramp_g0, float ramp_g1, float ramp_g2, float ramp_g3,
    float ramp_g4, float ramp_g5, float ramp_g6, float ramp_g7,
    float ramp_b0, float ramp_b1, float ramp_b2, float ramp_b3,
    float ramp_b4, float ramp_b5, float ramp_b6, float ramp_b7,
    int ramp_interp0, int ramp_interp1, int ramp_interp2, int ramp_interp3,
    int ramp_interp4, int ramp_interp5, int ramp_interp6, int ramp_interp7,
    float positions[MAX_RAMP_STOPS],
    float colors_r[MAX_RAMP_STOPS],
    float colors_g[MAX_RAMP_STOPS],
    float colors_b[MAX_RAMP_STOPS],
    int interp_modes[MAX_RAMP_STOPS]
) {
    positions[0] = ramp_pos0; colors_r[0] = ramp_r0; colors_g[0] = ramp_g0; colors_b[0] = ramp_b0; interp_modes[0] = ramp_interp0;
    positions[1] = ramp_pos1; colors_r[1] = ramp_r1; colors_g[1] = ramp_g1; colors_b[1] = ramp_b1; interp_modes[1] = ramp_interp1;
    positions[2] = ramp_pos2; colors_r[2] = ramp_r2; colors_g[2] = ramp_g2; colors_b[2] = ramp_b2; interp_modes[2] = ramp_interp2;
    positions[3] = ramp_pos3; colors_r[3] = ramp_r3; colors_g[3] = ramp_g3; colors_b[3] = ramp_b3; interp_modes[3] = ramp_interp3;
    positions[4] = ramp_pos4; colors_r[4] = ramp_r4; colors_g[4] = ramp_g4; colors_b[4] = ramp_b4; interp_modes[4] = ramp_interp4;
    positions[5] = ramp_pos5; colors_r[5] = ramp_r5; colors_g[5] = ramp_g5; colors_b[5] = ramp_b5; interp_modes[5] = ramp_interp5;
    positions[6] = ramp_pos6; colors_r[6] = ramp_r6; colors_g[6] = ramp_g6; colors_b[6] = ramp_b6; interp_modes[6] = ramp_interp6;
    positions[7] = ramp_pos7; colors_r[7] = ramp_r7; colors_g[7] = ramp_g7; colors_b[7] = ramp_b7; interp_modes[7] = ramp_interp7;
}

inline float3 sample_color_ramp(
    float t,
    int num_stops,
    float positions[MAX_RAMP_STOPS],
    float colors_r[MAX_RAMP_STOPS],
    float colors_g[MAX_RAMP_STOPS],
    float colors_b[MAX_RAMP_STOPS],
    int interp_modes[MAX_RAMP_STOPS]
) {
    if (num_stops <= 0) {
        return (float3)(1.0f, 1.0f, 1.0f);
    }
    if (num_stops == 1) {
        return (float3)(colors_r[0], colors_g[0], colors_b[0]);
    }
    
    // Clamp t
    t = clamp(t, 0.0f, 1.0f);
    
    // Find which segment we're in
    int idx = 0;
    for (int i = 0; i < num_stops - 1; i++) {
        if (t >= positions[i] && t <= positions[i + 1]) {
            idx = i;
            break;
        }
    }
    
    // Check interpolation mode (1 = constant, 0 = linear)
    if (interp_modes[idx] == 1) {
        // Constant interpolation - use left color
        return (float3)(colors_r[idx], colors_g[idx], colors_b[idx]);
    } else {
        // Linear interpolation
        float seg_range = positions[idx + 1] - positions[idx];
        float local_t = (seg_range > 0.0001f) ? (t - positions[idx]) / seg_range : 0.0f;
        
        float r = colors_r[idx] + (colors_r[idx + 1] - colors_r[idx]) * local_t;
        float g = colors_g[idx] + (colors_g[idx + 1] - colors_g[idx]) * local_t;
        float b = colors_b[idx] + (colors_b[idx + 1] - colors_b[idx]) * local_t;
        
        return (float3)(r, g, b);
    }
}

// Note: OpenCL has built-in clamp(), no need to define our own

// ============================================
// 3D MANDELBROT
// ============================================

inline FractalData get_mandelbrot_3d(float3 c, int max_iter, float escape_sq) {
    FractalData data;
    float3 z = (float3)(0.0f);
    float min_dist = 1e10f;
    
    for (int i = 0; i < max_iter; i++) {
        float r2 = dot3(z, z);
        min_dist = fmin(min_dist, r2);
        
        if (r2 > escape_sq) {
            float r = sqrt(r2);
            data.iteration = (float)i + 1.0f - log(log(r) / log(sqrt(escape_sq))) / log(2.0f);
            data.trap = sqrt(min_dist);
            return data;
        }
        
        float zx2 = z.x * z.x;
        float zy2 = z.y * z.y;
        float zz2 = z.z * z.z;
        
        z = (float3)(
            zx2 - zy2 - zz2 + c.x,
            2.0f * z.x * z.y + c.y,
            z.z * (z.x + z.y) + c.z
        );
    }
    
    data.iteration = (float)max_iter;
    data.trap = sqrt(min_dist);
    return data;
}

// ============================================
// 3D JULIA SET
// ============================================

inline FractalData get_julia_3d(float3 z, float3 c, int max_iter, float escape_sq) {
    FractalData data;
    float min_dist = 1e10f;
    
    for (int i = 0; i < max_iter; i++) {
        float r2 = dot3(z, z);
        min_dist = fmin(min_dist, r2);
        
        if (r2 > escape_sq) {
            float r = sqrt(r2);
            data.iteration = (float)i + 1.0f - log(log(r) / log(sqrt(escape_sq))) / log(2.0f);
            data.trap = sqrt(min_dist);
            return data;
        }
        
        float zx2 = z.x * z.x;
        float zy2 = z.y * z.y;
        float zz2 = z.z * z.z;
        
        z = (float3)(
            zx2 - zy2 - zz2 + c.x,
            2.0f * z.x * z.y + c.y,
            z.z * (z.x + z.y) + c.z
        );
    }
    
    data.iteration = (float)max_iter;
    data.trap = sqrt(min_dist);
    return data;
}

// ============================================
// MANDELBULB
// ============================================

inline FractalData get_mandelbulb(float3 pos, int max_iter, float bailout, float power) {
    FractalData data;
    float3 z = pos;
    float dr = 1.0f;
    float r = 0.0f;
    float min_dist = 1e10f;
    
    for (int i = 0; i < max_iter; i++) {
        r = length3(z);
        min_dist = fmin(min_dist, r);
        
        if (r > bailout) break;
        
        // Convert to spherical
        float theta = acos(z.z / r);
        float phi = atan2(z.y, z.x);
        
        // Apply power
        float zr = pow(r, power);
        theta = theta * power;
        phi = phi * power;
        
        // Convert back
        float sint = sin(theta);
        z = (float3)(
            sint * cos(phi),
            sint * sin(phi),
            cos(theta)
        ) * zr + pos;
        
        dr = pow(r, power - 1.0f) * power * dr + 1.0f;
    }
    
    // Distance estimator
    float dist = 0.5f * log(r) * r / dr;
    data.iteration = (dist < EPSILON) ? (float)max_iter : log(r);
    data.trap = min_dist;
    return data;
}

// ============================================
// MANDELBOX
// ============================================

inline FractalData get_mandelbox(float3 pos, int max_iter, float box_scale, float folding_limit) {
    FractalData data;
    float3 z = pos;
    float3 offset = z;
    float min_dist = 1e10f;
    float dr = 1.0f;
    
    for (int i = 0; i < max_iter; i++) {
        float r = length3(z);
        min_dist = fmin(min_dist, r);
        
        // Box fold
        z = clamp(z, -folding_limit, folding_limit) * 2.0f - z;
        
        // Sphere fold
        r = length3(z);
        if (r < 0.5f) {
            z *= 4.0f;
            dr *= 4.0f;
        } else if (r < 1.0f) {
            z /= (r * r);
            dr /= (r * r);
        }
        
        z = z * box_scale + offset;
        dr = dr * fabs(box_scale) + 1.0f;
    }
    
    float r = length3(z);
    data.iteration = (r < 100.0f) ? (float)max_iter : log(r);
    data.trap = min_dist;
    return data;
}

// ============================================
// APOLLONIAN GASKET (Sphere tracing version)
// ============================================

// Custom fractional part function (OpenCL fract requires 2 args)
inline float3 fract3(float3 x) {
    return x - floor(x);
}

inline float apollonian_sdf(float3 p, int iterations, float apo_scale) {
    float s = 1.0f;
    
    for (int i = 0; i < iterations; i++) {
        p = -1.0f + 2.0f * fract3(0.5f * p + 0.5f);
        
        float r2 = dot3(p, p);
        float k = apo_scale / r2;
        
        if (r2 < 0.0001f) break;
        
        p *= k;
        s *= k;
    }
    
    return length3(p) / s;
}

inline FractalData get_apollonian(float3 pos, int max_iter, float apo_scale) {
    FractalData data;
    float dist = apollonian_sdf(pos, max_iter, apo_scale);
    data.iteration = (dist < EPSILON) ? (float)max_iter : log(dist + 1.0f) * 10.0f;
    data.trap = dist;
    return data;
}

// ============================================
// MENGER SPONGE
// ============================================

inline float menger_sdf(float3 p, int iterations, float menger_scale) {
    float3 z = p;
    float s = 1.0f;
    float min_dist = 1e10f;
    
    for (int i = 0; i < iterations; i++) {
        float r = length3(z);
        min_dist = fmin(min_dist, r);
        
        // Box fold to [-1, 1]
        z = clamp(z, -1.0f, 1.0f) * 2.0f - z;
        
        // Scale and translate
        z *= menger_scale;
        s *= menger_scale;
        
        // Subtract cross (create holes)
        float da = length3((float3)(fmax(fabs(z.x) - 1.0f, 0.0f), fmax(fabs(z.y) - 1.0f, 0.0f), 0.0f));
        float db = length3((float3)(fmax(fabs(z.x) - 1.0f, 0.0f), 0.0f, fmax(fabs(z.z) - 1.0f, 0.0f)));
        float dc = length3((float3)(0.0f, fmax(fabs(z.y) - 1.0f, 0.0f), fmax(fabs(z.z) - 1.0f, 0.0f)));
        
        z = (float3)(
            copysign(fmin(fabs(z.x), 1.0f), z.x),
            copysign(fmin(fabs(z.y), 1.0f), z.y),
            copysign(fmin(fabs(z.z), 1.0f), z.z)
        );
    }
    
    float r = length3(z);
    return (r - 1.5f) / s;
}

inline FractalData get_menger(float3 pos, int max_iter, float menger_scale) {
    FractalData data;
    float dist = menger_sdf(pos, max_iter, menger_scale);
    data.iteration = (dist < EPSILON) ? (float)max_iter : log(dist + 1.0f) * 10.0f;
    data.trap = dist;
    return data;
}

// ============================================
// SIERPINSKI PYRAMID (Tetrahedron)
// ============================================

inline float sierpinski_sdf(float3 p, int iterations, float sierpinski_scale) {
    float3 z = p;
    float s = 1.0f;
    
    // Tetrahedron vertices
    float3 a = (float3)(1.0f, 1.0f, 1.0f);
    float3 b = (float3)(-1.0f, -1.0f, 1.0f);
    float3 c = (float3)(-1.0f, 1.0f, -1.0f);
    float3 d = (float3)(1.0f, -1.0f, -1.0f);
    
    for (int i = 0; i < iterations; i++) {
        // Find closest vertex
        float3 vertices[4] = {a, b, c, d};
        float min_dist = 1e10f;
        int closest = 0;
        
        for (int v = 0; v < 4; v++) {
            float dist = length3(z - vertices[v]);
            if (dist < min_dist) {
                min_dist = dist;
                closest = v;
            }
        }
        
        // Scale towards closest vertex
        z = (z - vertices[closest]) * sierpinski_scale + vertices[closest];
        s *= sierpinski_scale;
    }
    
    // Distance to tetrahedron
    float r = length3(z);
    return (r - 1.5f) / s;
}

inline FractalData get_sierpinski(float3 pos, int max_iter, float sierpinski_scale) {
    FractalData data;
    float dist = sierpinski_sdf(pos, max_iter, sierpinski_scale);
    data.iteration = (dist < EPSILON) ? (float)max_iter : log(dist + 1.0f) * 10.0f;
    data.trap = dist;
    return data;
}

// ============================================
// MAIN FRACTAL DISPATCHER
// ============================================

inline FractalData get_fractal_data(
    float3 pos,
    int fractal_type,
    int max_iter,
    float escape,
    // Julia params
    float julia_cx, float julia_cy, float julia_cz,
    // Mandelbulb params
    float mandelbulb_power,
    // Mandelbox params
    float mandelbox_scale, float mandelbox_folding,
    // Apollonian params
    float apollonian_scale,
    // Menger params
    float menger_scale,
    // Sierpinski params
    float sierpinski_scale
) {
    switch (fractal_type) {
        case FRACTAL_MANDELBROT_3D:
            return get_mandelbrot_3d(pos, max_iter, escape * escape);
            
        case FRACTAL_JULIA_3D:
            return get_julia_3d(pos, (float3)(julia_cx, julia_cy, julia_cz), max_iter, escape * escape);
            
        case FRACTAL_MANDELBULB:
            return get_mandelbulb(pos, max_iter, escape, mandelbulb_power);
            
        case FRACTAL_MANDELBOX:
            return get_mandelbox(pos, max_iter, mandelbox_scale, mandelbox_folding);
            
        case FRACTAL_APOLLONIAN:
            return get_apollonian(pos, max_iter, apollonian_scale);
            
        case FRACTAL_MENGER:
            return get_menger(pos, max_iter, menger_scale);
            
        case FRACTAL_SIERPINSKI:
            return get_sierpinski(pos, max_iter, sierpinski_scale);
            
        default:
            return get_mandelbrot_3d(pos, max_iter, escape * escape);
    }
}

// ============================================
// SOLID DETECTION FOR RAY MARCHING
// ============================================

inline bool is_point_solid(
    float3 p,
    int fractal_type,
    int max_iter,
    float esc2,
    float d_less, float d_greater,
    int hollow_mode,
    float cut_z,
    // Additional params
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power,
    float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale,
    float menger_scale,
    float sierpinski_scale
) {
    // Check clipping plane
    if (p.z > cut_z) return false;
    
    FractalData fd = get_fractal_data(
        p, fractal_type, max_iter, sqrt(esc2),
        julia_cx, julia_cy, julia_cz,
        mandelbulb_power,
        mandelbox_scale, mandelbox_folding,
        apollonian_scale,
        menger_scale,
        sierpinski_scale
    );
    
    // Hollow logic
    if (hollow_mode == 1 && fd.iteration >= (float)max_iter - 0.001f) return false;
    
    // Banding logic
    if (fd.iteration >= d_less && fd.iteration <= d_greater) return true;
    
    return false;
}

// ============================================
// RAY MARCHING KERNEL
// ============================================

typedef struct {
    float dist;
    int steps;
    int hit;
    int material;
    float3 pos;
    float3 normal;
} HitInfo;

// Helper to get "solid" iteration value that respects clipping and banding
// Returns max_iter for solid areas, 0 for empty/clipped areas
inline float get_solid_iteration(
    float3 p,
    int fractal_type,
    int max_iter,
    float esc2,
    float d_less, float d_greater,
    int hollow_mode,
    float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power,
    float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale,
    float menger_scale,
    float sierpinski_scale
) {
    // Check clipping plane (same as is_point_solid)
    if (p.z > cut_z) return 0.0f;
    
    FractalData fd = get_fractal_data(
        p, fractal_type, max_iter, sqrt(esc2),
        julia_cx, julia_cy, julia_cz,
        mandelbulb_power,
        mandelbox_scale, mandelbox_folding,
        apollonian_scale,
        menger_scale, sierpinski_scale
    );
    
    // Hollow logic
    if (hollow_mode == 1 && fd.iteration >= (float)max_iter - 0.001f) return 0.0f;
    
    // Banded solid logic - return max_iter if in solid band, 0 otherwise
    if (fd.iteration >= d_less && fd.iteration <= d_greater) return (float)max_iter;
    
    return 0.0f;
}

inline float3 compute_normal(
    float3 pos,
    float dt,
    int fractal_type,
    int max_iter,
    float esc2,
    float d_less, float d_greater,
    int hollow_mode,
    float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power,
    float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale,
    float menger_scale,
    float sierpinski_scale
) {
    float h = EPSILON;
    float3 n;
    
    n.x = get_solid_iteration(pos + (float3)(h,0,0), fractal_type, max_iter, esc2,
                              d_less, d_greater, hollow_mode, cut_z,
                              julia_cx, julia_cy, julia_cz, mandelbulb_power,
                              mandelbox_scale, mandelbox_folding, apollonian_scale,
                              menger_scale, sierpinski_scale) -
          get_solid_iteration(pos - (float3)(h,0,0), fractal_type, max_iter, esc2,
                              d_less, d_greater, hollow_mode, cut_z,
                              julia_cx, julia_cy, julia_cz, mandelbulb_power,
                              mandelbox_scale, mandelbox_folding, apollonian_scale,
                              menger_scale, sierpinski_scale);
    
    n.y = get_solid_iteration(pos + (float3)(0,h,0), fractal_type, max_iter, esc2,
                              d_less, d_greater, hollow_mode, cut_z,
                              julia_cx, julia_cy, julia_cz, mandelbulb_power,
                              mandelbox_scale, mandelbox_folding, apollonian_scale,
                              menger_scale, sierpinski_scale) -
          get_solid_iteration(pos - (float3)(0,h,0), fractal_type, max_iter, esc2,
                              d_less, d_greater, hollow_mode, cut_z,
                              julia_cx, julia_cy, julia_cz, mandelbulb_power,
                              mandelbox_scale, mandelbox_folding, apollonian_scale,
                              menger_scale, sierpinski_scale);
    
    n.z = get_solid_iteration(pos + (float3)(0,0,h), fractal_type, max_iter, esc2,
                              d_less, d_greater, hollow_mode, cut_z,
                              julia_cx, julia_cy, julia_cz, mandelbulb_power,
                              mandelbox_scale, mandelbox_folding, apollonian_scale,
                              menger_scale, sierpinski_scale) -
          get_solid_iteration(pos - (float3)(0,0,h), fractal_type, max_iter, esc2,
                              d_less, d_greater, hollow_mode, cut_z,
                              julia_cx, julia_cy, julia_cz, mandelbulb_power,
                              mandelbox_scale, mandelbox_folding, apollonian_scale,
                              menger_scale, sierpinski_scale);
    
    return normalize3(n);
}

inline float floor_sdf(float3 pos) {
    return pos.y + 1.5f;
}

inline float3 checkerboard(float3 pos) {
    float checker_scale = 0.5f;
    int check = ((int)floor(pos.x * checker_scale) + (int)floor(pos.z * checker_scale)) & 1;
    if (check) {
        return (float3)(0.2f, 0.2f, 0.2f);
    } else {
        return (float3)(0.8f, 0.8f, 0.8f);
    }
}

inline float calc_soft_shadow(
    float3 ro, float3 rd, float mint, float maxt, float k,
    int fractal_type, int max_iter, float esc2,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power,
    float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale,
    float menger_scale,
    float sierpinski_scale,
    float d_less, float d_greater, int hollow, float cut_z
) {
    float res = 1.0f;
    float t = mint;
    
    for (int j = 0; j < 32 && t < maxt; j++) {
        if (is_point_solid(ro + rd * t, fractal_type, max_iter, esc2, d_less, d_greater,
                          hollow, cut_z, julia_cx, julia_cy, julia_cz,
                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                          apollonian_scale, menger_scale, sierpinski_scale)) {
            return 0.0f;
        }
        res = fmin(res, k * 0.1f / t);
        t += 0.1f;
    }
    
    return clamp(res, 0.0f, 1.0f);
}

inline float3 shade(
    HitInfo hit, float3 rd, float3 light_pos,
    int fractal_type, int max_iter, float esc2,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power,
    float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale,
    float menger_scale,
    float sierpinski_scale,
    float d_less, float d_greater, int hollow, float cut_z,
    float floor_y, int floor_enable, float checker_size,
    float shadow,  // Pre-calculated shadow value
    float ao,  // Ambient occlusion
    float specular_intensity,
    float shininess,
    int metallic,
    int ramp_num_stops,
    float ramp_pos0, float ramp_pos1, float ramp_pos2, float ramp_pos3,
    float ramp_pos4, float ramp_pos5, float ramp_pos6, float ramp_pos7,
    float ramp_r0, float ramp_r1, float ramp_r2, float ramp_r3,
    float ramp_r4, float ramp_r5, float ramp_r6, float ramp_r7,
    float ramp_g0, float ramp_g1, float ramp_g2, float ramp_g3,
    float ramp_g4, float ramp_g5, float ramp_g6, float ramp_g7,
    float ramp_b0, float ramp_b1, float ramp_b2, float ramp_b3,
    float ramp_b4, float ramp_b5, float ramp_b6, float ramp_b7,
    int ramp_interp0, int ramp_interp1, int ramp_interp2, int ramp_interp3,
    int ramp_interp4, int ramp_interp5, int ramp_interp6, int ramp_interp7
) {
    // Build local arrays from kernel args
    float ramp_positions[MAX_RAMP_STOPS];
    float ramp_colors_r[MAX_RAMP_STOPS];
    float ramp_colors_g[MAX_RAMP_STOPS];
    float ramp_colors_b[MAX_RAMP_STOPS];
    int ramp_interp_modes[MAX_RAMP_STOPS];
    build_ramp_arrays(
        ramp_pos0, ramp_pos1, ramp_pos2, ramp_pos3, ramp_pos4, ramp_pos5, ramp_pos6, ramp_pos7,
        ramp_r0, ramp_r1, ramp_r2, ramp_r3, ramp_r4, ramp_r5, ramp_r6, ramp_r7,
        ramp_g0, ramp_g1, ramp_g2, ramp_g3, ramp_g4, ramp_g5, ramp_g6, ramp_g7,
        ramp_b0, ramp_b1, ramp_b2, ramp_b3, ramp_b4, ramp_b5, ramp_b6, ramp_b7,
        ramp_interp0, ramp_interp1, ramp_interp2, ramp_interp3,
        ramp_interp4, ramp_interp5, ramp_interp6, ramp_interp7,
        ramp_positions, ramp_colors_r, ramp_colors_g, ramp_colors_b, ramp_interp_modes
    );
    if (!hit.hit) {
        // Background gradient
        float t = 0.5f * (rd.y + 1.0f);
        return (float3)(1.0f, 1.0f, 1.0f) * (1.0f - t) + (float3)(0.5f, 0.7f, 1.0f) * t;
    }
    
    float3 light_dir = normalize3(light_pos - hit.pos);
    float3 view_dir = -rd;
    
    // Base color
    float3 base_color;
    if (hit.material == 0) {
        // Fractal - iteration based coloring
        FractalData fd = get_fractal_data(
            hit.pos, fractal_type, max_iter, sqrt(esc2),
            julia_cx, julia_cy, julia_cz,
            mandelbulb_power, mandelbox_scale, mandelbox_folding,
            apollonian_scale, menger_scale, sierpinski_scale
        );
        
        // Sample from color ramp - map iteration from del_less to del_greater to 0-1
        float t = (fd.iteration - d_less) / (d_greater - d_less);
        t = clamp(t, 0.0f, 1.0f);
        base_color = sample_color_ramp(t, ramp_num_stops, ramp_positions,
                                       ramp_colors_r, ramp_colors_g, ramp_colors_b, ramp_interp_modes);
    } else {
        // Checkerboard floor
        base_color = checkerboard(hit.pos);
    }
    
    // Diffuse lighting
    float diff = fmax(dot3(hit.normal, light_dir), 0.0f);
    
    // Ambient
    float ambient = 0.15f;
    
    // Specular - using material parameters (clamp to valid ranges)
    float safe_shininess = clamp(shininess, 1.0f, 128.0f);
    float safe_specular_intensity = clamp(specular_intensity, 0.0f, 2.0f);
    
    float3 half_dir = normalize3(light_dir + view_dir);
    float spec = pow(fmax(dot3(hit.normal, half_dir), 0.0f), safe_shininess);
    
    // Shadow is now passed as parameter (calculated in main kernel with light_radius)
    
    // Metallic: use base_color for specular instead of white
    float3 spec_color = (metallic == 1) ? base_color : (float3)(1.0f, 1.0f, 1.0f);
    
    // Combine - AO affects ambient term for more depth
    float ambient_occlusion = 0.3f + 0.7f * ao;  // AO darkens ambient (0.3 min ambient)
    float3 color = base_color * (ambient * ambient_occlusion + diff * shadow * 0.8f) +
                   spec_color * spec * shadow * safe_specular_intensity;

    return color;
}

// ============================================
// MAIN KERNEL
// ============================================

__kernel void render(
    __global float* output_color,
    __global float* output_depth,
    int width,
    int height,
    // Camera
    float3 camera_origin,
    float3 camera_target,
    float3 camera_up,
    float fov,
    // Light
    float3 light_pos,
    float light_radius,
    // Fractal type
    int fractal_type,
    // Base params
    float3 offset,
    float scale,
    int max_iter,
    float escape,
    float step_size,
    float dist_max,
    float del_less,
    float del_greater,
    int hollow,
    int seed_offset,
    float floor_y,
    int floor_enable,
    float checker_size,
    int max_steps,
    // Julia params
    float julia_cx, float julia_cy, float julia_cz,
    // Mandelbulb params
    float mandelbulb_power,
    // Mandelbox params
    float mandelbox_scale,
    float mandelbox_folding,
    // Apollonian params
    float apollonian_scale,
    // Menger params
    float menger_scale,
    // Sierpinski params
    float sierpinski_scale,
    // Color ramp params
    int ramp_num_stops,
    float ramp_pos0, float ramp_pos1, float ramp_pos2, float ramp_pos3,
    float ramp_pos4, float ramp_pos5, float ramp_pos6, float ramp_pos7,
    float ramp_r0, float ramp_r1, float ramp_r2, float ramp_r3,
    float ramp_r4, float ramp_r5, float ramp_r6, float ramp_r7,
    float ramp_g0, float ramp_g1, float ramp_g2, float ramp_g3,
    float ramp_g4, float ramp_g5, float ramp_g6, float ramp_g7,
    float ramp_b0, float ramp_b1, float ramp_b2, float ramp_b3,
    float ramp_b4, float ramp_b5, float ramp_b6, float ramp_b7,
    int ramp_interp0, int ramp_interp1, int ramp_interp2, int ramp_interp3,
    int ramp_interp4, int ramp_interp5, int ramp_interp6, int ramp_interp7,
    // Material params
    float specular_intensity,
    float shininess,
    int metallic
) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    
    // Calculate UV coordinates
    float u = (float)x / (float)width;
    float v = (float)y / (float)height;
    
    float aspect = (float)width / (float)height;
    float px = (2.0f * u - 1.0f) * aspect;
    float py = 1.0f - 2.0f * v;
    
    // Camera setup
    float3 forward = normalize3(camera_target - camera_origin);
    float3 right = normalize3(cross3(forward, camera_up));
    float3 up = cross3(right, forward);
    
    float fov_scale = tan(fov * 0.5f * PI / 180.0f);
    float3 rd = normalize3(forward + right * px * fov_scale + up * py * fov_scale);
    
    // Transform to fractal space
    float3 ro = (camera_origin / scale) + offset;
    float3 local_rd = rd;
    
    float esc2 = escape * escape;
    float dt = step_size / scale;
    float t_max = dist_max / scale;
    float cut_z = offset.z;
    float t = 0.0f;
    
    bool hit_fractal = false;
    bool hit_floor = false;
    float3 hit_pos;
    float3 norm = (float3)(0.0f, 1.0f, 0.0f);
    
    // Build color ramp arrays from kernel args
    float ramp_positions[MAX_RAMP_STOPS];
    float ramp_colors_r[MAX_RAMP_STOPS];
    float ramp_colors_g[MAX_RAMP_STOPS];
    float ramp_colors_b[MAX_RAMP_STOPS];
    int ramp_interp_modes[MAX_RAMP_STOPS];
    build_ramp_arrays(
        ramp_pos0, ramp_pos1, ramp_pos2, ramp_pos3, ramp_pos4, ramp_pos5, ramp_pos6, ramp_pos7,
        ramp_r0, ramp_r1, ramp_r2, ramp_r3, ramp_r4, ramp_r5, ramp_r6, ramp_r7,
        ramp_g0, ramp_g1, ramp_g2, ramp_g3, ramp_g4, ramp_g5, ramp_g6, ramp_g7,
        ramp_b0, ramp_b1, ramp_b2, ramp_b3, ramp_b4, ramp_b5, ramp_b6, ramp_b7,
        ramp_interp0, ramp_interp1, ramp_interp2, ramp_interp3,
        ramp_interp4, ramp_interp5, ramp_interp6, ramp_interp7,
        ramp_positions, ramp_colors_r, ramp_colors_g, ramp_colors_b, ramp_interp_modes
    );
    
    // Floor intersection
    float t_floor = 1e10f;
    float f_y = floor_y / scale;
    if (floor_enable == 1) {
        if (fabs(local_rd.y) > 0.0001f) {
            float tf = (f_y - ro.y) / local_rd.y;
            if (tf > 0.001f) t_floor = tf;
        }
    }
    
    // Ray march
    int step_limit = max_steps > 0 ? max_steps : ABSOLUTE_MAX_STEPS;
    for (int i = 0; i < step_limit && t < t_max; i++) {
        if (t > t_floor) break;
        
        float3 p = ro + local_rd * t;
        
        if (is_point_solid(p, fractal_type, max_iter, esc2, del_less, del_greater,
                          hollow, cut_z, julia_cx, julia_cy, julia_cz,
                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                          apollonian_scale, menger_scale, sierpinski_scale)) {
            // Refinement
            float t_low = t - dt;
            float t_high = t;
            
            for (int b = 0; b < BISECT_STEPS; b++) {
                float t_mid = (t_low + t_high) * 0.5f;
                if (is_point_solid(ro + local_rd * t_mid, fractal_type, max_iter, esc2,
                                  del_less, del_greater, hollow, cut_z,
                                  julia_cx, julia_cy, julia_cz,
                                  mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                  apollonian_scale, menger_scale, sierpinski_scale))
                    t_high = t_mid;
                else
                    t_low = t_mid;
            }
            
            hit_pos = ro + local_rd * t_high;
            hit_fractal = true;
            break;
        }
        t += dt;
    }
    
    // Resolve hit
    int hit_id = -1;
    
    if (!hit_fractal && t_floor < t_max) {
        hit_pos = ro + local_rd * t_floor;
        norm = (float3)(0.0f, 1.0f, 0.0f);
        hit_floor = true;
        hit_id = 1;
    } else if (hit_fractal) {
        hit_id = 0;
    }
    
    float3 out_color;
    
    if (hit_fractal || hit_floor) {
        if (hit_fractal) {
            // Compute normal
            norm = compute_normal(hit_pos, dt, fractal_type, max_iter, esc2,
                                 del_less, del_greater, hollow, cut_z,
                                 julia_cx, julia_cy, julia_cz,
                                 mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                 apollonian_scale, menger_scale, sierpinski_scale);
            if (dot3(norm, local_rd) > 0.0f) norm *= -1.0f;
        }
        
        uint seed = (uint)hit_id + (uint)seed_offset * 1337u + (uint)x + (uint)y * 7919u;
        
        // Shadows - use light_radius to control softness
        float shadow_sum = 0.0f;
        int shadow_samples = 8;  // Increased for smoother soft shadows
        
        // Smaller step size for shadows to reduce stepping artifacts
        float shadow_step = dt * 0.3f;
        
        for (int s = 0; s < shadow_samples; s++) {
            // Use stratified sampling for better distribution
            float rand1 = (float)((seed = (seed * 1103515245u + 12345u) >> 16) % 1000) / 1000.0f;
            float rand2 = (float)((seed = (seed * 1103515245u + 12345u) >> 16) % 1000) / 1000.0f;
            float rand3 = (float)((seed = (seed * 1103515245u + 12345u) >> 16) % 1000) / 1000.0f;
            
            // Use light_radius to scale the offset - larger radius = softer shadows
            float3 r_off = (float3)(rand1-0.5f, rand2-0.5f, rand3-0.5f) * 2.0f;
            float3 l_pos = (light_pos + r_off * light_radius) / scale + offset;
            float3 l_dir = normalize3(l_pos - hit_pos);
            float l_dist = length3(l_pos - hit_pos);
            
            float s_t = shadow_step * 2.0f;
            float current_sample = 1.0f;
            
            // More steps for smoother shadows
            for (int j = 0; j < MAX_SHADOW_STEPS && s_t < l_dist; j++) {
                if (is_point_solid(hit_pos + l_dir * s_t, fractal_type, max_iter, esc2,
                                  del_less, del_greater, hollow, cut_z,
                                  julia_cx, julia_cy, julia_cz,
                                  mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                  apollonian_scale, menger_scale, sierpinski_scale)) {
                    current_sample = 0.0f;
                    break;
                }
                s_t += shadow_step;
            }
            shadow_sum += current_sample;
        }
        float shadow = shadow_sum / (float)shadow_samples;
        
        // AO - Screen Space Ambient Occlusion style with falloff
        float ao_sum = 0.0f;
        int ao_samples = 12;
        float ao_range = dt * 40.0f;  // Longer range for better contact shadows
        
        for (int a = 0; a < ao_samples; a++) {
            // Use halton-like sequence for better distribution
            float angle = (float)a * (2.0f * PI / (float)ao_samples);
            float radius = 0.1f + 0.9f * ((float)a / (float)(ao_samples - 1));
            
            float rand1 = cos(angle) * radius;
            float rand2 = sin(angle) * radius;
            float rand3 = sqrt(1.0f - radius * radius);
            
            // Create hemisphere sample
            float3 ao_dir = normalize3(norm + (float3)(rand1, rand2, rand3));
            
            float ao_dist = dt * 2.0f;
            float sample_ao = 1.0f;
            
            // March along AO direction with distance falloff
            for (int step = 0; step < 8 && ao_dist < ao_range; step++) {
                if (is_point_solid(hit_pos + ao_dir * ao_dist, fractal_type, max_iter, esc2,
                                  del_less, del_greater, hollow, cut_z,
                                  julia_cx, julia_cy, julia_cz,
                                  mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                  apollonian_scale, menger_scale, sierpinski_scale)) {
                    // Falloff based on hit distance (closer = darker)
                    sample_ao = (ao_dist / ao_range) * 0.5f;
                    break;
                }
                ao_dist += dt * 5.0f;
            }
            ao_sum += sample_ao;
        }
        float ao = ao_sum / (float)ao_samples;
        
        // Shading - pass all color ramp values individually
        out_color = shade(
            (HitInfo){t, 0, hit_fractal || hit_floor, hit_floor ? 1 : 0, hit_pos, norm},
            rd, light_pos,
            fractal_type, max_iter, esc2,
            julia_cx, julia_cy, julia_cz,
            mandelbulb_power, mandelbox_scale, mandelbox_folding,
            apollonian_scale, menger_scale, sierpinski_scale,
            del_less, del_greater, hollow, cut_z,
            floor_y, floor_enable, checker_size,
            shadow,  // Pass pre-calculated shadow (uses light_radius)
            ao,  // Pass ambient occlusion to shade function
            specular_intensity, shininess, metallic,
            ramp_num_stops,
            ramp_positions[0], ramp_positions[1], ramp_positions[2], ramp_positions[3],
            ramp_positions[4], ramp_positions[5], ramp_positions[6], ramp_positions[7],
            ramp_colors_r[0], ramp_colors_r[1], ramp_colors_r[2], ramp_colors_r[3],
            ramp_colors_r[4], ramp_colors_r[5], ramp_colors_r[6], ramp_colors_r[7],
            ramp_colors_g[0], ramp_colors_g[1], ramp_colors_g[2], ramp_colors_g[3],
            ramp_colors_g[4], ramp_colors_g[5], ramp_colors_g[6], ramp_colors_g[7],
            ramp_colors_b[0], ramp_colors_b[1], ramp_colors_b[2], ramp_colors_b[3],
            ramp_colors_b[4], ramp_colors_b[5], ramp_colors_b[6], ramp_colors_b[7],
            ramp_interp_modes[0], ramp_interp_modes[1], ramp_interp_modes[2], ramp_interp_modes[3],
            ramp_interp_modes[4], ramp_interp_modes[5], ramp_interp_modes[6], ramp_interp_modes[7]
        );
        
        output_depth[idx] = length3(hit_pos - ro) * scale;
        
    } else {
        // Background
        float t_bg = 0.5f * (rd.y + 1.0f);
        out_color = (float3)(1.0f, 1.0f, 1.0f) * (1.0f - t_bg) + (float3)(0.5f, 0.7f, 1.0f) * t_bg;
        output_depth[idx] = 1e10f;
    }
    
    // Write RGB
    output_color[idx * 3 + 0] = out_color.x;
    output_color[idx * 3 + 1] = out_color.y;
    output_color[idx * 3 + 2] = out_color.z;
}
