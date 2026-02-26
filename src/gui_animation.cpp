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
    
    char* src = load_kernel("src/fractal_step_marcher.cl");
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
    err |= clSetKernelArg(ctx->kernel, arg++, sizeof(float), &p->lradius);
    
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
    
    // Current parameters
    Params params = default_params();
    
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
    
    // Main loop
    bool running = true;
    while (running) {
        Uint32 current_time = SDL_GetTicks();
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
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
        
        // FFmpeg encoding state
        static bool create_mp4 = false;
        static bool is_encoding_mp4 = false;
        static char mp4_filename[256] = "output/animation.mp4";
        
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
                    params = default_params();
                    needs_render = true;
                }
                if (ImGui::MenuItem("Save Project...")) {
                    show_save_modal = true;
                }
                if (ImGui::MenuItem("Load Project...")) {
                    show_load_modal = true;
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
        
        if (ImGui::CollapsingHeader("Fractal", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::SliderInt("Max Iterations", &params.max_iter, 10, 200);
            changed |= ImGui::SliderFloat("Step Size", &params.step_size, 0.0001f, 0.01f, "%.4f");
            changed |= ImGui::SliderFloat("Escape", &params.escape, 1.0f, 100.0f);
            changed |= ImGui::SliderFloat("Scale", &params.scale, 0.1f, 5.0f);
            changed |= ImGui::SliderFloat("Del Less", &params.del_less, 0.0f, 50.0f);
            changed |= ImGui::SliderFloat("Del Greater", &params.del_greater, 10.0f, 200.0f);
            changed |= ImGui::Checkbox("Hollow", (bool*)&params.hollow);
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
        
        if (ImGui::BeginPopupModal("Render Still Image", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputInt("Width", &still_width);
            ImGui::InputInt("Height", &still_height);
            ImGui::InputText("Filename", still_filename, sizeof(still_filename));
            
            if (ImGui::Button("Render", ImVec2(120, 0))) {
                export_image(&cl_ctx, &params, still_width, still_height, still_filename);
                show_render_still_modal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                show_render_still_modal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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
                    strncpy(mp4_filename, mp4_output, sizeof(mp4_filename) - 1);
                    mp4_filename[sizeof(mp4_filename) - 1] = '\0';
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
