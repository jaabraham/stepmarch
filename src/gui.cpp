// StepMarch GUI - Interactive Fractal Renderer
// Houdini-style interface with real-time preview

#define CL_TARGET_OPENCL_VERSION 300

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <CL/cl.h>

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

// PNG export (reuse existing code)
extern "C" {
#include "png_writer.h"
}

#define PREVIEW_WIDTH 512
#define PREVIEW_HEIGHT 384

// Parameter structure matching the kernel
typedef struct {
    // Camera
    float cx, cy, cz;
    float tx, ty, tz;
    float fov;
    
    // Light
    float lx, ly, lz;
    float lradius;
    
    // Fractal
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
    
    // Floor
    float floor_y;
    int floor_enable;
    float checker_size;
} Params;

// Default parameters
Params default_params() {
    Params p = {};
    p.cx = 3.0f; p.cy = 2.0f; p.cz = 4.0f;
    p.tx = 0.0f; p.ty = 0.0f; p.tz = 0.0f;
    p.fov = 60.0f;
    p.lx = 5.0f; p.ly = 8.0f; p.lz = 5.0f;
    p.lradius = 0.5f;
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
    p.floor_y = -2.0f;
    p.floor_enable = 1;
    p.checker_size = 1.0f;
    return p;
}

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

// Load kernel source
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

// Initialize OpenCL
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

// Render fractal
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

// Convert float RGB to GL texture data
void float_to_bytes(float* input, unsigned char* output, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        float r = input[i * 3 + 0];
        float g = input[i * 3 + 1];
        float b = input[i * 3 + 2];
        
        // Tone mapping
        r = r / (1.0f + r);
        g = g / (1.0f + g);
        b = b / (1.0f + b);
        
        // Gamma correction
        r = powf(r, 1.0f / 2.2f);
        g = powf(g, 1.0f / 2.2f);
        b = powf(b, 1.0f / 2.2f);
        
        output[i * 4 + 0] = (unsigned char)(fmaxf(0, fminf(255, r * 255)));
        output[i * 4 + 1] = (unsigned char)(fmaxf(0, fminf(255, g * 255)));
        output[i * 4 + 2] = (unsigned char)(fmaxf(0, fminf(255, b * 255)));
        output[i * 4 + 3] = 255;
    }
}

// Export high-res image
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

int main(int argc, char** argv) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    
    // GL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    // Create window
    int window_width = 1280;
    int window_height = 720;
    SDL_Window* window = SDL_CreateWindow("StepMarch - Interactive Fractal Renderer",
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
    
    // Initialize OpenCL for preview
    OpenCLContext cl_ctx;
    if (init_opencl(&cl_ctx, PREVIEW_WIDTH, PREVIEW_HEIGHT) != 0) {
        fprintf(stderr, "Failed to initialize OpenCL\n");
        return 1;
    }
    
    // Parameters
    Params params = default_params();
    Params prev_params = params;
    
    // Preview buffer
    float* preview_float = (float*)malloc(PREVIEW_WIDTH * PREVIEW_HEIGHT * 3 * sizeof(float));
    unsigned char* preview_bytes = (unsigned char*)malloc(PREVIEW_WIDTH * PREVIEW_HEIGHT * 4);
    
    // Create GL texture
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Initial render
    render_fractal(&cl_ctx, &params, preview_float);
    float_to_bytes(preview_float, preview_bytes, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PREVIEW_WIDTH, PREVIEW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, preview_bytes);
    
    bool needs_render = false;
    bool show_export_modal = false;
    int export_width = 1920;
    int export_height = 1080;
    char export_filename[256] = "output/render.png";
    bool is_rendering = false;
    
    // Main loop
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        
        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        // Main layout
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)window_width, (float)window_height));
        ImGui::Begin("StepMarch", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        
        // Left panel - Preview
        ImGui::BeginChild("Preview", ImVec2(window_width * 0.65f, 0), true);
        ImGui::Text("Preview (%dx%d)", PREVIEW_WIDTH, PREVIEW_HEIGHT);
        ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(PREVIEW_WIDTH, PREVIEW_HEIGHT));
        
        if (ImGui::Button("Refresh Preview", ImVec2(150, 30))) {
            needs_render = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Export High-Res", ImVec2(150, 30))) {
            show_export_modal = true;
        }
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Right panel - Parameters
        ImGui::BeginChild("Parameters", ImVec2(0, 0), true);
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
        
        ImGui::Separator();
        if (ImGui::Button("Reset to Defaults", ImVec2(150, 30))) {
            params = default_params();
            changed = true;
        }
        
        ImGui::EndChild();
        ImGui::End();
        
        // Export modal
        if (show_export_modal) {
            ImGui::OpenPopup("Export High-Resolution");
        }
        
        if (ImGui::BeginPopupModal("Export High-Resolution", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputInt("Width", &export_width);
            ImGui::InputInt("Height", &export_height);
            ImGui::InputText("Filename", export_filename, sizeof(export_filename));
            
            if (is_rendering) {
                ImGui::Text("Rendering...");
            } else {
                if (ImGui::Button("Render", ImVec2(120, 0))) {
                    is_rendering = true;
                    export_image(&cl_ctx, &params, export_width, export_height, export_filename);
                    is_rendering = false;
                    show_export_modal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    show_export_modal = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        
        // Render if parameters changed
        if (changed || needs_render) {
            render_fractal(&cl_ctx, &params, preview_float);
            float_to_bytes(preview_float, preview_bytes, PREVIEW_WIDTH, PREVIEW_HEIGHT);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, preview_bytes);
            needs_render = false;
        }
        
        // Render
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
