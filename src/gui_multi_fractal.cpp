// StepMarch GUI - Interactive Fractal Renderer with Animation
// Houdini-style interface with timeline and keyframes

#define CL_TARGET_OPENCL_VERSION 300

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <CL/cl.h>
#include <vector>
#include <string>

// SDL2 and OpenGL
#include <SDL2/SDL.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// Dear ImGui
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

// Animation system (includes Params struct)
#include "animation.h"

// PNG export
extern "C" {
#include "png_writer.h"
}

#define PREVIEW_WIDTH 512
#define PREVIEW_HEIGHT 384
#define PI 3.14159265359f

// OpenCL context
typedef struct {
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem color_buffer;
    cl_mem depth_buffer;
    int width;
    int height;
} OpenCLContext;

char* load_kernel(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = (char*)malloc(size + 1);
    fread(src, 1, size, f);
    src[size] = '\0';
    fclose(f);
    return src;
}

int init_opencl(OpenCLContext* ctx, int width, int height) {
    ctx->width = width;
    ctx->height = height;
    
    cl_int err;
    err = clGetPlatformIDs(1, &ctx->platform, NULL);
    if (err != CL_SUCCESS) return -1;
    
    err = clGetDeviceIDs(ctx->platform, CL_DEVICE_TYPE_GPU, 1, &ctx->device, NULL);
    if (err != CL_SUCCESS) {
        err = clGetDeviceIDs(ctx->platform, CL_DEVICE_TYPE_CPU, 1, &ctx->device, NULL);
        if (err != CL_SUCCESS) return -1;
    }
    
    ctx->context = clCreateContext(NULL, 1, &ctx->device, NULL, NULL, &err);
    if (err != CL_SUCCESS) return -1;
    
    cl_queue_properties props[] = {CL_QUEUE_PROPERTIES, 0, 0};
    ctx->queue = clCreateCommandQueueWithProperties(ctx->context, ctx->device, props, &err);
    if (err != CL_SUCCESS) return -1;
    
    char* src = load_kernel("src/multi_fractal.cl");
    if (!src) return -1;
    
    ctx->program = clCreateProgramWithSource(ctx->context, 1, (const char**)&src, NULL, &err);
    free(src);
    if (err != CL_SUCCESS) return -1;
    
    err = clBuildProgram(ctx->program, 1, &ctx->device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char* log = (char*)malloc(log_size);
        clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        fprintf(stderr, "Build error: %s\n", log);
        free(log);
        return -1;
    }
    
    ctx->kernel = clCreateKernel(ctx->program, "render", &err);
    if (err != CL_SUCCESS) return -1;
    
    size_t color_size = width * height * 3 * sizeof(float);
    size_t depth_size = width * height * sizeof(float);
    
    ctx->color_buffer = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY, color_size, NULL, &err);
    if (err != CL_SUCCESS) return -1;
    
    ctx->depth_buffer = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY, depth_size, NULL, &err);
    if (err != CL_SUCCESS) return -1;
    
    return 0;
}

void render_fractal(OpenCLContext* ctx, Params* p, float* output) {
    cl_int err;
    int arg = 0;
    
    cl_float3 cam_origin = {{p->cx, p->cy, p->cz}};
    cl_float3 cam_target = {{p->tx, p->ty, p->tz}};
    cl_float3 cam_up = {{0, 1, 0}};
    cl_float3 light = {{p->lx, p->ly, p->lz}};
    cl_float3 offset = {{p->offsetx, p->offsety, p->offsetz}};
    
    err = clSetKernelArg(ctx->kernel, arg++, sizeof(cl_mem), &ctx->color_buffer);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_mem), &ctx->depth_buffer);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_int), &ctx->width);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_int), &ctx->height);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_float3), &cam_origin);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_float3), &cam_target);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_float3), &cam_up);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->fov);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_float3), &light);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->lradius);
    // Fractal type (must be right after light_radius in kernel!)
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->fractal_type);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(cl_float3), &offset);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->scale);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->max_iter);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->escape);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->step_size);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->dist_max);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->del_less);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->del_greater);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->hollow);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->seed_offset);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->floor_y);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->floor_enable);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->checker_size);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->max_steps);
    // Julia params
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->julia_cx);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->julia_cy);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->julia_cz);
    // Mandelbulb params
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->mandelbulb_power);
    // Mandelbox params
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->mandelbox_scale);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->mandelbox_folding);
    // Apollonian params
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->apollonian_scale);
    // Menger params
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->menger_scale);
    // Sierpinski params
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->sierpinski_scale);
    // Color ramp params - pass all 41 values individually
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->color_ramp.num_stops);
    // Positions (8 values)
    for (int i = 0; i < 8; i++) {
        err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->color_ramp.positions[i]);
    }
    // Colors R (8 values)
    for (int i = 0; i < 8; i++) {
        err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->color_ramp.colors[i][0]);
    }
    // Colors G (8 values)
    for (int i = 0; i < 8; i++) {
        err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->color_ramp.colors[i][1]);
    }
    // Colors B (8 values)
    for (int i = 0; i < 8; i++) {
        err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->color_ramp.colors[i][2]);
    }
    // Interpolation modes (8 values)
    for (int i = 0; i < 8; i++) {
        err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[i]);
    }
    // Material params
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->specular_intensity);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->shininess);
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(int), &p->metallic);
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Error setting kernel args: %d\n", err);
        return;
    }
    
    size_t global_work_size[2] = {(size_t)ctx->width, (size_t)ctx->height};
    err = clEnqueueNDRangeKernel(ctx->queue, ctx->kernel, 2, NULL, global_work_size, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) return;
    
    err = clEnqueueReadBuffer(ctx->queue, ctx->color_buffer, CL_TRUE, 0,
                              ctx->width * ctx->height * 3 * sizeof(float), output, 0, NULL, NULL);
}

void float_to_bytes(float* input, unsigned char* output, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        float r = input[i * 3 + 0];
        float g = input[i * 3 + 1];
        float b = input[i * 3 + 2];
        
        r = r / (1.0f + r);
        g = g / (1.0f + g);
        b = b / (1.0f + b);
        
        r = powf(r, 1.0f / 2.2f);
        g = powf(g, 1.0f / 2.2f);
        b = powf(b, 1.0f / 2.2f);
        
        output[i * 4 + 0] = (unsigned char)(fmaxf(0, fminf(255, r * 255)));
        output[i * 4 + 1] = (unsigned char)(fmaxf(0, fminf(255, g * 255)));
        output[i * 4 + 2] = (unsigned char)(fmaxf(0, fminf(255, b * 255)));
        output[i * 4 + 3] = 255;
    }
}

// Static state for color ramp interaction
static int dragged_stop_idx = -1;
static int color_picker_stop_idx = -1;
static float color_picker_original_color[3] = {0, 0, 0};
static double last_click_time = 0;
static int last_clicked_stop = -1;

// Color ramp widget - Houdini-style gradient editor
// Returns true if changed
bool draw_color_ramp(ColorRamp& ramp, float del_less, float del_greater, bool& changed) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 30.0f;
    
    // Draw the gradient background
    int segments = 128;
    float seg_width = width / segments;
    
    for (int i = 0; i < segments; i++) {
        float t = (float)i / (segments - 1);
        float r, g, b;
        ramp.sample(t, &r, &g, &b);
        
        ImU32 col = IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
        draw_list->AddRectFilled(
            ImVec2(pos.x + i * seg_width, pos.y),
            ImVec2(pos.x + (i + 1) * seg_width + 1, pos.y + height),
            col
        );
    }
    
    // Draw border
    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(255, 255, 255, 128));
    
    // Draw stop handles
    float handle_radius = 8.0f;
    float stop_y = pos.y + height + handle_radius + 2;
    
    for (int i = 0; i < ramp.num_stops; i++) {
        float stop_x = pos.x + ramp.positions[i] * width;
        
        // Draw triangle handle
        ImU32 handle_col = IM_COL32(
            (int)(ramp.colors[i][0] * 255),
            (int)(ramp.colors[i][1] * 255),
            (int)(ramp.colors[i][2] * 255),
            255
        );
        
        // Draw interpolation mode indicator (small line above triangle)
        if (i < ramp.num_stops - 1) {
            float mid_y = pos.y + height + 3;
            if (ramp.interp_modes[i] == RAMP_INTERP_CONSTANT) {
                // Draw vertical line for constant interpolation
                draw_list->AddLine(ImVec2(stop_x, mid_y - 3), ImVec2(stop_x, mid_y + 3), IM_COL32(255, 255, 255, 200), 2.0f);
            } else {
                // Draw diagonal for linear
                draw_list->AddLine(ImVec2(stop_x, mid_y), ImVec2(stop_x + 5, mid_y - 3), IM_COL32(255, 255, 255, 150), 1.5f);
            }
        }
        
        ImVec2 tri_pts[3] = {
            ImVec2(stop_x, stop_y - handle_radius),
            ImVec2(stop_x - handle_radius * 0.7f, stop_y + handle_radius * 0.5f),
            ImVec2(stop_x + handle_radius * 0.7f, stop_y + handle_radius * 0.5f)
        };
        
        // Highlight if being dragged
        ImU32 border_col = (dragged_stop_idx == i) ? IM_COL32(255, 255, 0, 255) : IM_COL32(255, 255, 255, 200);
        float border_thickness = (dragged_stop_idx == i) ? 2.0f : 1.0f;
        
        draw_list->AddTriangleFilled(tri_pts[0], tri_pts[1], tri_pts[2], handle_col);
        draw_list->AddTriangle(tri_pts[0], tri_pts[1], tri_pts[2], border_col, border_thickness);
    }
    
    // Invisible button for interaction
    ImGui::InvisibleButton("color_ramp", ImVec2(width, height + handle_radius * 2 + 5));
    
    bool is_hovered = ImGui::IsItemHovered();
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    double current_time = ImGui::GetTime();
    
    // Track drag state
    static bool is_dragging = false;
    static float drag_start_mouse_x = 0;
    static float drag_start_position = 0;
    
    // Handle dragging
    if (dragged_stop_idx >= 0) {
        if (ImGui::IsMouseDown(0)) {
            // Continue dragging - update position based on mouse movement
            float mouse_delta = (mouse_pos.x - drag_start_mouse_x) / width;
            float new_t = drag_start_position + mouse_delta;
            new_t = fmaxf(0.0f, fminf(1.0f, new_t));
            
            // Don't allow crossing neighbors
            if (dragged_stop_idx > 0) {
                new_t = fmaxf(new_t, ramp.positions[dragged_stop_idx - 1] + 0.01f);
            }
            if (dragged_stop_idx < ramp.num_stops - 1) {
                new_t = fminf(new_t, ramp.positions[dragged_stop_idx + 1] - 0.01f);
            }
            
            if (ramp.positions[dragged_stop_idx] != new_t) {
                ramp.positions[dragged_stop_idx] = new_t;
                changed = true;
            }
        } else {
            // Mouse released - stop dragging
            dragged_stop_idx = -1;
            is_dragging = false;
        }
    }
    
    // Handle mouse down on handles to start drag
    if (is_hovered && ImGui::IsMouseClicked(0) && !is_dragging) {
        float click_t = (mouse_pos.x - pos.x) / width;
        click_t = fmaxf(0.0f, fminf(1.0f, click_t));
        
        int clicked_stop = ramp.find_stop_near(click_t, 0.03f);
        
        if (clicked_stop >= 0) {
            // Start dragging the handle
            dragged_stop_idx = clicked_stop;
            is_dragging = true;
            drag_start_mouse_x = mouse_pos.x;
            drag_start_position = ramp.positions[clicked_stop];
            
            // Check for double-click (within 0.3 seconds on same stop)
            if (last_clicked_stop == clicked_stop && (current_time - last_click_time) < 0.3) {
                // Double-click: open color picker
                color_picker_stop_idx = clicked_stop;
                color_picker_original_color[0] = ramp.colors[clicked_stop][0];
                color_picker_original_color[1] = ramp.colors[clicked_stop][1];
                color_picker_original_color[2] = ramp.colors[clicked_stop][2];
                ImGui::OpenPopup("ColorPickerModal");
                // Don't reset last_clicked_stop here so we can still track triple clicks if needed
            }
            
            last_clicked_stop = clicked_stop;
            last_click_time = current_time;
        } else if (ramp.num_stops < MAX_RAMP_STOPS) {
            // Click on gradient (not on handle): Add new stop
            float r, g, b;
            ramp.sample(click_t, &r, &g, &b);
            int new_idx = ramp.add_stop(click_t, r, g, b);
            changed = true;
        }
    }
    
    // Handle right-click on handle to toggle interpolation mode
    if (is_hovered && ImGui::IsMouseClicked(1)) {
        float click_t = (mouse_pos.x - pos.x) / width;
        int existing_stop = ramp.find_stop_near(click_t, 0.03f);
        if (existing_stop >= 0 && existing_stop < ramp.num_stops - 1) {
            // Right-click on handle: toggle interpolation mode for this segment
            ramp.interp_modes[existing_stop] = (ramp.interp_modes[existing_stop] == RAMP_INTERP_LINEAR)
                                                ? RAMP_INTERP_CONSTANT : RAMP_INTERP_LINEAR;
            changed = true;
        }
    }
    
    // Handle Ctrl+click to delete stop
    if (is_hovered && ImGui::IsMouseClicked(0) && ImGui::GetIO().KeyCtrl) {
        float click_t = (mouse_pos.x - pos.x) / width;
        int existing_stop = ramp.find_stop_near(click_t, 0.03f);
        if (existing_stop >= 0 && ramp.num_stops > 2) {
            ramp.remove_stop(existing_stop);
            if (dragged_stop_idx == existing_stop) dragged_stop_idx = -1;
            changed = true;
        }
    }
    
    // Full color picker modal (like Houdini)
    if (ImGui::BeginPopupModal("ColorPickerModal", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (color_picker_stop_idx >= 0 && color_picker_stop_idx < ramp.num_stops) {
            ImGui::Text("Color Stop %d (%.0f%%)", color_picker_stop_idx + 1, 
                       ramp.positions[color_picker_stop_idx] * 100);
            ImGui::Separator();
            
            float* col = ramp.colors[color_picker_stop_idx];
            
            // Show original color
            ImGui::Text("Original:");
            ImGui::ColorButton("##original", ImVec4(color_picker_original_color[0], 
                                                       color_picker_original_color[1],
                                                       color_picker_original_color[2], 1.0f),
                               ImGuiColorEditFlags_NoPicker, ImVec2(60, 40));
            
            ImGui::SameLine();
            ImGui::Text("→");
            ImGui::SameLine();
            
            // Show current color
            ImGui::Text("Current:");
            if (ImGui::ColorButton("##current", ImVec4(col[0], col[1], col[2], 1.0f),
                                   ImGuiColorEditFlags_NoPicker, ImVec2(60, 40))) {
                // Clicking current opens the actual picker
            }
            
            ImGui::Separator();
            
            // Full color picker
            if (ImGui::ColorPicker3("##picker", col, 
                                    ImGuiColorEditFlags_DisplayRGB | 
                                    ImGuiColorEditFlags_DisplayHSV |
                                    ImGuiColorEditFlags_PickerHueWheel)) {
                changed = true;
            }
            
            // Position slider
            ImGui::Separator();
            if (ImGui::SliderFloat("Position", &ramp.positions[color_picker_stop_idx], 0.0f, 1.0f, "%.3f")) {
                // Clamp to avoid crossing
                if (color_picker_stop_idx > 0) {
                    ramp.positions[color_picker_stop_idx] = fmaxf(ramp.positions[color_picker_stop_idx],
                                                                   ramp.positions[color_picker_stop_idx - 1] + 0.01f);
                }
                if (color_picker_stop_idx < ramp.num_stops - 1) {
                    ramp.positions[color_picker_stop_idx] = fminf(ramp.positions[color_picker_stop_idx],
                                                                   ramp.positions[color_picker_stop_idx + 1] - 0.01f);
                }
                changed = true;
            }
            
            // Interpolation mode toggle
            const char* interp_names[] = {"Linear", "Constant"};
            if (color_picker_stop_idx < ramp.num_stops - 1) {
                if (ImGui::Combo("Interpolation", &ramp.interp_modes[color_picker_stop_idx], interp_names, 2)) {
                    changed = true;
                }
            }
            
            ImGui::Separator();
            
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                color_picker_stop_idx = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                // Restore original color
                col[0] = color_picker_original_color[0];
                col[1] = color_picker_original_color[1];
                col[2] = color_picker_original_color[2];
                changed = true;
                ImGui::CloseCurrentPopup();
                color_picker_stop_idx = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete", ImVec2(120, 0)) && ramp.num_stops > 2) {
                ramp.remove_stop(color_picker_stop_idx);
                color_picker_stop_idx = -1;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    
    // Show range info
    ImGui::Text("Range: %.1f to %.1f", del_less, del_greater);
    ImGui::TextDisabled("Drag=Move position | Double-click=Edit color | Right-click=Toggle interp | Ctrl+Click=Delete");
    
    return changed;
}

// Get list of saved color ramp presets
std::vector<std::string> get_color_ramp_presets() {
    std::vector<std::string> presets;
    
    // Create directory if needed
    system("mkdir -p ~/.config/stepmarch/ramps");
    
    // List .ramp files
    FILE* pipe = popen("ls ~/.config/stepmarch/ramps/*.ramp 2>/dev/null | xargs -n1 basename -s .ramp 2>/dev/null", "r");
    if (pipe) {
        char line[128];
        while (fgets(line, sizeof(line), pipe)) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (strlen(line) > 0) {
                presets.push_back(line);
            }
        }
        pclose(pipe);
    }
    
    return presets;
}

// Save color ramp preset
void save_color_ramp_preset(const ColorRamp& ramp, const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "~/.config/stepmarch/ramps/%s.ramp", name);
    
    // Expand tilde
    char expanded[512];
    if (path[0] == '~') {
        snprintf(expanded, sizeof(expanded), "%s%s", getenv("HOME"), path + 1);
    } else {
        strncpy(expanded, path, sizeof(expanded) - 1);
    }
    
    ColorRamp temp = ramp;
    strncpy(temp.name, name, sizeof(temp.name) - 1);
    temp.save_to_file(expanded);
}

// Load color ramp preset
bool load_color_ramp_preset(ColorRamp& ramp, const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "~/.config/stepmarch/ramps/%s.ramp", name);
    
    // Expand tilde
    char expanded[512];
    if (path[0] == '~') {
        snprintf(expanded, sizeof(expanded), "%s%s", getenv("HOME"), path + 1);
    } else {
        strncpy(expanded, path, sizeof(expanded) - 1);
    }
    
    return ramp.load_from_file(expanded);
}

void export_image(OpenCLContext* ctx, Params* p, int width, int height, const char* filename) {
    OpenCLContext highres;
    if (init_opencl(&highres, width, height) != 0) {
        fprintf(stderr, "Failed to init high-res context\n");
        return;
    }
    
    float* output = (float*)malloc(width * height * 3 * sizeof(float));
    render_fractal(&highres, p, output);
    
    Image* img = image_create(width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            image_set_pixel(img, x, y, output[idx * 3 + 0], output[idx * 3 + 1], output[idx * 3 + 2]);
        }
    }
    
    image_save_png(img, filename);
    image_free(img);
    free(output);
    
    clReleaseMemObject(highres.color_buffer);
    clReleaseMemObject(highres.depth_buffer);
    clReleaseKernel(highres.kernel);
    clReleaseProgram(highres.program);
    clReleaseCommandQueue(highres.queue);
    clReleaseContext(highres.context);
}

// Initialize OpenCL for high-quality rendering (loads high_quality.cl)
int init_high_quality_opencl(OpenCLContext* ctx, int width, int height) {
    ctx->width = width;
    ctx->height = height;
    
    cl_int err;
    err = clGetPlatformIDs(1, &ctx->platform, NULL);
    if (err != CL_SUCCESS) return -1;
    
    err = clGetDeviceIDs(ctx->platform, CL_DEVICE_TYPE_GPU, 1, &ctx->device, NULL);
    if (err != CL_SUCCESS) {
        printf("No GPU found, falling back to CPU...\n");
        err = clGetDeviceIDs(ctx->platform, CL_DEVICE_TYPE_CPU, 1, &ctx->device, NULL);
        if (err != CL_SUCCESS) return -1;
    } else {
        char device_name[256];
        clGetDeviceInfo(ctx->device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
        printf("Using GPU: %s\n", device_name);
    }
    
    ctx->context = clCreateContext(NULL, 1, &ctx->device, NULL, NULL, &err);
    if (err != CL_SUCCESS) return -1;
    
    cl_queue_properties props[] = {CL_QUEUE_PROPERTIES, 0, 0};
    ctx->queue = clCreateCommandQueueWithProperties(ctx->context, ctx->device, props, &err);
    if (err != CL_SUCCESS) return -1;
    
    char* src = load_kernel("src/high_quality.cl");
    if (!src) return -1;
    
    ctx->program = clCreateProgramWithSource(ctx->context, 1, (const char**)&src, NULL, &err);
    free(src);
    if (err != CL_SUCCESS) return -1;
    
    err = clBuildProgram(ctx->program, 1, &ctx->device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char* log = (char*)malloc(log_size);
        clGetProgramBuildInfo(ctx->program, ctx->device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        fprintf(stderr, "High quality build error: %s\n", log);
        free(log);
        return -1;
    }
    
    ctx->kernel = clCreateKernel(ctx->program, "render_high_quality", &err);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Failed to create render_high_quality kernel: %d\n", err);
        return -1;
    }
    
    size_t color_size = width * height * 3 * sizeof(float);
    ctx->color_buffer = clCreateBuffer(ctx->context, CL_MEM_WRITE_ONLY, color_size, NULL, &err);
    if (err != CL_SUCCESS) return -1;
    
    ctx->depth_buffer = NULL;
    
    return 0;
}

// High-quality render with supersampling, better shadows, DOF, tone mapping
void export_image_high_quality(OpenCLContext* ctx, Params* p, int width, int height,
                                const char* filename,
                                int aa_samples,        // 1, 4, 9, 16
                                int shadow_samples,    // 1, 8, 16, 32, 64
                                int ao_samples,        // 0, 8, 16, 32
                                int enable_dof,        // 0 or 1
                                float aperture_size,
                                float focal_distance,
                                int enable_tonemap,
                                float exposure,
                                float contrast) {
    OpenCLContext hq_ctx;
    if (init_high_quality_opencl(&hq_ctx, width, height) != 0) {
        fprintf(stderr, "Failed to init high quality context\n");
        return;
    }
    
    printf("Rendering high quality image...\n");
    printf("  Resolution: %dx%d\n", width, height);
    printf("  Anti-aliasing: %dx supersampling\n", (int)sqrt((float)aa_samples));
    printf("  Shadow samples: %d\n", shadow_samples);
    printf("  AO samples: %d\n", ao_samples);
    
    float* output = (float*)malloc(width * height * 3 * sizeof(float));
    
    // Set all kernel arguments
    int arg = 0;
    cl_int err = 0;
    
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_mem), &hq_ctx.color_buffer);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_int), &width);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_int), &height);
    
    // Quality settings
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_int), &aa_samples);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_int), &shadow_samples);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_int), &ao_samples);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_int), &enable_dof);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &aperture_size);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &focal_distance);
    
    // Tone mapping
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &exposure);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &contrast);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_int), &enable_tonemap);
    
    // Camera
    cl_float3 cam_origin = {{p->cx, p->cy, p->cz}};
    cl_float3 cam_target = {{p->tx, p->ty, p->tz}};
    cl_float3 cam_up = {{0, 1, 0}};
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_float3), &cam_origin);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_float3), &cam_target);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_float3), &cam_up);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->fov);
    
    // Light
    cl_float3 light = {{p->lx, p->ly, p->lz}};
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_float3), &light);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->lradius);
    
    // Fractal params
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->fractal_type);
    cl_float3 offset = {{p->offsetx, p->offsety, p->offsetz}};
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(cl_float3), &offset);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->scale);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->max_iter);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->escape);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->step_size);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->dist_max);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->del_less);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->del_greater);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->hollow);
    
    // Julia
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->julia_cx);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->julia_cy);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->julia_cz);
    
    // Mandelbulb
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->mandelbulb_power);
    
    // Mandelbox
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->mandelbox_scale);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->mandelbox_folding);
    
    // Apollonian
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->apollonian_scale);
    
    // Menger
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->menger_scale);
    
    // Sierpinski
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->sierpinski_scale);
    
    // Floor
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->floor_y);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->floor_enable);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->checker_size);
    
    // Material
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->specular_intensity);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->shininess);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->metallic);
    
    // Color ramp
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.num_stops);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[3]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[4]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[5]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[6]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.positions[7]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[0][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[1][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[2][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[3][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[4][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[5][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[6][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[7][0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[0][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[1][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[2][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[3][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[4][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[5][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[6][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[7][1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[0][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[1][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[2][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[3][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[4][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[5][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[6][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(float), &p->color_ramp.colors[7][2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[0]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[1]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[2]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[3]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[4]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[5]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[6]);
    err |= clSetKernelArg(hq_ctx.kernel, arg++, sizeof(int), &p->color_ramp.interp_modes[7]);
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Error setting kernel args: %d\n", err);
        free(output);
        return;
    }
    
    // Execute kernel
    size_t global_size[2] = {(size_t)width, (size_t)height};
    err = clEnqueueNDRangeKernel(hq_ctx.queue, hq_ctx.kernel, 2, NULL, global_size, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "Kernel execution error: %d\n", err);
        free(output);
        return;
    }
    
    clFinish(hq_ctx.queue);
    
    // Read back result
    clEnqueueReadBuffer(hq_ctx.queue, hq_ctx.color_buffer, CL_TRUE, 0,
                        width * height * 3 * sizeof(float), output, 0, NULL, NULL);
    
    // Save as PNG
    Image* img = image_create(width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            image_set_pixel(img, x, y, output[idx * 3 + 0], output[idx * 3 + 1], output[idx * 3 + 2]);
        }
    }
    
    image_save_png(img, filename);
    image_free(img);
    free(output);
    
    printf("Saved: %s\n", filename);
    
    clReleaseMemObject(hq_ctx.color_buffer);
    clReleaseKernel(hq_ctx.kernel);
    clReleaseProgram(hq_ctx.program);
    clReleaseCommandQueue(hq_ctx.queue);
    clReleaseContext(hq_ctx.context);
}

// Find next keyframe
int find_next_keyframe(const AnimationProject& proj, int current_frame) {
    for (const auto& kf : proj.keyframes) {
        if (kf.frame > current_frame) {
            return kf.frame;
        }
    }
    return -1; // No next keyframe
}

// Find previous keyframe
int find_prev_keyframe(const AnimationProject& proj, int current_frame) {
    int prev = -1;
    for (const auto& kf : proj.keyframes) {
        if (kf.frame < current_frame) {
            prev = kf.frame;
        } else {
            break;
        }
    }
    return prev;
}

// Draw timeline widget
void draw_timeline(AnimationProject& proj, int& current_frame, bool& params_changed,
                   bool& is_playing, bool& jump_to_next_kf, bool& jump_to_prev_kf) {
    ImGui::Separator();
    ImGui::Text("Timeline");
    
    // Frame range
    ImGui::PushItemWidth(80);
    ImGui::InputInt("Start", &proj.start_frame);
    ImGui::SameLine();
    ImGui::InputInt("End", &proj.end_frame);
    ImGui::PopItemWidth();
    
    // Playback controls
    if (ImGui::Button(is_playing ? "Stop" : "Play Preview", ImVec2(120, 25))) {
        is_playing = !is_playing;
    }
    ImGui::SameLine();
    ImGui::Text("24 FPS");
    
    // Current frame slider
    ImGui::PushItemWidth(400);
    if (ImGui::SliderInt("Frame", &current_frame, proj.start_frame, proj.end_frame)) {
        params_changed = true;
        is_playing = false; // Stop playback when user drags
    }
    ImGui::PopItemWidth();
    
    // Timeline visualization
    float timeline_width = 400.0f;
    float timeline_height = 30.0f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Background
    draw_list->AddRectFilled(pos, ImVec2(pos.x + timeline_width, pos.y + timeline_height), 
                              IM_COL32(40, 40, 40, 255));
    
    // Frame range
    float frame_range = (float)(proj.end_frame - proj.start_frame);
    if (frame_range > 0) {
        // Draw keyframes
        for (const auto& kf : proj.keyframes) {
            float x = pos.x + ((float)(kf.frame - proj.start_frame) / frame_range) * timeline_width;
            draw_list->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + timeline_height), 
                               IM_COL32(255, 200, 0, 255), 3.0f);
        }
        
        // Current frame indicator
        float cur_x = pos.x + ((float)(current_frame - proj.start_frame) / frame_range) * timeline_width;
        draw_list->AddLine(ImVec2(cur_x, pos.y), ImVec2(cur_x, pos.y + timeline_height), 
                           IM_COL32(0, 255, 0, 255), 2.0f);
    }
    
    ImGui::Dummy(ImVec2(timeline_width, timeline_height));
    
    // Keyframe buttons - arranged horizontally
    if (ImGui::Button("<< Prev KF", ImVec2(90, 25))) {
        jump_to_prev_kf = true;
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Add KF", ImVec2(70, 25))) {
        set_keyframe(proj, current_frame, proj.keyframes.empty() ? default_params() : 
                     interpolate_params(proj, current_frame));
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Del KF", ImVec2(70, 25))) {
        delete_keyframe(proj, current_frame);
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Next KF >>", ImVec2(90, 25))) {
        jump_to_next_kf = true;
    }
    ImGui::SameLine();
    
    bool is_keyframe = has_keyframe(proj, current_frame);
    ImGui::Text(is_keyframe ? "[KEYFRAME]" : "[%d keyframes]", (int)proj.keyframes.size());
    
    ImGui::Separator();
}

int main(int argc, char** argv) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    int window_width = 1400;
    int window_height = 800;
    SDL_Window* window = SDL_CreateWindow("StepMarch - Animation Edition",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          window_width, window_height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);
    
    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    ImGui::StyleColorsDark();
    
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");
    
    // Initialize OpenCL
    OpenCLContext cl_ctx;
    if (init_opencl(&cl_ctx, PREVIEW_WIDTH, PREVIEW_HEIGHT) != 0) {
        fprintf(stderr, "Failed to initialize OpenCL\n");
        return 1;
    }
    
    // Animation project
    AnimationProject proj;
    proj.name = "Untitled";
    proj.start_frame = 1;
    proj.end_frame = 250;
    proj.current_frame = 1;
    proj.render_width = 1920;
    proj.render_height = 1080;
    proj.output_pattern = "output/render_%04d.png";  // Default to render_ prefix
    
    // Current parameters - load default scene if it exists
    Params params = default_params();
    if (!load_default_scene(params)) {
        // No default scene found, use built-in defaults
        params = default_params();
    }
    
    // Preview buffer
    float* preview_float = (float*)malloc(PREVIEW_WIDTH * PREVIEW_HEIGHT * 3 * sizeof(float));
    unsigned char* preview_bytes = (unsigned char*)malloc(PREVIEW_WIDTH * PREVIEW_HEIGHT * 4);
    
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Initial render
    render_fractal(&cl_ctx, &params, preview_float);
    float_to_bytes(preview_float, preview_bytes, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PREVIEW_WIDTH, PREVIEW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, preview_bytes);
    
    // State
    bool needs_render = true;
    bool show_render_still_modal = false;
    bool show_render_anim_modal = false;
    bool show_save_modal = false;
    bool show_load_modal = false;
    bool is_rendering = false;
    bool is_encoding_mp4 = false;
    bool params_changed = false;
    bool is_playing = false;
    bool jump_to_next_kf = false;
    bool jump_to_prev_kf = false;
    bool create_mp4 = false;
    int render_current_frame = 0;
    char project_filename[256] = "project.step";
    char render_status[256] = "";
    char mp4_filename[256] = "output/animation.mp4";
    
    // Playback timing
    Uint32 last_frame_time = SDL_GetTicks();
    const Uint32 FRAME_DELAY_MS = 1000 / 24; // 24 FPS
    
    // Camera control state (Houdini-style)
    bool is_orbiting = false;
    bool is_panning = false;
    int last_mouse_x = 0;
    int last_mouse_y = 0;
    float orbit_speed = 0.01f;
    float pan_speed = 0.01f;
    float zoom_speed = 0.1f;
    
    // Main loop
    bool running = true;
    
    // Track preview image position for camera controls
    ImVec2 preview_min(0, 0);
    ImVec2 preview_max(0, 0);
    
    while (running) {
        Uint32 current_time = SDL_GetTicks();
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            
            // Houdini-style camera controls (only when mouse is over preview image)
            // Check if mouse is over the 3D preview area
            int mouse_x, mouse_y;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            bool mouse_over_preview = (mouse_x >= preview_min.x && mouse_x < preview_max.x &&
                                       mouse_y >= preview_min.y && mouse_y < preview_max.y);
            
            // Allow camera controls when mouse is over preview image (regardless of ImGui state)
            if (mouse_over_preview) {
                // Mouse button down - start camera operation
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    const Uint8* key_state = SDL_GetKeyboardState(NULL);
                    bool alt_held = key_state[SDL_SCANCODE_LALT] || key_state[SDL_SCANCODE_RALT];
                    
                    if (alt_held) {
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            is_orbiting = true;
                            last_mouse_x = event.button.x;
                            last_mouse_y = event.button.y;
                            SDL_SetRelativeMouseMode(SDL_TRUE);  // Capture mouse for smooth dragging
                        } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                            is_panning = true;
                            last_mouse_x = event.button.x;
                            last_mouse_y = event.button.y;
                            SDL_SetRelativeMouseMode(SDL_TRUE);
                        }
                    }
                }
                
                // Mouse button up - end camera operation
                if (event.type == SDL_MOUSEBUTTONUP) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        is_orbiting = false;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        is_panning = false;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                    }
                }
                
                // Mouse wheel - zoom
                if (event.type == SDL_MOUSEWHEEL) {
                    const Uint8* key_state = SDL_GetKeyboardState(NULL);
                    bool alt_held = key_state[SDL_SCANCODE_LALT] || key_state[SDL_SCANCODE_RALT];
                    
                    if (alt_held) {
                        // Calculate view direction
                        float dx = params.tx - params.cx;
                        float dy = params.ty - params.cy;
                        float dz = params.tz - params.cz;
                        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                        
                        // Move camera along view direction
                        float zoom_factor = 1.0f - (event.wheel.y * zoom_speed);
                        zoom_factor = fmaxf(0.1f, fminf(10.0f, zoom_factor));
                        
                        params.cx = params.tx - dx * zoom_factor;
                        params.cy = params.ty - dy * zoom_factor;
                        params.cz = params.tz - dz * zoom_factor;
                        
                        needs_render = true;
                        
                        // Update keyframe if one exists at current frame
                        if (has_keyframe(proj, proj.current_frame)) {
                            set_keyframe(proj, proj.current_frame, params);
                        }
                    }
                }
            }
        }
        
        // Handle active camera operations (mouse motion)
        if (is_orbiting || is_panning) {
            int mouse_x, mouse_y;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            
            float delta_x = (float)(mouse_x - last_mouse_x);
            float delta_y = (float)(mouse_y - last_mouse_y);
            
            if (is_orbiting) {
                // Orbit around target
                float dx = params.cx - params.tx;
                float dy = params.cy - params.ty;
                float dz = params.cz - params.tz;
                float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                
                // Calculate current angles
                float azimuth = atan2f(dx, dz);
                float elevation = asinf(fmaxf(-1.0f, fminf(1.0f, dy / dist)));
                
                // Apply rotation
                azimuth -= delta_x * orbit_speed;
                elevation += delta_y * orbit_speed;
                
                // Clamp elevation to avoid gimbal lock
                elevation = fmaxf(-PI/2 + 0.01f, fminf(PI/2 - 0.01f, elevation));
                
                // Calculate new camera position
                float cos_elev = cosf(elevation);
                params.cx = params.tx + dist * sinf(azimuth) * cos_elev;
                params.cy = params.ty + dist * sinf(elevation);
                params.cz = params.tz + dist * cosf(azimuth) * cos_elev;
                
                needs_render = true;
            } else if (is_panning) {
                // Pan camera (move both camera and target)
                float dx = params.tx - params.cx;
                float dy = params.ty - params.cy;
                float dz = params.tz - params.cz;
                
                // Calculate camera basis vectors
                float forward_len = sqrtf(dx*dx + dy*dy + dz*dz);
                float fx = dx / forward_len;
                float fy = dy / forward_len;
                float fz = dz / forward_len;
                
                // Right vector = forward x up
                float rx = fz * 1.0f - fy * 0.0f;
                float ry = fx * 0.0f - fz * 0.0f;
                float rz = fy * 0.0f - fx * 1.0f;
                float right_len = sqrtf(rx*rx + ry*ry + rz*rz);
                rx /= right_len; ry /= right_len; rz /= right_len;
                
                // Up vector = right x forward
                float ux = ry * fz - rz * fy;
                float uy = rz * fx - rx * fz;
                float uz = rx * fy - ry * fx;
                
                // Pan amount based on distance to target
                float pan_amount = forward_len * pan_speed;
                
                // Move in screen space (right and up) - inverted for Houdini-style controls
                float pan_x = (delta_x * rx - delta_y * ux) * pan_amount;
                float pan_y = (delta_x * ry - delta_y * uy) * pan_amount;
                float pan_z = (delta_x * rz - delta_y * uz) * pan_amount;
                
                params.cx += pan_x;
                params.cy += pan_y;
                params.cz += pan_z;
                params.tx += pan_x;
                params.ty += pan_y;
                params.tz += pan_z;
                
                needs_render = true;
            }
            
            // Update keyframe if one exists at current frame (so camera changes persist)
            if (has_keyframe(proj, proj.current_frame)) {
                set_keyframe(proj, proj.current_frame, params);
            }
            
            last_mouse_x = mouse_x;
            last_mouse_y = mouse_y;
        }
        
        // Handle playback at 24 FPS
        if (is_playing) {
            if (current_time - last_frame_time >= FRAME_DELAY_MS) {
                proj.current_frame++;
                if (proj.current_frame > proj.end_frame) {
                    proj.current_frame = proj.start_frame; // Loop
                }
                params_changed = true;
                needs_render = true;
                last_frame_time = current_time;
            }
        }
        
        // Handle jump to next keyframe
        if (jump_to_next_kf) {
            int next = find_next_keyframe(proj, proj.current_frame);
            if (next > 0) {
                proj.current_frame = next;
                params_changed = true;
            }
            jump_to_next_kf = false;
        }
        
        // Handle jump to previous keyframe
        if (jump_to_prev_kf) {
            int prev = find_prev_keyframe(proj, proj.current_frame);
            if (prev > 0) {
                proj.current_frame = prev;
                params_changed = true;
            }
            jump_to_prev_kf = false;
        }
        
        // Handle animation rendering (high-res export)
        if (is_rendering && render_current_frame <= proj.end_frame) {
            Params frame_params = interpolate_params(proj, render_current_frame);
            
            char filename[256];
            snprintf(filename, sizeof(filename), proj.output_pattern.c_str(), render_current_frame);
            
            export_image(&cl_ctx, &frame_params, proj.render_width, proj.render_height, filename);
            
            snprintf(render_status, sizeof(render_status), "Rendering frame %d/%d", 
                    render_current_frame, proj.end_frame);
            
            render_current_frame++;
            
            if (render_current_frame > proj.end_frame) {
                is_rendering = false;
                
                // Start FFmpeg encoding if requested
                if (create_mp4) {
                    is_encoding_mp4 = true;
                    snprintf(render_status, sizeof(render_status), "Encoding MP4 with FFmpeg...");
                } else {
                    snprintf(render_status, sizeof(render_status), "Render complete! Files: render_%04d.png to render_%04d.png", 
                            proj.start_frame, proj.end_frame);
                }
            }
        }
        
        // Handle FFmpeg encoding (runs after all frames are rendered)
        if (is_encoding_mp4) {
            // Extract pattern base (remove %04d.png)
            char input_pattern[512];
            strncpy(input_pattern, proj.output_pattern.c_str(), sizeof(input_pattern) - 1);
            input_pattern[sizeof(input_pattern) - 1] = '\0';
            
            // Build FFmpeg command with high quality H.264 settings
            char ffmpeg_cmd[1024];
            snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
                "ffmpeg -y -framerate 24 -i \"%s\" -c:v libx264 -preset slow -crf 17 -pix_fmt yuv420p \"%s\" 2>&1",
                proj.output_pattern.c_str(), mp4_filename);
            
            printf("Running FFmpeg: %s\n", ffmpeg_cmd);
            int result = system(ffmpeg_cmd);
            
            is_encoding_mp4 = false;
            
            if (result == 0) {
                snprintf(render_status, sizeof(render_status), 
                        "Complete! MP4 saved: %s", mp4_filename);
            } else {
                snprintf(render_status, sizeof(render_status), 
                        "Frames rendered, but FFmpeg failed (exit code: %d)", result);
            }
        }
        
        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        // Menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Project")) {
                    proj = AnimationProject();
                    proj.name = "Untitled";
                    proj.keyframes.clear();
                    proj.output_pattern = "output/render_%04d.png";
                    // Load default scene if it exists, otherwise use built-in defaults
                    if (!load_default_scene(params)) {
                        params = default_params();
                    }
                    needs_render = true;
                }
                if (ImGui::MenuItem("Save Project...")) {
                    show_save_modal = true;
                }
                if (ImGui::MenuItem("Load Project...")) {
                    show_load_modal = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Default Scene")) {
                    if (save_default_scene(params)) {
                        printf("Default scene saved to ~/.config/stepmarch/default.scene\n");
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {
                    running = false;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        
        // Main window
        ImGui::SetNextWindowPos(ImVec2(0, 20));
        ImGui::SetNextWindowSize(ImVec2((float)window_width, (float)window_height - 20));
        ImGui::Begin("StepMarch Animation", NULL, 
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                    ImGuiWindowFlags_MenuBar);
        
        // Left panel - Preview
        ImGui::BeginChild("PreviewPanel", ImVec2(window_width * 0.6f, 0), true);
        ImGui::Text("Preview - Frame %d %s", proj.current_frame, is_playing ? "(Playing)" : "");
        
        // Camera controls hint - shows active state
        if (is_orbiting) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Camera: ORBITING (Alt+LMB)");
        } else if (is_panning) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Camera: PANNING (Alt+MMB)");
        } else {
            ImGui::TextDisabled("Camera: Alt+LMB=Orbit | Alt+MMB=Pan | Alt+Scroll=Zoom");
        }
        
        // Store preview position BEFORE drawing image (for mouse hit testing)
        preview_min = ImGui::GetCursorScreenPos();
        preview_max = ImVec2(preview_min.x + PREVIEW_WIDTH, preview_min.y + PREVIEW_HEIGHT);
        
        ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(PREVIEW_WIDTH, PREVIEW_HEIGHT));
        
        // Render buttons
        if (ImGui::Button("Refresh", ImVec2(100, 25))) {
            needs_render = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Render Still", ImVec2(100, 25))) {
            show_render_still_modal = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Render Animation", ImVec2(130, 25))) {
            show_render_anim_modal = true;
        }
        
        // Render status
        if (strlen(render_status) > 0) {
            ImGui::Text("%s", render_status);
        }
        
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Right panel - Parameters and Timeline
        ImGui::BeginChild("ControlPanel", ImVec2(0, 0), true);
        
        // TIMELINE SECTION
        draw_timeline(proj, proj.current_frame, params_changed, is_playing, 
                      jump_to_next_kf, jump_to_prev_kf);
        
        // Update params if frame changed
        if (params_changed) {
            params = interpolate_params(proj, proj.current_frame);
            needs_render = true;
            params_changed = false;
        }
        
        // PARAMETERS SECTION
        ImGui::Text("Parameters");
        ImGui::Separator();
        
        bool changed = false;
        
        // FRACTAL TYPE DROPDOWN (at the top!)
        const char* fractal_names[] = {
            "3D Mandelbrot",
            "3D Julia", 
            "Mandelbulb",
            "Mandelbox",
            "Apollonian Gasket",
            "Menger Sponge",
            "Sierpinski Pyramid"
        };
        int current_fractal = params.fractal_type;
        if (ImGui::Combo("Fractal Type", &current_fractal, fractal_names, 7)) {
            params.fractal_type = current_fractal;
            changed = true;
        }
        ImGui::Separator();
        
        // FRACTAL-SPECIFIC PARAMETERS (conditional)
        if (params.fractal_type == 1) { // 3D Julia
            if (ImGui::CollapsingHeader("Julia Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::SliderFloat("Julia CX", &params.julia_cx, -2.0f, 2.0f);
                changed |= ImGui::SliderFloat("Julia CY", &params.julia_cy, -2.0f, 2.0f);
                changed |= ImGui::SliderFloat("Julia CZ", &params.julia_cz, -2.0f, 2.0f);
            }
        }
        else if (params.fractal_type == 2) { // Mandelbulb
            if (ImGui::CollapsingHeader("Mandelbulb Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::SliderFloat("Power", &params.mandelbulb_power, 2.0f, 20.0f);
            }
        }
        else if (params.fractal_type == 3) { // Mandelbox
            if (ImGui::CollapsingHeader("Mandelbox Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::SliderFloat("Scale", &params.mandelbox_scale, -5.0f, 5.0f);
                changed |= ImGui::SliderFloat("Folding Limit", &params.mandelbox_folding, 0.1f, 5.0f);
            }
        }
        else if (params.fractal_type == 4) { // Apollonian
            if (ImGui::CollapsingHeader("Apollonian Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::SliderFloat("Scale", &params.apollonian_scale, 1.0f, 10.0f);
            }
        }
        else if (params.fractal_type == 5) { // Menger
            if (ImGui::CollapsingHeader("Menger Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::SliderFloat("Scale", &params.menger_scale, 2.0f, 5.0f);
            }
        }
        else if (params.fractal_type == 6) { // Sierpinski
            if (ImGui::CollapsingHeader("Sierpinski Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::SliderFloat("Scale", &params.sierpinski_scale, 1.5f, 3.0f);
            }
        }
        
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::SliderFloat("Cam X", &params.cx, -10.0f, 10.0f);
            changed |= ImGui::SliderFloat("Cam Y", &params.cy, -10.0f, 10.0f);
            changed |= ImGui::SliderFloat("Cam Z", &params.cz, -10.0f, 10.0f);
            changed |= ImGui::SliderFloat("Target X", &params.tx, -5.0f, 5.0f);
            changed |= ImGui::SliderFloat("Target Y", &params.ty, -5.0f, 5.0f);
            changed |= ImGui::SliderFloat("Target Z", &params.tz, -5.0f, 5.0f);
            changed |= ImGui::SliderFloat("FOV", &params.fov, 10.0f, 120.0f);
        }
        
        if (ImGui::CollapsingHeader("Light")) {
            changed |= ImGui::SliderFloat("Light X", &params.lx, -20.0f, 20.0f);
            changed |= ImGui::SliderFloat("Light Y", &params.ly, -20.0f, 20.0f);
            changed |= ImGui::SliderFloat("Light Z", &params.lz, -20.0f, 20.0f);
            changed |= ImGui::SliderFloat("Light Radius", &params.lradius, 0.0f, 2.0f);
        }
        
        if (ImGui::CollapsingHeader("Material")) {
            changed |= ImGui::SliderFloat("Specular Intensity", &params.specular_intensity, 0.0f, 2.0f);
            changed |= ImGui::SliderFloat("Shininess", &params.shininess, 1.0f, 128.0f);
            changed |= ImGui::Checkbox("Metallic", (bool*)&params.metallic);
            ImGui::TextDisabled("Metallic: specular uses base color instead of white");
        }
        
        if (ImGui::CollapsingHeader("Fractal Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::SliderInt("Max Iterations", &params.max_iter, 10, 200);
            changed |= ImGui::SliderInt("Step Count (Ray March)", &params.max_steps, 1000, 50000, "%d steps");
            changed |= ImGui::SliderFloat("Step Size", &params.step_size, 0.0001f, 0.01f, "%.4f");
            changed |= ImGui::SliderFloat("Escape", &params.escape, 1.0f, 100.0f);
            changed |= ImGui::SliderFloat("Scale", &params.scale, 0.1f, 5.0f);
            changed |= ImGui::SliderFloat("Del Less", &params.del_less, 0.0f, 50.0f);
            changed |= ImGui::SliderFloat("Del Greater", &params.del_greater, 10.0f, 200.0f);
            changed |= ImGui::Checkbox("Hollow", (bool*)&params.hollow);
        }
        
        if (ImGui::CollapsingHeader("Color Ramp", ImGuiTreeNodeFlags_DefaultOpen)) {
            draw_color_ramp(params.color_ramp, params.del_less, params.del_greater, changed);
            
            // Reset button and Preset dropdown
            if (ImGui::Button("Reset to Default Gradient")) {
                params.color_ramp.init_default();
                changed = true;
            }
            
            ImGui::SameLine();
            
            // Preset dropdown
            static std::vector<std::string> presets;
            static bool presets_loaded = false;
            if (!presets_loaded) {
                presets = get_color_ramp_presets();
                presets_loaded = true;
            }
            
            if (ImGui::Button("Presets ▼")) {
                presets = get_color_ramp_presets(); // Refresh list
                ImGui::OpenPopup("ColorRampPresets");
            }
            
            if (ImGui::BeginPopup("ColorRampPresets")) {
                ImGui::Text("Load Preset:");
                ImGui::Separator();
                
                for (const auto& preset : presets) {
                    if (ImGui::MenuItem(preset.c_str())) {
                        if (load_color_ramp_preset(params.color_ramp, preset.c_str())) {
                            changed = true;
                        }
                    }
                }
                
                if (presets.empty()) {
                    ImGui::TextDisabled("No saved presets");
                }
                
                ImGui::Separator();
                
                // Save current as preset
                static char new_preset_name[64] = "";
                ImGui::InputText("New Preset Name", new_preset_name, sizeof(new_preset_name));
                if (ImGui::Button("Save Current") && strlen(new_preset_name) > 0) {
                    save_color_ramp_preset(params.color_ramp, new_preset_name);
                    presets = get_color_ramp_presets(); // Refresh
                    new_preset_name[0] = '\0';
                }
                
                ImGui::EndPopup();
            }
        }
        
        if (ImGui::CollapsingHeader("Floor")) {
            changed |= ImGui::Checkbox("Enable Floor", (bool*)&params.floor_enable);
            changed |= ImGui::SliderFloat("Floor Y", &params.floor_y, -10.0f, 5.0f);
            changed |= ImGui::SliderFloat("Checker Size", &params.checker_size, 0.1f, 5.0f);
        }
        
        // If parameters changed and at keyframe, update it
        if (changed && has_keyframe(proj, proj.current_frame)) {
            set_keyframe(proj, proj.current_frame, params);
            needs_render = true;
        } else if (changed) {
            needs_render = true;
        }
        
        ImGui::Separator();
        if (ImGui::Button("Reset to Defaults", ImVec2(150, 30))) {
            params = default_params();
            needs_render = true;
        }
        
        ImGui::EndChild();
        ImGui::End();
        
        // Render Still Image Modal
        if (show_render_still_modal) {
            ImGui::OpenPopup("Render Still Image");
        }
        
        static int still_width = 1920;
        static int still_height = 1080;
        static char still_filename[256] = "output/render_still.png";
        
        // High quality render settings (persist across modal opens)
        static int aa_samples = 4;           // 1, 4, 9, 16 (1x, 2x, 3x, 4x)
        static int shadow_samples = 16;      // 1, 8, 16, 32, 64
        static int ao_samples = 16;          // 0, 8, 16, 32
        static int enable_dof = 0;           // 0 or 1
        static float aperture_size = 0.1f;   // 0.0 - 1.0
        static float focal_distance = 5.0f;  // Focus plane distance
        static int enable_tonemap = 1;       // 0 or 1
        static float exposure = 1.0f;        // 0.1 - 5.0
        static float contrast = 1.0f;        // 0.5 - 2.0 (1.0 = neutral)
        static int is_rendering_hq = 0;
        
        if (ImGui::BeginPopupModal("Render Still Image", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputInt("Width", &still_width);
            ImGui::InputInt("Height", &still_height);
            ImGui::InputText("Filename", still_filename, sizeof(still_filename));
            
            ImGui::Separator();
            ImGui::Text("Quality Settings");
            
            // Anti-aliasing
            const char* aa_names[] = {"1x (Off)", "2x (4 samples)", "3x (9 samples)", "4x (16 samples)"};
            int aa_index = 0;
            if (aa_samples == 4) aa_index = 1;
            else if (aa_samples == 9) aa_index = 2;
            else if (aa_samples == 16) aa_index = 3;
            if (ImGui::Combo("Anti-Aliasing", &aa_index, aa_names, 4)) {
                aa_samples = (aa_index == 0) ? 1 : (aa_index == 1) ? 4 : (aa_index == 2) ? 9 : 16;
            }
            
            // Soft shadows
            const char* shadow_names[] = {"Hard (1 sample)", "Soft (8 samples)", "Very Soft (16 samples)", 
                                          "Ultra Soft (32 samples)", "Maximum (64 samples)"};
            int shadow_index = 0;
            if (shadow_samples == 8) shadow_index = 1;
            else if (shadow_samples == 16) shadow_index = 2;
            else if (shadow_samples == 32) shadow_index = 3;
            else if (shadow_samples == 64) shadow_index = 4;
            if (ImGui::Combo("Shadow Quality", &shadow_index, shadow_names, 5)) {
                shadow_samples = (shadow_index == 0) ? 1 : (shadow_index == 1) ? 8 : 
                                 (shadow_index == 2) ? 16 : (shadow_index == 3) ? 32 : 64;
            }
            
            // Ambient occlusion
            const char* ao_names[] = {"Off", "Low (8 samples)", "Medium (16 samples)", "High (32 samples)"};
            int ao_index = 0;
            if (ao_samples == 8) ao_index = 1;
            else if (ao_samples == 16) ao_index = 2;
            else if (ao_samples == 32) ao_index = 3;
            if (ImGui::Combo("Ambient Occlusion", &ao_index, ao_names, 4)) {
                ao_samples = (ao_index == 0) ? 0 : (ao_index == 1) ? 8 : (ao_index == 2) ? 16 : 32;
            }
            
            ImGui::Separator();
            ImGui::Text("Depth of Field");
            ImGui::Checkbox("Enable DOF", (bool*)&enable_dof);
            if (enable_dof) {
                ImGui::SliderFloat("Aperture Size", &aperture_size, 0.0f, 1.0f);
                ImGui::SliderFloat("Focal Distance", &focal_distance, 0.1f, 20.0f);
            }
            
            ImGui::Separator();
            ImGui::Text("Tone Mapping");
            ImGui::Checkbox("Enable ACES Tonemap", (bool*)&enable_tonemap);
            ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
            ImGui::SliderFloat("Contrast", &contrast, 0.5f, 2.0f);
            ImGui::TextDisabled("1.0 = neutral, <1 = flat, >1 = punchy");
            
            ImGui::Separator();
            
            if (is_rendering_hq) {
                ImGui::Text("Rendering... (check terminal for progress)");
                ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(200, 0));
                
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    is_rendering_hq = 0;
                }
            } else {
                if (ImGui::Button("Render High Quality", ImVec2(150, 0))) {
                    is_rendering_hq = 1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Fast Render", ImVec2(120, 0))) {
                    export_image(&cl_ctx, &params, still_width, still_height, still_filename);
                    show_render_still_modal = false;
                    ImGui::CloseCurrentPopup();
                    snprintf(render_status, sizeof(render_status), "Fast render complete: %s", still_filename);
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                    show_render_still_modal = false;
                    is_rendering_hq = 0;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        
        // Handle high quality rendering outside modal
        if (is_rendering_hq) {
            is_rendering_hq = 0;
            show_render_still_modal = false;
            ImGui::CloseCurrentPopup();
            
            export_image_high_quality(
                &cl_ctx, &params, still_width, still_height, still_filename,
                aa_samples, shadow_samples, ao_samples,
                enable_dof, aperture_size, focal_distance,
                enable_tonemap, exposure, contrast
            );
            
            snprintf(render_status, sizeof(render_status), "High quality render complete: %s", still_filename);
        }
        
        // Render Animation Modal
        if (show_render_anim_modal) {
            ImGui::OpenPopup("Render Animation");
        }
        
        // Static vars for FFmpeg options (persist across modal opens)
        static bool create_mp4_checkbox = false;
        static char mp4_output[256] = "output/animation.mp4";
        
        if (ImGui::BeginPopupModal("Render Animation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputInt("Start Frame", &proj.start_frame);
            ImGui::InputInt("End Frame", &proj.end_frame);
            ImGui::InputInt("Width", &proj.render_width);
            ImGui::InputInt("Height", &proj.render_height);
            
            char pattern[256];
            strncpy(pattern, proj.output_pattern.c_str(), sizeof(pattern) - 1);
            pattern[sizeof(pattern) - 1] = '\0';
            if (ImGui::InputText("Output Pattern", pattern, sizeof(pattern))) {
                proj.output_pattern = pattern;
            }
            ImGui::TextDisabled("Use %%04d for frame number (e.g., render_%%04d.png)");
            
            ImGui::Separator();
            
            // FFmpeg option
            ImGui::Checkbox("Create MP4 with FFmpeg (H.264 High Quality)", &create_mp4_checkbox);
            if (create_mp4_checkbox) {
                ImGui::InputText("MP4 Filename", mp4_output, sizeof(mp4_output));
                ImGui::TextDisabled("FFmpeg will run after all frames are rendered");
                ImGui::TextDisabled("Settings: libx264, preset slow, CRF 17 (high quality)");
            }
            
            ImGui::Separator();
            
            if (is_rendering || is_encoding_mp4) {
                ImGui::Text("Status: %s", render_status);
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    is_rendering = false;
                    is_encoding_mp4 = false;
                }
            } else {
                if (ImGui::Button("Start Render", ImVec2(120, 0))) {
                    create_mp4 = create_mp4_checkbox;
                    if (create_mp4) {
                        strncpy(mp4_filename, mp4_output, sizeof(mp4_filename) - 1);
                        mp4_filename[sizeof(mp4_filename) - 1] = '\0';
                    }
                    is_rendering = true;
                    render_current_frame = proj.start_frame;
                }
                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(120, 0))) {
                    show_render_anim_modal = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        
        // Save Project Modal
        if (show_save_modal) {
            ImGui::OpenPopup("Save Project");
        }
        
        if (ImGui::BeginPopupModal("Save Project", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Filename", project_filename, sizeof(project_filename));
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (save_project(proj, project_filename)) {
                    show_save_modal = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_save_modal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        // Load Project Modal
        if (show_load_modal) {
            ImGui::OpenPopup("Load Project");
        }
        
        if (ImGui::BeginPopupModal("Load Project", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Filename", project_filename, sizeof(project_filename));
            if (ImGui::Button("Load", ImVec2(120, 0))) {
                if (load_project(proj, project_filename)) {
                    params = interpolate_params(proj, proj.current_frame);
                    needs_render = true;
                    show_load_modal = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_load_modal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        // Render preview if needed
        if (needs_render) {
            render_fractal(&cl_ctx, &params, preview_float);
            float_to_bytes(preview_float, preview_bytes, PREVIEW_WIDTH, PREVIEW_HEIGHT);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, preview_bytes);
            needs_render = false;
        }
        
        // Render to screen
        ImGui::Render();
        glViewport(0, 0, window_width, window_height);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    
    // Cleanup
    free(preview_float);
    free(preview_bytes);
    glDeleteTextures(1, &texture);
    
    clReleaseMemObject(cl_ctx.color_buffer);
    clReleaseMemObject(cl_ctx.depth_buffer);
    clReleaseKernel(cl_ctx.kernel);
    clReleaseProgram(cl_ctx.program);
    clReleaseCommandQueue(cl_ctx.queue);
    clReleaseContext(cl_ctx.context);
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}
