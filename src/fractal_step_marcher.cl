// Museum-Quality Fractal Step-Marcher - GPU Version
// Matches Houdini OpenCL implementation exactly

// Math constants
#define PI 3.14159265359f
#define MAX_STEPS 50000
#define MAX_SHADOW_STEPS 20000
#define BISECT_STEPS 10

// Default parameters (based on typical Houdini values)
#define DEFAULT_SCALE 1.0f
#define DEFAULT_MAX_ITER 20
#define DEFAULT_ESCAPE 16.0f
#define DEFAULT_STEP_SIZE 0.005f
#define DEFAULT_DIST_MAX 10.0f
#define DEFAULT_DEL_LESS 0.0f
#define DEFAULT_DEL_GREATER 1000.0f
#define DEFAULT_HOLLOW 0
#define DEFAULT_FLOOR_Y -1.5f
#define DEFAULT_CHECKER_SIZE 1.0f
#define DEFAULT_LIGHT_RADIUS 0.5f

typedef struct {
    float iteration;
    float trap;
} FractalData;

// Random number generator (matching Houdini GPU)
inline float rand_gpu(uint* seed) {
    *seed = (*seed ^ 61) ^ (*seed >> 16);
    *seed *= 9;
    *seed = *seed ^ (*seed >> 4);
    *seed *= 0x27d4eb2d;
    *seed = *seed ^ (*seed >> 15);
    return (float)(*seed) / 4294967296.0f;
}

// 3D Mandelbrot (quaternion-style iteration)
inline FractalData get_fractal_data(float3 c, int max_iter, float escape_sq) {
    FractalData data;
    float3 z = (float3)(0.0f);
    float min_dist = 1e10f;
    
    for (int i = 0; i < max_iter; i++) {
        float r2 = dot(z, z);
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

// Vibrant color palette based on iteration count
// Maps iteration 0-90 to a rainbow gradient, >90 is white
inline float3 iteration_color(float iter) {
    // White for deep interior (high iteration count)
    if (iter > 90.0f) {
        return (float3)(1.0f, 1.0f, 1.0f);
    }
    
    // Normalize to 0-1 range for the gradient (0 to 90)
    float t = iter / 90.0f;
    
    // Highly saturated vibrant multi-color gradient
    float3 colors[8];
    colors[0] = (float3)(0.5f, 0.0f, 0.8f);   // Purple
    colors[1] = (float3)(0.2f, 0.0f, 1.0f);   // Electric blue
    colors[2] = (float3)(0.0f, 0.6f, 1.0f);   // Cyan
    colors[3] = (float3)(0.0f, 0.9f, 0.5f);   // Spring green
    colors[4] = (float3)(0.5f, 1.0f, 0.0f);   // Lime
    colors[5] = (float3)(1.0f, 0.9f, 0.0f);   // Gold
    colors[6] = (float3)(1.0f, 0.4f, 0.0f);   // Orange-red
    colors[7] = (float3)(1.0f, 0.0f, 0.4f);   // Hot pink
    
    float scaled_t = t * 7.0f;  // 0 to 7
    int idx = (int)floor(scaled_t);
    float frac = scaled_t - floor(scaled_t);
    
    if (idx >= 7) return colors[7];
    
    // Smooth interpolation between colors
    return mix(colors[idx], colors[idx + 1], frac);
}

// Check if point is inside fractal
inline bool is_point_solid(float3 p, int max_iter, float esc2, 
                           float d_less, float d_greater, int hollow_mode, float cut_z) {
    // Check clipping plane
    if (p.z > cut_z) return false;
    
    FractalData fd = get_fractal_data(p, max_iter, esc2);
    
    // Solid logic
    if (hollow_mode == 1 && fd.iteration >= (float)max_iter - 0.001f) return false;
    if (fd.iteration >= d_less && fd.iteration <= d_greater) return true;
    
    return false;
}

// Main kernel
__kernel void render(
    __global float* output_color,  // RGB output (width * height * 3)
    __global float* output_depth,  // Depth buffer
    int width,
    int height,
    float3 camera_origin,
    float3 camera_target,
    float3 camera_up,
    float fov,
    float3 light_pos,
    float3 offset,           // Fractal offset
    float scale,             // Fractal scale
    int max_iter,            // Maximum iterations
    float escape,            // Escape radius
    float step_size,         // March step size
    float dist_max,          // Max ray distance
    float del_less,          // Banding start
    float del_greater,       // Banding end
    int hollow,              // Hollow mode
    int seed_offset,         // Random seed offset
    float floor_y,           // Floor plane Y
    int floor_enable,        // Enable floor
    float checker_size,      // Checkerboard scale
    float light_radius       // Soft shadow radius
) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    
    // Calculate ray direction (camera setup)
    float u = (float)x / (float)width;
    float v = (float)y / (float)height;
    
    float aspect = (float)width / (float)height;
    float px = (2.0f * u - 1.0f) * aspect;
    float py = 1.0f - 2.0f * v;
    
    float3 forward = normalize(camera_target - camera_origin);
    float3 right = normalize(cross(forward, camera_up));
    float3 up = cross(right, forward);
    
    float fov_scale = tan(fov * 0.5f * PI / 180.0f);
    float3 rd = normalize(forward + right * px * fov_scale + up * py * fov_scale);
    
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
    
    // 1. FLOOR INTERSECTION
    float t_floor = 1e10f;
    float f_y = floor_y / scale;
    
    if (floor_enable == 1) {
        if (fabs(local_rd.y) > 0.0001f) {
            float tf = (f_y - ro.y) / local_rd.y;
            if (tf > 0.001f) t_floor = tf;
        }
    }
    
    // 2. PRIMARY MARCH
    for (int i = 0; i < MAX_STEPS && t < t_max; i++) {
        if (t > t_floor) break;
        
        float3 p = ro + local_rd * t;
        
        if (is_point_solid(p, max_iter, esc2, del_less, del_greater, hollow, cut_z)) {
            // Refinement (bisection)
            float t_low = t - dt;
            float t_high = t;
            
            for (int b = 0; b < BISECT_STEPS; b++) {
                float t_mid = (t_low + t_high) * 0.5f;
                if (is_point_solid(ro + local_rd * t_mid, max_iter, esc2, del_less, del_greater, hollow, cut_z))
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
    
    // 3. RESOLVE HIT
    int hit_id = -1;
    
    if (!hit_fractal && t_floor < t_max) {
        hit_pos = ro + local_rd * t_floor;
        norm = (float3)(0.0f, 1.0f, 0.0f);
        hit_floor = true;
        hit_id = 1;
    } else if (hit_fractal) {
        hit_id = 0;
    }
    
    // Output color
    float3 out_color;
    
    if (hit_fractal || hit_floor) {
        if (hit_fractal) {
            // Normal calculation (gradient of iteration count)
            float h = dt * 0.2f;
            float3 n;
            n.x = get_fractal_data(hit_pos + (float3)(h,0,0), max_iter, esc2).iteration 
                - get_fractal_data(hit_pos - (float3)(h,0,0), max_iter, esc2).iteration;
            n.y = get_fractal_data(hit_pos + (float3)(0,h,0), max_iter, esc2).iteration 
                - get_fractal_data(hit_pos - (float3)(0,h,0), max_iter, esc2).iteration;
            n.z = get_fractal_data(hit_pos + (float3)(0,0,h), max_iter, esc2).iteration 
                - get_fractal_data(hit_pos - (float3)(0,0,h), max_iter, esc2).iteration;
            
            norm = normalize(n);
            if (dot(norm, local_rd) > 0.0f) norm *= -1.0f;
        }
        
        // Random seed
        uint seed = (uint)hit_id + (uint)seed_offset * 1337u + (uint)get_global_id(0) + (uint)get_global_id(1) * 7919u;
        
        // 4. SHADOWS (multi-sampled)
        float shadow_sum = 0.0f;
        int shadow_samples = 4;
        
        for (int s = 0; s < shadow_samples; s++) {
            float3 r_off = (float3)(rand_gpu(&seed)-0.5f, rand_gpu(&seed)-0.5f, rand_gpu(&seed)-0.5f) * 2.0f;
            float3 l_pos = (light_pos + r_off * light_radius) / scale + offset;
            float3 l_dir = normalize(l_pos - hit_pos);
            float l_dist = length(l_pos - hit_pos);
            
            float s_t = dt * 2.0f;
            float current_sample = 1.0f;
            
            for (int j = 0; j < MAX_SHADOW_STEPS && s_t < l_dist; j++) {
                if (is_point_solid(hit_pos + l_dir * s_t, max_iter, esc2, del_less, del_greater, hollow, cut_z)) {
                    current_sample = 0.0f;
                    break;
                }
                s_t += dt * 0.9f;
            }
            shadow_sum += current_sample;
        }
        float shadow = shadow_sum / (float)shadow_samples;
        
        // 5. AMBIENT OCCLUSION
        float ao_sum = 0.0f;
        int ao_samples = 6;
        float ao_range = dt * 20.0f;
        
        for (int a = 0; a < ao_samples; a++) {
            float3 rand_vec = (float3)(rand_gpu(&seed)-0.5f, rand_gpu(&seed)-0.5f, rand_gpu(&seed)-0.5f) * 2.0f;
            float3 ao_dir = normalize(rand_vec + norm);
            float ao_t = dt * 2.0f;
            float sample_ao = 1.0f;
            
            for (int step = 0; step < 6; step++) {
                if (is_point_solid(hit_pos + ao_dir * ao_t, max_iter, esc2, del_less, del_greater, hollow, cut_z)) {
                    sample_ao = 0.0f;
                    break;
                }
                ao_t += dt * 4.0f;
            }
            ao_sum += sample_ao;
        }
        float ao = ao_sum / (float)ao_samples;
        
        // 6. SHADING
        if (hit_fractal) {
            FractalData fd = get_fractal_data(hit_pos, max_iter, esc2);
            
            // Vibrant iteration-based coloring
            // White for iterations > 90 (deep interior)
            float3 base_color = iteration_color(fd.iteration);
            
            // Add some orbit trap variation for extra detail
            float trap_factor = 1.0f - smoothstep(0.0f, 0.3f, fd.trap);
            base_color = mix(base_color, (float3)(1.0f, 0.9f, 0.5f), trap_factor * 0.3f);
            
            // Apply lighting: ambient + diffuse * shadow
            float3 light_dir = normalize((light_pos / scale + offset) - hit_pos);
            float diff = fmax(dot(norm, light_dir), 0.0f);
            
            // Brighter ambient for more vibrancy
            float ambient = 0.15f;
            float ao_factor = 0.4f + 0.6f * ao;  // AO affects ambient
            
            // Combine lighting (don't darken too much)
            float lighting = ambient * ao_factor + diff * shadow * 0.85f;
            
            // Boost saturation and brightness for more vibrancy
            float3 saturated = base_color * 1.3f;  // Increase brightness
            saturated = clamp(saturated, 0.0f, 1.0f);
            
            // Ensure white interior stays bright
            if (fd.iteration > 90.0f) {
                lighting = fmax(lighting, 0.7f);  // Keep white areas bright
            }
            
            out_color = saturated * lighting;
            
        } else {
            // CHECKERBOARD FLOOR
            float c_size = checker_size > 0.001f ? checker_size : 1.0f;
            int cx = (int)(floor(hit_pos.x / (c_size/scale)));
            int cz = (int)(floor(hit_pos.z / (c_size/scale)));
            bool check = (int)(abs(cx + cz)) % 2 == 0;
            
            float3 base_color = check ? (float3)(0.8f, 0.8f, 0.8f) : (float3)(0.2f, 0.2f, 0.2f);
            
            // Floor lighting
            float3 light_dir = normalize((light_pos / scale + offset) - hit_pos);
            float diff = fmax(dot(norm, light_dir), 0.0f);
            
            float ambient = 0.15f;
            out_color = base_color * (ambient + diff * shadow * 0.85f);
        }
        
        output_depth[idx] = length(hit_pos - ro) * scale;
        
    } else {
        // Background - sky gradient
        out_color = (float3)(0.5f, 0.7f, 1.0f) * (0.5f + 0.5f * rd.y);
        output_depth[idx] = 1e10f;
    }
    
    // Write RGB
    output_color[idx * 3 + 0] = out_color.x;
    output_color[idx * 3 + 1] = out_color.y;
    output_color[idx * 3 + 2] = out_color.z;
}
