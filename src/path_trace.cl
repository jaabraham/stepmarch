// Path Tracing Kernel for StepMarch
// High-quality global illumination rendering for final output

#define PI 3.14159265359f
#define EPSILON 0.0005f
#define MAX_DIST 100.0f
#define BISECT_STEPS 10

// Fractal type constants
#define FRACTAL_MANDELBROT_3D 0
#define FRACTAL_JULIA_3D 1
#define FRACTAL_MANDELBULB 2
#define FRACTAL_MANDELBOX 3
#define FRACTAL_APOLLONIAN 4
#define FRACTAL_MENGER 5
#define FRACTAL_SIERPINSKI 6

// Color ramp stops
#define MAX_RAMP_STOPS 8

// Structure for fractal data
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

// Random number generator (PCG)
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
    float3 pos,
    int fractal_type,
    int max_iter,
    float escape,
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
    if (p.z > cut_z) return false;
    
    FractalData fd = get_fractal_data(
        p, fractal_type, max_iter, sqrt(esc2),
        julia_cx, julia_cy, julia_cz,
        mandelbulb_power,
        mandelbox_scale, mandelbox_folding,
        apollonian_scale,
        menger_scale, sierpinski_scale
    );
    
    if (hollow_mode == 1 && fd.iteration >= (float)max_iter - 0.001f) return false;
    if (fd.iteration >= d_less && fd.iteration <= d_greater) return true;
    
    return false;
}

// ============================================
// COLOR RAMP SAMPLING
// ============================================

inline float3 sample_color_ramp(
    float t,
    int num_stops,
    __global float* positions,
    __global float* colors_r,
    __global float* colors_g,
    __global float* colors_b,
    __global int* interp_modes
) {
    if (num_stops <= 0) return (float3)(1.0f, 1.0f, 1.0f);
    if (num_stops == 1) return (float3)(colors_r[0], colors_g[0], colors_b[0]);
    
    t = clamp(t, 0.0f, 1.0f);
    
    // Find segment
    int idx = 0;
    for (int i = 0; i < num_stops - 1; i++) {
        if (t >= positions[i] && t <= positions[i + 1]) {
            idx = i;
            break;
        }
    }
    
    // Constant interpolation
    if (interp_modes[idx] == 1) {
        return (float3)(colors_r[idx], colors_g[idx], colors_b[idx]);
    }
    
    // Linear interpolation
    float seg_range = positions[idx + 1] - positions[idx];
    float local_t = (seg_range > 0.0001f) ? (t - positions[idx]) / seg_range : 0.0f;
    
    float r = colors_r[idx] + (colors_r[idx + 1] - colors_r[idx]) * local_t;
    float g = colors_g[idx] + (colors_g[idx + 1] - colors_g[idx]) * local_t;
    float b = colors_b[idx] + (colors_b[idx + 1] - colors_b[idx]) * local_t;
    
    return (float3)(r, g, b);
}

// ============================================
// PATH TRACING FUNCTIONS
// ============================================

inline float3 sample_cosine_hemisphere(float3 normal, float u, float v) {
    float phi = 2.0f * PI * u;
    float cos_theta = sqrt(1.0f - v);
    float sin_theta = sqrt(v);
    
    float3 tangent, bitangent;
    if (fabs(normal.x) < 0.999f) {
        tangent = normalize3(cross3((float3)(1.0f, 0.0f, 0.0f), normal));
    } else {
        tangent = normalize3(cross3((float3)(0.0f, 1.0f, 0.0f), normal));
    }
    bitangent = cross3(normal, tangent);
    
    return normalize3(
        tangent * cos(phi) * sin_theta +
        bitangent * sin(phi) * sin_theta +
        normal * cos_theta
    );
}

// Compute normal for fractal
inline float3 compute_fractal_normal(
    float3 pos,
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
    
    n.x = is_point_solid(pos + (float3)(h,0,0), fractal_type, max_iter, esc2, d_less, d_greater,
                         hollow_mode, cut_z, julia_cx, julia_cy, julia_cz,
                         mandelbulb_power, mandelbox_scale, mandelbox_folding,
                         apollonian_scale, menger_scale, sierpinski_scale) ? 1.0f : 0.0f;
    n.x -= is_point_solid(pos - (float3)(h,0,0), fractal_type, max_iter, esc2, d_less, d_greater,
                          hollow_mode, cut_z, julia_cx, julia_cy, julia_cz,
                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                          apollonian_scale, menger_scale, sierpinski_scale) ? 1.0f : 0.0f;
    
    n.y = is_point_solid(pos + (float3)(0,h,0), fractal_type, max_iter, esc2, d_less, d_greater,
                         hollow_mode, cut_z, julia_cx, julia_cy, julia_cz,
                         mandelbulb_power, mandelbox_scale, mandelbox_folding,
                         apollonian_scale, menger_scale, sierpinski_scale) ? 1.0f : 0.0f;
    n.y -= is_point_solid(pos - (float3)(0,h,0), fractal_type, max_iter, esc2, d_less, d_greater,
                          hollow_mode, cut_z, julia_cx, julia_cy, julia_cz,
                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                          apollonian_scale, menger_scale, sierpinski_scale) ? 1.0f : 0.0f;
    
    n.z = is_point_solid(pos + (float3)(0,0,h), fractal_type, max_iter, esc2, d_less, d_greater,
                         hollow_mode, cut_z, julia_cx, julia_cy, julia_cz,
                         mandelbulb_power, mandelbox_scale, mandelbox_folding,
                         apollonian_scale, menger_scale, sierpinski_scale) ? 1.0f : 0.0f;
    n.z -= is_point_solid(pos - (float3)(0,0,h), fractal_type, max_iter, esc2, d_less, d_greater,
                          hollow_mode, cut_z, julia_cx, julia_cy, julia_cz,
                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                          apollonian_scale, menger_scale, sierpinski_scale) ? 1.0f : 0.0f;
    
    return normalize3(n);
}

// Floor checkerboard color
inline float3 get_floor_color(float3 pos, float checker_size) {
    float scale = 0.5f / checker_size;
    int check = ((int)floor(pos.x * scale) + (int)floor(pos.z * scale)) & 1;
    if (check) {
        return (float3)(0.2f, 0.2f, 0.2f);
    } else {
        return (float3)(0.8f, 0.8f, 0.8f);
    }
}

// Ray march with floor support
// Returns: 0 = miss, 1 = fractal hit, 2 = floor hit
inline int ray_march(
    float3 ro, float3 rd,
    int fractal_type, int max_iter, float esc2,
    float d_less, float d_greater, int hollow, float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power, float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale, float menger_scale, float sierpinski_scale,
    float floor_y, int floor_enable,
    float dt, float t_max,
    float3* hit_pos, float3* normal, float* hit_material
) {
    float t = 0.0f;
    float3 floor_normal = (float3)(0.0f, 1.0f, 0.0f);
    
    // Calculate floor intersection first
    float t_floor = MAX_DIST;
    if (floor_enable == 1 && fabs(rd.y) > 0.0001f) {
        float tf = (floor_y - ro.y) / rd.y;
        if (tf > 0.001f && tf < t_max) {
            t_floor = tf;
        }
    }
    
    // Ray march fractal
    for (int i = 0; i < 50000 && t < t_max && t < t_floor; i++) {
        float3 p = ro + rd * t;
        
        if (is_point_solid(p, fractal_type, max_iter, esc2, d_less, d_greater,
                          hollow, cut_z, julia_cx, julia_cy, julia_cz,
                          mandelbulb_power, mandelbox_scale, mandelbox_folding,
                          apollonian_scale, menger_scale, sierpinski_scale)) {
            // Refinement
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
            
            *hit_pos = ro + rd * t_high;
            *normal = compute_fractal_normal(*hit_pos, fractal_type, max_iter, esc2,
                                     d_less, d_greater, hollow, cut_z,
                                     julia_cx, julia_cy, julia_cz,
                                     mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                     apollonian_scale, menger_scale, sierpinski_scale);
            if (dot3(*normal, rd) > 0.0f) *normal *= -1.0f;
            *hit_material = 0.0f;  // Fractal
            
            return 1;
        }
        t += dt;
    }
    
    // Check if we hit floor first
    if (floor_enable == 1 && t_floor < t_max && t_floor < t) {
        *hit_pos = ro + rd * t_floor;
        *normal = floor_normal;
        *hit_material = 1.0f;  // Floor
        return 2;
    }
    
    return 0;  // Miss
}

// Path trace a single sample
inline float3 path_trace_sample(
    float3 ro, float3 rd,
    int fractal_type, int max_iter, float escape,
    float d_less, float d_greater, int hollow, float cut_z,
    float julia_cx, float julia_cy, float julia_cz,
    float mandelbulb_power, float mandelbox_scale, float mandelbox_folding,
    float apollonian_scale, float menger_scale, float sierpinski_scale,
    float3 light_pos, float light_radius,
    float floor_y, int floor_enable, float checker_size,
    int ramp_num_stops,
    __global float* ramp_positions,
    __global float* ramp_colors_r,
    __global float* ramp_colors_g,
    __global float* ramp_colors_b,
    __global int* ramp_interp_modes,
    float dt, float t_max,
    float3 bg_color_top, float3 bg_color_bottom,
    int max_bounces,
    uint* seed
) {
    float3 throughput = (float3)(1.0f, 1.0f, 1.0f);
    float3 radiance = (float3)(0.0f, 0.0f, 0.0f);
    float3 ray_origin = ro;
    float3 ray_dir = rd;
    float esc2 = escape * escape;
    
    for (int bounce = 0; bounce < max_bounces; bounce++) {
        float3 hit_pos, normal;
        float hit_material;
        
        int hit = ray_march(ray_origin, ray_dir, fractal_type, max_iter, esc2,
                             d_less, d_greater, hollow, cut_z,
                             julia_cx, julia_cy, julia_cz,
                             mandelbulb_power, mandelbox_scale, mandelbox_folding,
                             apollonian_scale, menger_scale, sierpinski_scale,
                             floor_y, floor_enable,
                             dt, t_max, &hit_pos, &normal, &hit_material);
        
        if (hit == 0) {
            // Miss - add background
            float t = 0.5f * (ray_dir.y + 1.0f);
            float3 bg = mix3(bg_color_bottom, bg_color_top, t);
            radiance += throughput * bg;
            break;
        }
        
        // Get base color
        float3 base_color;
        if (hit_material < 0.5f) {
            // Fractal - use color ramp
            FractalData fd = get_fractal_data(
                hit_pos, fractal_type, max_iter, escape,
                julia_cx, julia_cy, julia_cz,
                mandelbulb_power, mandelbox_scale, mandelbox_folding,
                apollonian_scale, menger_scale, sierpinski_scale
            );
            float t = (fd.iteration - d_less) / (d_greater - d_less);
            t = clamp(t, 0.0f, 1.0f);
            base_color = sample_color_ramp(t, ramp_num_stops, ramp_positions,
                                           ramp_colors_r, ramp_colors_g, ramp_colors_b,
                                           ramp_interp_modes);
        } else {
            // Floor - checkerboard
            base_color = get_floor_color(hit_pos, checker_size);
        }
        
        // Direct lighting (next event estimation)
        float u = random_float(seed);
        float v = random_float(seed);
        float3 light_sample = light_pos + (float3)(
            (u - 0.5f) * light_radius * 2.0f,
            (v - 0.5f) * light_radius * 2.0f,
            (random_float(seed) - 0.5f) * light_radius * 2.0f
        );
        
        float3 light_dir = normalize3(light_sample - hit_pos);
        float light_dist = length3(light_sample - hit_pos);
        
        // Shadow ray
        float3 shadow_hit, shadow_normal;
        float shadow_material;
        bool in_shadow = ray_march(hit_pos + normal * dt * 2.0f, light_dir,
                                   fractal_type, max_iter, esc2,
                                   d_less, d_greater, hollow, cut_z,
                                   julia_cx, julia_cy, julia_cz,
                                   mandelbulb_power, mandelbox_scale, mandelbox_folding,
                                   apollonian_scale, menger_scale, sierpinski_scale,
                                   floor_y, floor_enable,
                                   dt, light_dist, &shadow_hit, &shadow_normal, &shadow_material);
        
        float n_dot_l = fmax(dot3(normal, light_dir), 0.0f);
        if (!in_shadow && n_dot_l > 0.0f) {
            float light_pdf = 1.0f / (light_radius * light_radius * 4.0f);
            float3 emission = (float3)(10.0f, 10.0f, 10.0f);
            radiance += throughput * base_color * emission * n_dot_l / (light_pdf * light_dist * light_dist + 0.001f);
        }
        
        // Indirect - cosine-weighted hemisphere sampling
        u = random_float(seed);
        v = random_float(seed);
        float3 new_dir = sample_cosine_hemisphere(normal, u, v);
        
        throughput *= base_color;
        
        // Russian roulette termination
        float p = fmax(throughput.x, fmax(throughput.y, throughput.z));
        if (random_float(seed) > p) break;
        throughput /= p;
        
        // Setup next bounce
        ray_origin = hit_pos + normal * dt * 2.0f;
        ray_dir = new_dir;
    }
    
    return radiance;
}

// ============================================
// MAIN KERNEL
// ============================================

__kernel void path_trace(
    __global float* output_color,
    __global float* output_albedo,
    __global float* output_normal,
    int width,
    int height,
    int sample_count,
    int samples_per_pixel,
    int max_bounces,
    // Camera
    float3 camera_origin,
    float3 camera_target,
    float3 camera_up,
    float fov,
    // Light
    float3 light_pos,
    float light_radius,
    // Background
    float3 bg_color_top,
    float3 bg_color_bottom,
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
    // Color ramp buffers
    __global float* ramp_positions,
    __global float* ramp_colors_r,
    __global float* ramp_colors_g,
    __global float* ramp_colors_b,
    __global int* ramp_interp_modes,
    int ramp_num_stops
) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    
    // Seed based on pixel position and sample
    uint seed = (uint)(x * 73856093u ^ y * 19349663u ^ sample_count * 83492791u);
    
    // Calculate UV with jitter for antialiasing
    float jitter_x = random_float(&seed);
    float jitter_y = random_float(&seed);
    float u = ((float)x + jitter_x) / (float)width;
    float v = ((float)y + jitter_y) / (float)height;
    
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
    
    float dt = step_size / scale;
    float t_max = dist_max / scale;
    float cut_z = offset.z;
    float floor_y_scaled = floor_y / scale;
    
    // Path trace this sample
    float3 sample_color = path_trace_sample(
        ro, local_rd, fractal_type, max_iter, escape,
        del_less, del_greater, hollow, cut_z,
        julia_cx, julia_cy, julia_cz,
        mandelbulb_power, mandelbox_scale, mandelbox_folding,
        apollonian_scale, menger_scale, sierpinski_scale,
        light_pos, light_radius,
        floor_y_scaled, floor_enable, checker_size,
        ramp_num_stops, ramp_positions, ramp_colors_r, ramp_colors_g, ramp_colors_b, ramp_interp_modes,
        dt, t_max, bg_color_top, bg_color_bottom, max_bounces, &seed
    );
    
    // Progressive accumulation
    if (sample_count == 0) {
        output_color[idx * 3 + 0] = sample_color.x;
        output_color[idx * 3 + 1] = sample_color.y;
        output_color[idx * 3 + 2] = sample_color.z;
    } else {
        float prev_weight = (float)sample_count;
        float new_weight = 1.0f;
        float total = prev_weight + new_weight;
        
        output_color[idx * 3 + 0] = (output_color[idx * 3 + 0] * prev_weight + sample_color.x) / total;
        output_color[idx * 3 + 1] = (output_color[idx * 3 + 1] * prev_weight + sample_color.y) / total;
        output_color[idx * 3 + 2] = (output_color[idx * 3 + 2] * prev_weight + sample_color.z) / total;
    }
}
