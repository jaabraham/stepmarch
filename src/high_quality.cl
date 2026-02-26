// High-Quality Render Kernel for StepMarch
// Features: Supersampling AA, configurable soft shadows, depth of field, ACES tone mapping

#define PI 3.14159265359f
#define EPSILON 0.0005f
#define MAX_DIST 100.0f
#define MAX_SHADOW_STEPS 50000
#define BISECT_STEPS 10
#define ABSOLUTE_MAX_STEPS 100000

// Fractal type constants
#define FRACTAL_MANDELBROT_3D 0
#define FRACTAL_JULIA_3D 1
#define FRACTAL_MANDELBULB 2
#define FRACTAL_MANDELBOX 3
#define FRACTAL_APOLLONIAN 4
#define FRACTAL_MENGER 5
#define FRACTAL_SIERPINSKI 6

#define MAX_RAMP_STOPS 8

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
    return (len > 0.0f) ? (float3)(v.x/len, v.y/len, v.z/len) : v;
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

inline uint pcg_hash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

inline float random_float(uint* seed) {
    *seed = pcg_hash(*seed);
    return (float)(*seed) / 4294967296.0f;
}

// ============================================
// TONE MAPPING
// ============================================

// ACES Filmic tone mapping (approximation)
inline float3 aces_tone_map(float3 color) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    
    float3 result;
    result.x = (color.x * (a * color.x + b)) / (color.x * (c * color.x + d) + e);
    result.y = (color.y * (a * color.y + b)) / (color.y * (c * color.y + d) + e);
    result.z = (color.z * (a * color.z + b)) / (color.z * (c * color.z + d) + e);
    
    return clamp(result, 0.0f, 1.0f);
}

// Gamma correction
inline float3 gamma_correct(float3 color, float gamma) {
    float inv_gamma = 1.0f / gamma;
    return (float3)(pow(color.x, inv_gamma), pow(color.y, inv_gamma), pow(color.z, inv_gamma));
}

// Contrast adjustment (centered at 0.5)
inline float3 adjust_contrast(float3 color, float contrast) {
    // contrast: 0.5 = reduce contrast, 1.0 = neutral, 2.0 = increase contrast
    float3 result;
    result.x = (color.x - 0.5f) * contrast + 0.5f;
    result.y = (color.y - 0.5f) * contrast + 0.5f;
    result.z = (color.z - 0.5f) * contrast + 0.5f;
    return clamp(result, 0.0f, 1.0f);
}

// ============================================
// COLOR RAMP
// ============================================

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
    if (num_stops <= 0) return (float3)(1.0f, 1.0f, 1.0f);
    if (num_stops == 1) return (float3)(colors_r[0], colors_g[0], colors_b[0]);
    
    t = clamp(t, 0.0f, 1.0f);
    
    int idx = 0;
    for (int i = 0; i < num_stops - 1; i++) {
        if (t >= positions[i] && t <= positions[i + 1]) {
            idx = i;
            break;
        }
    }
    
    if (interp_modes[idx] == 1) {
        return (float3)(colors_r[idx], colors_g[idx], colors_b[idx]);
    } else {
        float seg_range = positions[idx + 1] - positions[idx];
        float local_t = (seg_range > 0.0001f) ? (t - positions[idx]) / seg_range : 0.0f;
        
        float r = colors_r[idx] + (colors_r[idx + 1] - colors_r[idx]) * local_t;
        float g = colors_g[idx] + (colors_g[idx + 1] - colors_g[idx]) * local_t;
        float b = colors_b[idx] + (colors_b[idx + 1] - colors_b[idx]) * local_t;
        
        return (float3)(r, g, b);
    }
}

// ============================================
// FRACTAL FUNCTIONS
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
        
        float theta = acos(z.z / r);
        float phi = atan2(z.y, z.x);
        
        float zr = pow(r, power);
        theta = theta * power;
        phi = phi * power;
        
        float sint = sin(theta);
        z = (float3)(
            sint * cos(phi),
            sint * sin(phi),
            cos(theta)
        ) * zr + pos;
        
        dr = pow(r, power - 1.0f) * power * dr + 1.0f;
    }
    
    float dist = 0.5f * log(r) * r / dr;
    data.iteration = (dist < EPSILON) ? (float)max_iter : log(r);
    data.trap = min_dist;
    return data;
}

inline FractalData get_mandelbox(float3 pos, int max_iter, float box_scale, float folding_limit) {
    FractalData data;
    float3 z = pos;
    float3 offset = z;
    float min_dist = 1e10f;
    float dr = 1.0f;
    
    for (int i = 0; i < max_iter; i++) {
        float r = length3(z);
        min_dist = fmin(min_dist, r);
        
        z = clamp(z, -folding_limit, folding_limit) * 2.0f - z;
        
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

inline float menger_sdf(float3 p, int iterations, float menger_scale) {
    float3 z = p;
    float s = 1.0f;
    for (int i = 0; i < iterations; i++) {
        z = clamp(z, -1.0f, 1.0f) * 2.0f - z;
        z *= menger_scale;
        s *= menger_scale;
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

inline float sierpinski_sdf(float3 p, int iterations, float sierpinski_scale) {
    float3 z = p;
    float s = 1.0f;
    float3 a = (float3)(1.0f, 1.0f, 1.0f);
    float3 b = (float3)(-1.0f, -1.0f, 1.0f);
    float3 c = (float3)(-1.0f, 1.0f, -1.0f);
    float3 d = (float3)(1.0f, -1.0f, -1.0f);
    
    for (int i = 0; i < iterations; i++) {
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
        z = (z - vertices[closest]) * sierpinski_scale + vertices[closest];
        s *= sierpinski_scale;
    }
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

inline FractalData get_fractal_data(
    float3 pos, int fractal_type, int max_iter, float escape,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power,
    float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale,
    float menger_scale,
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

inline bool is_point_solid(
    float3 p, int fractal_type, int max_iter, float esc2,
    float d_less, float d_greater, int hollow_mode, float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power, float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale, float menger_scale, float sierpinski_scale
) {
    if (p.z > cut_z) return false;
    
    FractalData fd = get_fractal_data(
        p, fractal_type, max_iter, sqrt(esc2),
        julia_cx, julia_cy, julia_cz,
        mandelbulb_power, mandelbox_scale, mandelbox_folding,
        apollonian_scale, menger_scale, sierpinski_scale
    );
    
    if (hollow_mode == 1 && fd.iteration >= (float)max_iter - 0.001f) return false;
    if (fd.iteration >= d_less && fd.iteration <= d_greater) return true;
    
    return false;
}

// ============================================
// RAY MARCHING
// ============================================

typedef struct {
    float3 pos;
    float3 normal;
    int material;  // 0 = fractal, 1 = floor
    float iteration;
} HitResult;

inline float3 compute_normal(
    float3 pos, float dt,
    int fractal_type, int max_iter, float esc2,
    float d_less, float d_greater, int hollow_mode, float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power, float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale, float menger_scale, float sierpinski_scale
) {
    float h = EPSILON;
    float3 n;
    
    #define GET_SOLID(p) is_point_solid(p, fractal_type, max_iter, esc2, d_less, d_greater, \
        hollow_mode, cut_z, julia_cx, julia_cy, julia_cz, mandelbulb_power, \
        mandelbox_scale, mandelbox_folding, apollonian_scale, menger_scale, sierpinski_scale)
    
    n.x = GET_SOLID(pos + (float3)(h,0,0)) ? 1.0f : 0.0f;
    n.x -= GET_SOLID(pos - (float3)(h,0,0)) ? 1.0f : 0.0f;
    n.y = GET_SOLID(pos + (float3)(0,h,0)) ? 1.0f : 0.0f;
    n.y -= GET_SOLID(pos - (float3)(0,h,0)) ? 1.0f : 0.0f;
    n.z = GET_SOLID(pos + (float3)(0,0,h)) ? 1.0f : 0.0f;
    n.z -= GET_SOLID(pos - (float3)(0,0,h)) ? 1.0f : 0.0f;
    
    #undef GET_SOLID
    
    return normalize3(n);
}

inline int ray_march(
    float3 ro, float3 rd,
    int fractal_type, int max_iter, float esc2,
    float d_less, float d_greater, int hollow, float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power, float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale, float menger_scale, float sierpinski_scale,
    float floor_y, int floor_enable,
    float dt, float t_max,
    HitResult* hit
) {
    float t = 0.0f;
    
    // Floor intersection
    float t_floor = MAX_DIST;
    if (floor_enable == 1 && fabs(rd.y) > 0.0001f) {
        float tf = (floor_y - ro.y) / rd.y;
        if (tf > 0.001f) t_floor = tf;
    }
    
    // Ray march fractal
    for (int i = 0; i < ABSOLUTE_MAX_STEPS && t < t_max && t < t_floor; i++) {
        float3 p = ro + rd * t;
        
        if (is_point_solid(p, fractal_type, max_iter, esc2, d_less, d_greater,
                          hollow, cut_z, julia_cx, julia_cy, julia_cz,
                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                          apollonian_scale, menger_scale, sierpinski_scale)) {
            // Binary refinement
            float t_low = t - dt;
            float t_high = t;
            
            for (int b = 0; b < BISECT_STEPS; b++) {
                float t_mid = (t_low + t_high) * 0.5f;
                float3 p_mid = ro + rd * t_mid;
                if (is_point_solid(p_mid, fractal_type, max_iter, esc2, d_less, d_greater,
                                  hollow, cut_z, julia_cx, julia_cy, julia_cz,
                                  mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                  apollonian_scale, menger_scale, sierpinski_scale))
                    t_high = t_mid;
                else
                    t_low = t_mid;
            }
            
            hit->pos = ro + rd * t_high;
            hit->normal = compute_normal(hit->pos, dt, fractal_type, max_iter, esc2,
                                          d_less, d_greater, hollow, cut_z,
                                          julia_cx, julia_cy, julia_cz,
                                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                          apollonian_scale, menger_scale, sierpinski_scale);
            if (dot3(hit->normal, rd) > 0.0f) hit->normal *= -1.0f;
            hit->material = 0;
            
            FractalData fd = get_fractal_data(hit->pos, fractal_type, max_iter, sqrt(esc2),
                julia_cx, julia_cy, julia_cz, mandelbulb_power, mandelbox_scale, mandelbox_folding,
                apollonian_scale, menger_scale, sierpinski_scale);
            hit->iteration = fd.iteration;
            
            return 1;  // Hit fractal
        }
        t += dt;
    }
    
    // Check floor
    if (floor_enable == 1 && t_floor < t_max && t_floor <= t) {
        hit->pos = ro + rd * t_floor;
        hit->normal = (float3)(0.0f, 1.0f, 0.0f);
        hit->material = 1;
        hit->iteration = 0.0f;
        return 2;  // Hit floor
    }
    
    return 0;  // Miss
}

// ============================================
// SHADING
// ============================================

inline float3 checkerboard(float3 pos, float checker_size) {
    float scale = 0.5f / checker_size;
    int check = ((int)floor(pos.x * scale) + (int)floor(pos.z * scale)) & 1;
    return check ? (float3)(0.2f, 0.2f, 0.2f) : (float3)(0.8f, 0.8f, 0.8f);
}

inline float calc_soft_shadow(
    float3 ro, float3 rd, float mint, float maxt, float k,
    int fractal_type, int max_iter, float esc2,
    float d_less, float d_greater, int hollow, float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power, float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale, float menger_scale, float sierpinski_scale,
    float dt, uint* seed, int shadow_samples
) {
    if (shadow_samples <= 1) {
        // Hard shadow
        float t = mint;
        for (int j = 0; j < MAX_SHADOW_STEPS && t < maxt; j++) {
            if (is_point_solid(ro + rd * t, fractal_type, max_iter, esc2, d_less, d_greater,
                              hollow, cut_z, julia_cx, julia_cy, julia_cz,
                              mandelbulb_power, mandelbox_scale, mandelbox_folding,
                              apollonian_scale, menger_scale, sierpinski_scale)) {
                return 0.0f;
            }
            t += dt * 0.5f;
        }
        return 1.0f;
    }
    
    // Soft shadows with multiple samples
    float shadow_sum = 0.0f;
    float shadow_step = dt * 0.3f;
    
    for (int s = 0; s < shadow_samples; s++) {
        float rand1 = random_float(seed);
        float rand2 = random_float(seed);
        float rand3 = random_float(seed);
        
        float3 offset = (float3)(rand1-0.5f, rand2-0.5f, rand3-0.5f) * 2.0f * k;
        float3 sample_dir = normalize3(rd + offset * 0.1f);
        
        float t = shadow_step * 2.0f;
        float sample_shadow = 1.0f;
        
        for (int j = 0; j < MAX_SHADOW_STEPS && t < maxt; j++) {
            if (is_point_solid(ro + sample_dir * t, fractal_type, max_iter, esc2, d_less, d_greater,
                              hollow, cut_z, julia_cx, julia_cy, julia_cz,
                              mandelbulb_power, mandelbox_scale, mandelbox_folding,
                              apollonian_scale, menger_scale, sierpinski_scale)) {
                sample_shadow = 0.0f;
                break;
            }
            t += shadow_step;
        }
        shadow_sum += sample_shadow;
    }
    
    return shadow_sum / (float)shadow_samples;
}

inline float calc_ao(
    float3 pos, float3 normal, float dt,
    int fractal_type, int max_iter, float esc2,
    float d_less, float d_greater, int hollow, float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power, float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale, float menger_scale, float sierpinski_scale,
    int ao_samples, uint* seed
) {
    float ao_sum = 0.0f;
    float ao_range = dt * 40.0f;
    
    for (int a = 0; a < ao_samples; a++) {
        float angle = (float)a * (2.0f * PI / (float)ao_samples);
        float radius = 0.1f + 0.9f * ((float)a / (float)(ao_samples - 1));
        
        float rand1 = cos(angle) * radius;
        float rand2 = sin(angle) * radius;
        float rand3 = sqrt(1.0f - radius * radius);
        
        float3 ao_dir = normalize3(normal + (float3)(rand1, rand2, rand3));
        
        float ao_dist = dt * 2.0f;
        float sample_ao = 1.0f;
        
        for (int step = 0; step < 8 && ao_dist < ao_range; step++) {
            if (is_point_solid(pos + ao_dir * ao_dist, fractal_type, max_iter, esc2,
                              d_less, d_greater, hollow, cut_z,
                              julia_cx, julia_cy, julia_cz,
                              mandelbulb_power, mandelbox_scale, mandelbox_folding,
                              apollonian_scale, menger_scale, sierpinski_scale)) {
                sample_ao = (ao_dist / ao_range) * 0.5f;
                break;
            }
            ao_dist += dt * 5.0f;
        }
        ao_sum += sample_ao;
    }
    
    return ao_sum / (float)ao_samples;
}

// ============================================
// CAMERA
// ============================================

inline float3 calculate_ray_direction(
    float u, float v, float aspect, float fov,
    float3 forward, float3 right, float3 up
) {
    float px = (2.0f * u - 1.0f) * aspect;
    float py = 1.0f - 2.0f * v;
    float fov_scale = tan(fov * 0.5f * PI / 180.0f);
    return normalize3(forward + right * px * fov_scale + up * py * fov_scale);
}

// Bokeh disk sampling for depth of field
inline float2 sample_bokeh_disk(float u, float v, int shape) {
    float r = sqrt(u);
    float theta = 2.0f * PI * v;
    
    if (shape == 1) {  // Hexagon
        // Simplified hexagon approximation
        float angle = theta;
        r = r * (1.0f - 0.1f * cos(6.0f * angle));
    }
    
    return (float2)(r * cos(theta), r * sin(theta));
}

// ============================================
// HIGH QUALITY RENDER KERNEL
// ============================================

__kernel void render_high_quality(
    __global float* output_color,
    int width,
    int height,
    // Quality settings
    int aa_samples,        // 1, 4, 9, 16 (1x, 2x, 3x, 4x supersampling)
    int shadow_samples,    // 1, 8, 16, 32, 64
    int ao_samples,        // 0, 8, 16, 32
    int enable_dof,        // 0 or 1
    float aperture_size,   // 0.0 - 1.0
    float focal_distance,  // Focus plane distance
    // Tone mapping
    float exposure,
    float contrast,        // 0.5 - 2.0 (1.0 = neutral)
    int enable_tonemap,    // 0 or 1
    // Camera
    float3 camera_origin,
    float3 camera_target,
    float3 camera_up,
    float fov,
    // Light
    float3 light_pos,
    float light_radius,
    // Fractal
    int fractal_type,
    float3 offset,
    float scale,
    int max_iter,
    float escape,
    float step_size,
    float dist_max,
    float del_less,
    float del_greater,
    int hollow,
    // Julia
    float julia_cx, float julia_cy, float julia_cz,
    // Mandelbulb
    float mandelbulb_power,
    // Mandelbox
    float mandelbox_scale, float mandelbox_folding,
    // Apollonian
    float apollonian_scale,
    // Menger
    float menger_scale,
    // Sierpinski
    float sierpinski_scale,
    // Floor
    float floor_y,
    int floor_enable,
    float checker_size,
    // Material
    float specular_intensity,
    float shininess,
    int metallic,
    // Color ramp
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
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    
    // Seed for random numbers
    uint seed = (uint)(x * 73856093u ^ y * 19349663u);
    
    // Build color ramp arrays
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
    
    // Camera basis vectors
    float3 forward = normalize3(camera_target - camera_origin);
    float3 right = normalize3(cross3(forward, camera_up));
    float3 up = cross3(right, forward);
    
    float aspect = (float)width / (float)height;
    float esc2 = escape * escape;
    float dt = step_size / scale;
    float t_max = dist_max / scale;
    float cut_z = offset.z;
    float floor_y_scaled = floor_y / scale;
    float3 ro = (camera_origin / scale) + offset;
    
    // Supersampling accumulation
    float3 accumulated_color = (float3)(0.0f, 0.0f, 0.0f);
    int samples_taken = 0;
    
    // Calculate grid size for stratified sampling
    int grid_size = (int)sqrt((float)aa_samples);
    if (grid_size * grid_size < aa_samples) grid_size++;
    
    for (int sy = 0; sy < grid_size; sy++) {
        for (int sx = 0; sx < grid_size; sx++) {
            if (samples_taken >= aa_samples) break;
            
            // Stratified sampling within pixel
            float jitter_x = random_float(&seed);
            float jitter_y = random_float(&seed);
            
            float u = ((float)x + ((float)sx + jitter_x) / (float)grid_size) / (float)width;
            float v = ((float)y + ((float)sy + jitter_y) / (float)grid_size) / (float)height;
            
            // Primary ray direction
            float3 rd = calculate_ray_direction(u, v, aspect, fov, forward, right, up);
            
            // Depth of field
            if (enable_dof && aperture_size > 0.0f) {
                // Find focal point
                float3 focal_point = camera_origin + rd * focal_distance;
                
                // Sample lens position
                float2 lens_uv = sample_bokeh_disk(random_float(&seed), random_float(&seed), 0);
                float3 lens_offset = right * lens_uv.x * aperture_size + up * lens_uv.y * aperture_size;
                
                camera_origin += lens_offset;
                rd = normalize3(focal_point - camera_origin);
                ro = (camera_origin / scale) + offset;
            }
            
            // Ray march
            HitResult hit;
            int hit_type = ray_march(ro, rd, fractal_type, max_iter, esc2,
                                     del_less, del_greater, hollow, cut_z,
                                     julia_cx, julia_cy, julia_cz,
                                     mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                     apollonian_scale, menger_scale, sierpinski_scale,
                                     floor_y_scaled, floor_enable,
                                     dt, t_max, &hit);
            
            float3 sample_color;
            
            if (hit_type == 0) {
                // Background
                float t_bg = 0.5f * (rd.y + 1.0f);
                sample_color = mix3((float3)(1.0f, 1.0f, 1.0f), (float3)(0.5f, 0.7f, 1.0f), t_bg);
            } else {
                // Get base color
                float3 base_color;
                if (hit.material == 0) {
                    // Fractal
                    float t = (hit.iteration - del_less) / (del_greater - del_less);
                    t = clamp(t, 0.0f, 1.0f);
                    base_color = sample_color_ramp(t, ramp_num_stops, ramp_positions,
                                                   ramp_colors_r, ramp_colors_g, ramp_colors_b,
                                                   ramp_interp_modes);
                } else {
                    // Floor
                    base_color = checkerboard(hit.pos, checker_size);
                }
                
                // Light direction
                float3 light_dir = normalize3(light_pos - hit.pos);
                float3 view_dir = -rd;
                
                // Soft shadows
                float light_dist = length3(light_pos - hit.pos);
                float shadow = calc_soft_shadow(
                    hit.pos + hit.normal * dt * 2.0f, light_dir, dt * 2.0f, light_dist, light_radius,
                    fractal_type, max_iter, esc2, del_less, del_greater, hollow, cut_z,
                    julia_cx, julia_cy, julia_cz, mandelbulb_power, mandelbox_scale, mandelbox_folding,
                    apollonian_scale, menger_scale, sierpinski_scale, dt, &seed, shadow_samples
                );
                
                // Ambient occlusion
                float ao = 1.0f;
                if (ao_samples > 0) {
                    ao = calc_ao(hit.pos, hit.normal, dt, fractal_type, max_iter, esc2,
                                 del_less, del_greater, hollow, cut_z,
                                 julia_cx, julia_cy, julia_cz, mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                 apollonian_scale, menger_scale, sierpinski_scale, ao_samples, &seed);
                }
                
                // Diffuse
                float diff = fmax(dot3(hit.normal, light_dir), 0.0f);
                
                // Specular
                float safe_shininess = clamp(shininess, 1.0f, 128.0f);
                float3 half_dir = normalize3(light_dir + view_dir);
                float spec = pow(fmax(dot3(hit.normal, half_dir), 0.0f), safe_shininess);
                
                // Combine
                float ambient = 0.15f * (0.3f + 0.7f * ao);
                float3 spec_color = (metallic == 1) ? base_color : (float3)(1.0f, 1.0f, 1.0f);
                
                sample_color = base_color * (ambient + diff * shadow * 0.8f) +
                               spec_color * spec * shadow * specular_intensity;
                
                // Exposure
                sample_color *= exposure;
            }
            
            accumulated_color += sample_color;
            samples_taken++;
        }
    }
    
    // Average samples
    float3 final_color = accumulated_color / (float)samples_taken;
    
    // Contrast adjustment (before tone mapping for better control)
    final_color = adjust_contrast(final_color, contrast);
    
    // Tone mapping
    if (enable_tonemap) {
        final_color = aces_tone_map(final_color);
    }
    
    // Gamma correction
    final_color = gamma_correct(final_color, 2.2f);
    
    // Clamp and store
    final_color = clamp(final_color, 0.0f, 1.0f);
    output_color[idx * 3 + 0] = final_color.x;
    output_color[idx * 3 + 1] = final_color.y;
    output_color[idx * 3 + 2] = final_color.z;
}
