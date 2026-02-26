// StepMarch Animation System
// Keyframe-based animation with bezier interpolation

#ifndef ANIMATION_H
#define ANIMATION_H

#include <vector>
#include <string>
#include <math.h>
#include <algorithm>

// Include fractal types
#include "fractal_types.h"

// Color ramp interpolation modes
#define RAMP_INTERP_LINEAR 0
#define RAMP_INTERP_CONSTANT 1

// Color ramp stop - position (0-1) and RGB color
#define MAX_RAMP_STOPS 8
struct ColorRamp {
    int num_stops;
    float positions[MAX_RAMP_STOPS];  // 0.0 to 1.0 (mapped to del_less/del_greater)
    float colors[MAX_RAMP_STOPS][3];  // RGB values
    int interp_modes[MAX_RAMP_STOPS]; // Interpolation mode for each segment (0=linear, 1=constant)
    char name[64];                    // Name for saved presets
    
    ColorRamp() {
        num_stops = 0;
        name[0] = '\0';
        for (int i = 0; i < MAX_RAMP_STOPS; i++) {
            interp_modes[i] = RAMP_INTERP_LINEAR;
        }
    }
    
    // Initialize with default rainbow gradient
    void init_default() {
        num_stops = 8;
        // Positions spread across 0-1
        positions[0] = 0.0f;   colors[0][0] = 0.5f; colors[0][1] = 0.0f; colors[0][2] = 0.8f;   // Purple
        positions[1] = 0.14f;  colors[1][0] = 0.2f; colors[1][1] = 0.0f; colors[1][2] = 1.0f;   // Electric blue
        positions[2] = 0.28f;  colors[2][0] = 0.0f; colors[2][1] = 0.6f; colors[2][2] = 1.0f;   // Cyan
        positions[3] = 0.42f;  colors[3][0] = 0.0f; colors[3][1] = 0.9f; colors[3][2] = 0.5f;   // Spring green
        positions[4] = 0.57f;  colors[4][0] = 0.5f; colors[4][1] = 1.0f; colors[4][2] = 0.0f;   // Lime
        positions[5] = 0.71f;  colors[5][0] = 1.0f; colors[5][1] = 0.9f; colors[5][2] = 0.0f;   // Gold
        positions[6] = 0.85f;  colors[6][0] = 1.0f; colors[6][1] = 0.4f; colors[6][2] = 0.0f;   // Orange-red
        positions[7] = 1.0f;   colors[7][0] = 1.0f; colors[7][1] = 0.0f; colors[7][2] = 0.4f;   // Hot pink
    }
    
    // Sample color at position t (0-1)
    void sample(float t, float* out_r, float* out_g, float* out_b) const {
        if (num_stops == 0) {
            *out_r = *out_g = *out_b = 1.0f;
            return;
        }
        if (num_stops == 1) {
            *out_r = colors[0][0]; *out_g = colors[0][1]; *out_b = colors[0][2];
            return;
        }
        
        // Clamp t
        t = fmaxf(0.0f, fminf(1.0f, t));
        
        // Find which segment we're in
        int idx = 0;
        for (int i = 0; i < num_stops - 1; i++) {
            if (t >= positions[i] && t <= positions[i + 1]) {
                idx = i;
                break;
            }
        }
        
        // Check interpolation mode for this segment
        if (interp_modes[idx] == RAMP_INTERP_CONSTANT) {
            // Constant interpolation - use left color
            *out_r = colors[idx][0];
            *out_g = colors[idx][1];
            *out_b = colors[idx][2];
        } else {
            // Linear interpolation
            float seg_range = positions[idx + 1] - positions[idx];
            float local_t = (seg_range > 0.0001f) ? (t - positions[idx]) / seg_range : 0.0f;
            
            *out_r = colors[idx][0] + (colors[idx + 1][0] - colors[idx][0]) * local_t;
            *out_g = colors[idx][1] + (colors[idx + 1][1] - colors[idx][1]) * local_t;
            *out_b = colors[idx][2] + (colors[idx + 1][2] - colors[idx][2]) * local_t;
        }
    }
    
    // Add a stop (returns index, or -1 if full)
    int add_stop(float pos, float r, float g, float b) {
        if (num_stops >= MAX_RAMP_STOPS) return -1;
        
        // Clamp position
        pos = fmaxf(0.0f, fminf(1.0f, pos));
        
        // Find insertion point to keep sorted
        int insert_idx = num_stops;
        for (int i = 0; i < num_stops; i++) {
            if (pos < positions[i]) {
                insert_idx = i;
                break;
            }
        }
        
        // Shift existing stops
        for (int i = num_stops; i > insert_idx; i--) {
            positions[i] = positions[i - 1];
            colors[i][0] = colors[i - 1][0];
            colors[i][1] = colors[i - 1][1];
            colors[i][2] = colors[i - 1][2];
        }
        
        // Insert new stop
        positions[insert_idx] = pos;
        colors[insert_idx][0] = r;
        colors[insert_idx][1] = g;
        colors[insert_idx][2] = b;
        num_stops++;
        
        return insert_idx;
    }
    
    // Remove a stop at index
    void remove_stop(int idx) {
        if (idx < 0 || idx >= num_stops) return;
        for (int i = idx; i < num_stops - 1; i++) {
            positions[i] = positions[i + 1];
            colors[i][0] = colors[i + 1][0];
            colors[i][1] = colors[i + 1][1];
            colors[i][2] = colors[i + 1][2];
        }
        num_stops--;
    }
    
    // Find stop index near a position (within tolerance)
    int find_stop_near(float pos, float tolerance) const {
        for (int i = 0; i < num_stops; i++) {
            if (fabsf(positions[i] - pos) < tolerance) {
                return i;
            }
        }
        return -1;
    }
    
    // Save color ramp to file
    bool save_to_file(const char* filename) const {
        FILE* f = fopen(filename, "w");
        if (!f) return false;
        
        fprintf(f, "# Color Ramp Preset\n");
        fprintf(f, "NAME %s\n", name);
        fprintf(f, "NUM_STOPS %d\n", num_stops);
        for (int i = 0; i < num_stops; i++) {
            fprintf(f, "STOP %.6f %.6f %.6f %.6f %d\n", 
                    positions[i], colors[i][0], colors[i][1], colors[i][2], interp_modes[i]);
        }
        fclose(f);
        return true;
    }
    
    // Load color ramp from file
    bool load_from_file(const char* filename) {
        FILE* f = fopen(filename, "r");
        if (!f) return false;
        
        char line[256];
        int expected_stops = 0;
        int loaded_stops = 0;
        name[0] = '\0';
        
        // Clear current ramp
        num_stops = 0;
        for (int i = 0; i < MAX_RAMP_STOPS; i++) {
            positions[i] = 0.0f;
            colors[i][0] = colors[i][1] = colors[i][2] = 0.0f;
            interp_modes[i] = RAMP_INTERP_LINEAR;
        }
        
        while (fgets(line, sizeof(line), f)) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            
            if (line[0] == '#' || line[0] == '\0') continue;
            
            if (strncmp(line, "NAME ", 5) == 0) {
                strncpy(name, line + 5, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
            } else if (strncmp(line, "NUM_STOPS ", 10) == 0) {
                sscanf(line + 10, "%d", &expected_stops);
            } else if (strncmp(line, "STOP ", 5) == 0 && loaded_stops < MAX_RAMP_STOPS) {
                int mode = RAMP_INTERP_LINEAR;
                int n = sscanf(line + 5, "%f %f %f %f %d", 
                       &positions[loaded_stops], 
                       &colors[loaded_stops][0], 
                       &colors[loaded_stops][1], 
                       &colors[loaded_stops][2], 
                       &mode);
                if (n >= 4) {
                    interp_modes[loaded_stops] = mode;
                    loaded_stops++;
                }
            }
        }
        
        fclose(f);
        
        // Validate
        num_stops = loaded_stops;
        if (num_stops > MAX_RAMP_STOPS) num_stops = MAX_RAMP_STOPS;
        if (num_stops < 2) {
            // Invalid - use default
            init_default();
            return false;
        }
        
        return true;
    }
};

// Parameter structure (defined here for animation system)
typedef struct {
    // Camera
    float cx, cy, cz;
    float tx, ty, tz;
    float fov;
    // Light
    float lx, ly, lz;
    float lradius;
    // Base fractal
    float offsetx, offsety, offsetz;
    float scale;
    int max_iter;
    float escape;
    float step_size;
    float dist_max;
    float del_less;
    float del_greater;
    int hollow;
    int seed_offset;
    int max_steps;  // Runtime step limit for ray marching
    // Floor
    float floor_y;
    int floor_enable;
    float checker_size;
    // Fractal type and specific params
    int fractal_type;
    // Julia
    float julia_cx, julia_cy, julia_cz;
    // Mandelbulb
    float mandelbulb_power;
    // Mandelbox
    float mandelbox_scale;
    float mandelbox_folding;
    // Apollonian
    float apollonian_scale;
    // Menger
    float menger_scale;
    // Sierpinski
    float sierpinski_scale;
    // Color ramp for iteration-based coloring
    ColorRamp color_ramp;
    // Material properties
    float specular_intensity;  // 0.0 to 2.0
    float shininess;           // 1.0 to 128.0
    int metallic;              // 0 = dielectric, 1 = metallic
} Params;

// Single keyframe - stores all parameters at a specific frame
struct Keyframe {
    int frame;
    Params params;
    
    // Bezier handles for smooth interpolation
    float tangent_in[30];   // Sized for all params
    float tangent_out[30];  // Sized for all params
};

// Animation project
struct AnimationProject {
    std::string name;
    int start_frame = 1;
    int end_frame = 250;
    int current_frame = 1;
    std::vector<Keyframe> keyframes;
    
    // Resolution for animation renders
    int render_width = 1920;
    int render_height = 1080;
    std::string output_pattern = "output/frame_%04d.png";
};

// Default params helper
inline Params default_params() {
    Params p = {};
    // Camera
    p.cx = 3.0f; p.cy = 2.0f; p.cz = 4.0f;
    p.tx = 0.0f; p.ty = 0.0f; p.tz = 0.0f;
    p.fov = 60.0f;
    // Light
    p.lx = 5.0f; p.ly = 8.0f; p.lz = 5.0f;
    p.lradius = 0.5f;
    // Base fractal
    p.offsetx = 0.0f; p.offsety = 0.0f; p.offsetz = 0.0f;
    p.scale = 1.0f;
    p.max_iter = 50;
    p.escape = 16.0f;
    p.step_size = 0.005f;
    p.dist_max = 10.0f;
    p.del_less = 6.0f;
    p.del_greater = 100.0f;
    p.hollow = 0;
    p.seed_offset = 0;
    p.max_steps = 50000;  // Default to high quality
    // Floor
    p.floor_y = -2.0f;
    p.floor_enable = 1;
    p.checker_size = 1.0f;
    // Fractal type (3D Mandelbrot default)
    p.fractal_type = FRACTAL_MANDELBROT_3D;
    // Julia defaults
    p.julia_cx = -0.8f; p.julia_cy = 0.0f; p.julia_cz = 0.0f;
    // Mandelbulb defaults
    p.mandelbulb_power = 8.0f;
    // Mandelbox defaults
    p.mandelbox_scale = 2.0f;
    p.mandelbox_folding = 1.0f;
    // Apollonian defaults
    p.apollonian_scale = 3.0f;
    // Menger defaults
    p.menger_scale = 3.0f;
    // Sierpinski defaults
    p.sierpinski_scale = 2.0f;
    // Color ramp default
    p.color_ramp.init_default();
    // Material defaults
    p.specular_intensity = 0.5f;
    p.shininess = 32.0f;
    p.metallic = 0;
    return p;
}

// Get parameter as float by index
inline float get_param_value(const Params& p, int idx) {
    switch (idx) {
        case 0: return p.cx;
        case 1: return p.cy;
        case 2: return p.cz;
        case 3: return p.tx;
        case 4: return p.ty;
        case 5: return p.tz;
        case 6: return p.fov;
        case 7: return p.lx;
        case 8: return p.ly;
        case 9: return p.lz;
        case 10: return p.lradius;
        case 11: return p.offsetx;
        case 12: return p.offsety;
        case 13: return p.offsetz;
        case 14: return p.scale;
        case 15: return (float)p.max_iter;
        case 16: return p.escape;
        case 17: return p.step_size;
        case 18: return p.dist_max;
        case 19: return p.del_less;
        case 20: return p.del_greater;
        case 21: return (float)p.hollow;
        case 22: return (float)p.seed_offset;
        case 23: return p.floor_y;
        case 24: return (float)p.floor_enable;
        case 25: return p.checker_size;
        case 26: return (float)p.fractal_type;
        case 27: return p.julia_cx;
        case 28: return p.julia_cy;
        case 29: return p.julia_cz;
        case 30: return p.mandelbulb_power;
        case 31: return p.mandelbox_scale;
        case 32: return p.mandelbox_folding;
        case 33: return p.apollonian_scale;
        case 34: return p.menger_scale;
        case 35: return p.sierpinski_scale;
        case 36: return p.specular_intensity;
        case 37: return p.shininess;
        case 38: return (float)p.metallic;
        default: return 0.0f;
    }
}

// Set parameter from float by index
inline void set_param_value(Params& p, int idx, float val) {
    switch (idx) {
        case 0: p.cx = val; break;
        case 1: p.cy = val; break;
        case 2: p.cz = val; break;
        case 3: p.tx = val; break;
        case 4: p.ty = val; break;
        case 5: p.tz = val; break;
        case 6: p.fov = val; break;
        case 7: p.lx = val; break;
        case 8: p.ly = val; break;
        case 9: p.lz = val; break;
        case 10: p.lradius = val; break;
        case 11: p.offsetx = val; break;
        case 12: p.offsety = val; break;
        case 13: p.offsetz = val; break;
        case 14: p.scale = val; break;
        case 15: p.max_iter = (int)val; break;
        case 16: p.escape = val; break;
        case 17: p.step_size = val; break;
        case 18: p.dist_max = val; break;
        case 19: p.del_less = val; break;
        case 20: p.del_greater = val; break;
        case 21: p.hollow = (int)val; break;
        case 22: p.seed_offset = (int)val; break;
        case 23: p.floor_y = val; break;
        case 24: p.floor_enable = (int)val; break;
        case 25: p.checker_size = val; break;
        case 26: p.fractal_type = (int)val; break;
        case 27: p.julia_cx = val; break;
        case 28: p.julia_cy = val; break;
        case 29: p.julia_cz = val; break;
        case 30: p.mandelbulb_power = val; break;
        case 31: p.mandelbox_scale = val; break;
        case 32: p.mandelbox_folding = val; break;
        case 33: p.apollonian_scale = val; break;
        case 34: p.menger_scale = val; break;
        case 35: p.sierpinski_scale = val; break;
        case 36: p.specular_intensity = val; break;
        case 37: p.shininess = val; break;
        case 38: p.metallic = (int)val; break;
    }
}

#define PARAM_COUNT 39

// Smooth step interpolation (ease in/out)
inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Find keyframe index at or before given frame
inline int find_keyframe_index(const std::vector<Keyframe>& keyframes, int frame) {
    int result = -1;
    for (int i = 0; i < (int)keyframes.size(); i++) {
        if (keyframes[i].frame <= frame) {
            result = i;
        } else {
            break;
        }
    }
    return result;
}

// Interpolate parameters at a given frame
inline Params interpolate_params(const AnimationProject& proj, int frame) {
    int k1_idx = find_keyframe_index(proj.keyframes, frame);
    
    // Before first keyframe
    if (k1_idx < 0) {
        return proj.keyframes.empty() ? default_params() : proj.keyframes[0].params;
    }
    
    // At or after last keyframe
    if (k1_idx >= (int)proj.keyframes.size() - 1) {
        return proj.keyframes.back().params;
    }
    
    const Keyframe& k1 = proj.keyframes[k1_idx];
    const Keyframe& k2 = proj.keyframes[k1_idx + 1];
    
    // Calculate interpolation factor
    float t = 0.0f;
    if (k2.frame > k1.frame) {
        t = (float)(frame - k1.frame) / (float)(k2.frame - k1.frame);
    }
    
    // Apply smoothstep for ease in/out
    t = smoothstep(t);
    
    // Interpolate all parameters
    Params result;
    for (int i = 0; i < PARAM_COUNT; i++) {
        float v1 = get_param_value(k1.params, i);
        float v2 = get_param_value(k2.params, i);
        float val = v1 + (v2 - v1) * t;
        set_param_value(result, i, val);
    }
    
    return result;
}

// Add or update keyframe
inline void set_keyframe(AnimationProject& proj, int frame, const Params& params) {
    // Check if exists
    for (auto& kf : proj.keyframes) {
        if (kf.frame == frame) {
            kf.params = params;
            return;
        }
    }
    
    // Insert new keyframe
    Keyframe kf;
    kf.frame = frame;
    kf.params = params;
    for (int i = 0; i < 30; i++) {
        kf.tangent_in[i] = 0.0f;
        kf.tangent_out[i] = 0.0f;
    }
    
    // Insert in sorted order
    auto it = proj.keyframes.begin();
    while (it != proj.keyframes.end() && it->frame < frame) {
        ++it;
    }
    proj.keyframes.insert(it, kf);
}

// Delete keyframe
inline void delete_keyframe(AnimationProject& proj, int frame) {
    for (auto it = proj.keyframes.begin(); it != proj.keyframes.end(); ++it) {
        if (it->frame == frame) {
            proj.keyframes.erase(it);
            return;
        }
    }
}

// Check if frame has keyframe
inline bool has_keyframe(const AnimationProject& proj, int frame) {
    for (const auto& kf : proj.keyframes) {
        if (kf.frame == frame) return true;
    }
    return false;
}

// Save project to file
inline bool save_project(const AnimationProject& proj, const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) return false;
    
    fprintf(f, "# StepMarch Animation Project\n");
    fprintf(f, "NAME %s\n", proj.name.c_str());
    fprintf(f, "FRAME_RANGE %d %d\n", proj.start_frame, proj.end_frame);
    fprintf(f, "RENDER_SIZE %d %d\n", proj.render_width, proj.render_height);
    fprintf(f, "OUTPUT_PATTERN %s\n", proj.output_pattern.c_str());
    fprintf(f, "KEYFRAME_COUNT %zu\n", proj.keyframes.size());
    fprintf(f, "\n");
    
    for (const auto& kf : proj.keyframes) {
        fprintf(f, "KEYFRAME %d\n", kf.frame);
        const Params& p = kf.params;
        fprintf(f, "  CAMERA %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", 
                p.cx, p.cy, p.cz, p.tx, p.ty, p.tz, p.fov);
        fprintf(f, "  LIGHT %.6f %.6f %.6f %.6f\n",
                p.lx, p.ly, p.lz, p.lradius);
        fprintf(f, "  FRACTAL %.6f %.6f %.6f %.6f %d %.6f %.6f %.6f %.6f %.6f %d %d\n",
                p.offsetx, p.offsety, p.offsetz, p.scale, p.max_iter, p.escape,
                p.step_size, p.dist_max, p.del_less, p.del_greater, p.hollow, p.seed_offset);
        fprintf(f, "  FLOOR %.6f %d %.6f\n",
                p.floor_y, p.floor_enable, p.checker_size);
        // Color ramp
        fprintf(f, "  COLOR_RAMP %d", p.color_ramp.num_stops);
        for (int i = 0; i < p.color_ramp.num_stops; i++) {
            fprintf(f, " %.4f %.4f %.4f %.4f", 
                    p.color_ramp.positions[i],
                    p.color_ramp.colors[i][0],
                    p.color_ramp.colors[i][1],
                    p.color_ramp.colors[i][2]);
        }
        fprintf(f, "\n");
        // Material properties
        fprintf(f, "  MATERIAL %.6f %.6f %d\n",
                p.specular_intensity, p.shininess, p.metallic);
        fprintf(f, "\n");
    }
    
    fclose(f);
    return true;
}

// Load project from file
inline bool load_project(AnimationProject& proj, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return false;
    
    proj.keyframes.clear();
    
    char line[1024];
    Keyframe current_kf;
    // Initialize with defaults to ensure new fields have valid values
    current_kf.params = default_params();
    bool in_keyframe = false;
    
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        if (line[0] == '#' || line[0] == '\0') continue;
        
        if (strncmp(line, "NAME ", 5) == 0) {
            proj.name = line + 5;
        } else if (strncmp(line, "FRAME_RANGE ", 12) == 0) {
            sscanf(line + 12, "%d %d", &proj.start_frame, &proj.end_frame);
        } else if (strncmp(line, "RENDER_SIZE ", 12) == 0) {
            sscanf(line + 12, "%d %d", &proj.render_width, &proj.render_height);
        } else if (strncmp(line, "OUTPUT_PATTERN ", 15) == 0) {
            proj.output_pattern = line + 15;
        } else if (strncmp(line, "KEYFRAME ", 9) == 0) {
            if (in_keyframe) {
                proj.keyframes.push_back(current_kf);
            }
            in_keyframe = true;
            current_kf = Keyframe();
            // Initialize with defaults for new fields not in old files
            current_kf.params = default_params();
            sscanf(line + 9, "%d", &current_kf.frame);
            for (int i = 0; i < 30; i++) {
                current_kf.tangent_in[i] = 0.0f;
                current_kf.tangent_out[i] = 0.0f;
            }
        } else if (in_keyframe && strncmp(line, "  CAMERA ", 9) == 0) {
            Params& p = current_kf.params;
            sscanf(line + 9, "%f %f %f %f %f %f %f",
                   &p.cx, &p.cy, &p.cz, &p.tx, &p.ty, &p.tz, &p.fov);
        } else if (in_keyframe && strncmp(line, "  LIGHT ", 8) == 0) {
            Params& p = current_kf.params;
            sscanf(line + 8, "%f %f %f %f", &p.lx, &p.ly, &p.lz, &p.lradius);
        } else if (in_keyframe && strncmp(line, "  FRACTAL ", 10) == 0) {
            Params& p = current_kf.params;
            sscanf(line + 10, "%f %f %f %f %d %f %f %f %f %f %d %d",
                   &p.offsetx, &p.offsety, &p.offsetz, &p.scale, &p.max_iter,
                   &p.escape, &p.step_size, &p.dist_max, &p.del_less, &p.del_greater,
                   &p.hollow, &p.seed_offset);
        } else if (in_keyframe && strncmp(line, "  FLOOR ", 8) == 0) {
            Params& p = current_kf.params;
            sscanf(line + 8, "%f %d %f", &p.floor_y, &p.floor_enable, &p.checker_size);
        } else if (in_keyframe && strncmp(line, "  COLOR_RAMP ", 13) == 0) {
            Params& p = current_kf.params;
            char* ptr = line + 13;
            int num = 0;
            sscanf(ptr, "%d", &num);
            p.color_ramp.num_stops = num;
            ptr = strchr(ptr, ' ');
            if (ptr) {
                for (int i = 0; i < num && ptr; i++) {
                    sscanf(ptr, " %f %f %f %f",
                           &p.color_ramp.positions[i],
                           &p.color_ramp.colors[i][0],
                           &p.color_ramp.colors[i][1],
                           &p.color_ramp.colors[i][2]);
                    // Move to next stop
                    for (int j = 0; j < 4 && ptr; j++) {
                        ptr = strchr(ptr + 1, ' ');
                    }
                }
            }
        } else if (in_keyframe && strncmp(line, "  MATERIAL ", 12) == 0) {
            Params& p = current_kf.params;
            sscanf(line + 12, "%f %f %d", &p.specular_intensity, &p.shininess, &p.metallic);
        }
    }
    
    if (in_keyframe) {
        proj.keyframes.push_back(current_kf);
    }
    
    fclose(f);
    return true;
}

// Get path to default scene file
inline std::string get_default_scene_path() {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.config/stepmarch/default.scene";
}

// Save default scene (all parameters for startup)
inline bool save_default_scene(const Params& p) {
    std::string path = get_default_scene_path();
    
    // Ensure directory exists
    std::string cmd = "mkdir -p \"" + path.substr(0, path.find_last_of('/')) + "\"";
    system(cmd.c_str());
    
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    
    fprintf(f, "# StepMarch Default Scene\n");
    fprintf(f, "# This file is loaded automatically on startup\n\n");
    
    // Fractal type
    fprintf(f, "FRACTAL_TYPE %d\n", p.fractal_type);
    
    // Camera
    fprintf(f, "\n# Camera\n");
    fprintf(f, "CAMERA %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", 
            p.cx, p.cy, p.cz, p.tx, p.ty, p.tz, p.fov);
    
    // Light
    fprintf(f, "\n# Light\n");
    fprintf(f, "LIGHT %.6f %.6f %.6f %.6f\n",
            p.lx, p.ly, p.lz, p.lradius);
    
    // Base fractal
    fprintf(f, "\n# Base Fractal\n");
    fprintf(f, "FRACTAL %.6f %.6f %.6f %.6f %d %.6f %.6f %.6f %.6f %.6f %d %d %d\n",
            p.offsetx, p.offsety, p.offsetz, p.scale, p.max_iter, p.escape,
            p.step_size, p.dist_max, p.del_less, p.del_greater, p.hollow, p.seed_offset, p.max_steps);
    
    // Floor
    fprintf(f, "\n# Floor\n");
    fprintf(f, "FLOOR %.6f %d %.6f\n",
            p.floor_y, p.floor_enable, p.checker_size);
    
    // Julia params
    fprintf(f, "\n# Julia Parameters\n");
    fprintf(f, "JULIA %.6f %.6f %.6f\n", p.julia_cx, p.julia_cy, p.julia_cz);
    
    // Mandelbulb params
    fprintf(f, "\n# Mandelbulb Parameters\n");
    fprintf(f, "MANDELBULB %.6f\n", p.mandelbulb_power);
    
    // Mandelbox params
    fprintf(f, "\n# Mandelbox Parameters\n");
    fprintf(f, "MANDELBOX %.6f %.6f\n", p.mandelbox_scale, p.mandelbox_folding);
    
    // Apollonian params
    fprintf(f, "\n# Apollonian Parameters\n");
    fprintf(f, "APOLLONIAN %.6f\n", p.apollonian_scale);
    
    // Menger params
    fprintf(f, "\n# Menger Parameters\n");
    fprintf(f, "MENGER %.6f\n", p.menger_scale);
    
    // Sierpinski params
    fprintf(f, "\n# Sierpinski Parameters\n");
    fprintf(f, "SIERPINSKI %.6f\n", p.sierpinski_scale);
    
    // Color ramp
    fprintf(f, "\n# Color Ramp\n");
    fprintf(f, "COLOR_RAMP %d", p.color_ramp.num_stops);
    for (int i = 0; i < p.color_ramp.num_stops; i++) {
        fprintf(f, " %.6f %.6f %.6f %.6f %d", 
                p.color_ramp.positions[i],
                p.color_ramp.colors[i][0],
                p.color_ramp.colors[i][1],
                p.color_ramp.colors[i][2],
                p.color_ramp.interp_modes[i]);
    }
    fprintf(f, "\n");
    
    // Material properties
    fprintf(f, "\n# Material\n");
    fprintf(f, "MATERIAL %.6f %.6f %d\n",
            p.specular_intensity, p.shininess, p.metallic);
    
    fclose(f);
    return true;
}

// Load default scene
inline bool load_default_scene(Params& p) {
    std::string path = get_default_scene_path();
    
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    
    // Start with defaults to ensure new fields are initialized
    p = default_params();
    
    char line[1024];
    
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        if (line[0] == '#' || line[0] == '\0') continue;
        
        if (strncmp(line, "FRACTAL_TYPE ", 13) == 0) {
            sscanf(line + 13, "%d", &p.fractal_type);
        } else if (strncmp(line, "CAMERA ", 7) == 0) {
            sscanf(line + 7, "%f %f %f %f %f %f %f",
                   &p.cx, &p.cy, &p.cz, &p.tx, &p.ty, &p.tz, &p.fov);
        } else if (strncmp(line, "LIGHT ", 6) == 0) {
            sscanf(line + 6, "%f %f %f %f", &p.lx, &p.ly, &p.lz, &p.lradius);
        } else if (strncmp(line, "FRACTAL ", 8) == 0) {
            // Try to parse with max_steps first (new format)
            int n = sscanf(line + 8, "%f %f %f %f %d %f %f %f %f %f %d %d %d",
                   &p.offsetx, &p.offsety, &p.offsetz, &p.scale, &p.max_iter,
                   &p.escape, &p.step_size, &p.dist_max, &p.del_less, &p.del_greater,
                   &p.hollow, &p.seed_offset, &p.max_steps);
            // If old format without max_steps, use default
            if (n < 13) {
                p.max_steps = 50000;
            }
        } else if (strncmp(line, "FLOOR ", 6) == 0) {
            sscanf(line + 6, "%f %d %f", &p.floor_y, &p.floor_enable, &p.checker_size);
        } else if (strncmp(line, "JULIA ", 6) == 0) {
            sscanf(line + 6, "%f %f %f", &p.julia_cx, &p.julia_cy, &p.julia_cz);
        } else if (strncmp(line, "MANDELBULB ", 11) == 0) {
            sscanf(line + 11, "%f", &p.mandelbulb_power);
        } else if (strncmp(line, "MANDELBOX ", 10) == 0) {
            sscanf(line + 10, "%f %f", &p.mandelbox_scale, &p.mandelbox_folding);
        } else if (strncmp(line, "APOLLONIAN ", 11) == 0) {
            sscanf(line + 11, "%f", &p.apollonian_scale);
        } else if (strncmp(line, "MENGER ", 7) == 0) {
            sscanf(line + 7, "%f", &p.menger_scale);
        } else if (strncmp(line, "SIERPINSKI ", 11) == 0) {
            sscanf(line + 11, "%f", &p.sierpinski_scale);
        } else if (strncmp(line, "MATERIAL ", 10) == 0) {
            sscanf(line + 10, "%f %f %d", &p.specular_intensity, &p.shininess, &p.metallic);
        } else if (strncmp(line, "COLOR_RAMP ", 11) == 0) {
            char* ptr = line + 11;
            int num = 0;
            sscanf(ptr, "%d", &num);
            p.color_ramp.num_stops = num;
            ptr = strchr(ptr, ' ');
            if (ptr) {
                for (int i = 0; i < num && ptr && i < MAX_RAMP_STOPS; i++) {
                    int mode = RAMP_INTERP_LINEAR;
                    int n = sscanf(ptr, " %f %f %f %f %d",
                           &p.color_ramp.positions[i],
                           &p.color_ramp.colors[i][0],
                           &p.color_ramp.colors[i][1],
                           &p.color_ramp.colors[i][2],
                           &mode);
                    if (n >= 4) {
                        p.color_ramp.interp_modes[i] = (n >= 5) ? mode : RAMP_INTERP_LINEAR;
                    }
                    // Move to next stop
                    for (int j = 0; j < 5 && ptr; j++) {
                        ptr = strchr(ptr + 1, ' ');
                    }
                }
            }
        }
    }
    
    fclose(f);
    return true;
}

#endif // ANIMATION_H
