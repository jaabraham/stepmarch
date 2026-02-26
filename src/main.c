#define CL_TARGET_OPENCL_VERSION 300
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <CL/cl.h>
#include "png_writer.h"

#define KERNEL_FILE "src/fractal_step_marcher.cl"
#define DEFAULT_WIDTH 1024
#define DEFAULT_HEIGHT 768
#define DEFAULT_OUTPUT "output/mandelbrot3d.png"

typedef struct {
    float x, y, z;
} float3;

typedef struct {
    // Image settings
    int width;
    int height;
    char* output_file;
    
    // Camera
    float3 camera_origin;
    float3 camera_target;
    float3 camera_up;
    float fov;
    
    // Light
    float3 light_pos;
    float light_radius;
    
    // Fractal parameters
    float3 offset;
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
} Config;

// Default configuration (based on typical Houdini values)
Config default_config() {
    Config cfg;
    cfg.width = DEFAULT_WIDTH;
    cfg.height = DEFAULT_HEIGHT;
    cfg.output_file = strdup(DEFAULT_OUTPUT);
    
    // Camera
    cfg.camera_origin = (float3){3.0f, 2.0f, 4.0f};
    cfg.camera_target = (float3){0.0f, 0.0f, 0.0f};
    cfg.camera_up = (float3){0.0f, 1.0f, 0.0f};
    cfg.fov = 60.0f;
    
    // Light
    cfg.light_pos = (float3){5.0f, 8.0f, 5.0f};
    cfg.light_radius = 0.5f;
    
    // Fractal (3D Mandelbrot)
    cfg.offset = (float3){0.0f, 0.0f, 0.0f};
    cfg.scale = 1.0f;
    cfg.max_iter = 20;
    cfg.escape = 16.0f;
    cfg.step_size = 0.005f;
    cfg.dist_max = 10.0f;
    cfg.del_less = 6.0f;      // Only show iterations >= 6 (the "solid" part)
    cfg.del_greater = 100.0f; // Max iteration to show
    cfg.hollow = 0;
    cfg.seed_offset = 0;
    
    // Floor
    cfg.floor_y = -1.5f;
    cfg.floor_enable = 1;
    cfg.checker_size = 1.0f;
    
    return cfg;
}

void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\nImage Settings:\n");
    printf("  -w WIDTH      Image width (default: %d)\n", DEFAULT_WIDTH);
    printf("  -h HEIGHT     Image height (default: %d)\n", DEFAULT_HEIGHT);
    printf("  -o FILE       Output PNG file (default: %s)\n", DEFAULT_OUTPUT);
    
    printf("\nCamera:\n");
    printf("  -cx, -cy, -cz     Camera position (default: 3, 2, 4)\n");
    printf("  -tx, -ty, -tz     Camera target (default: 0, 0, 0)\n");
    printf("  -fov              Field of view (default: 60)\n");
    
    printf("\nLight:\n");
    printf("  -lx, -ly, -lz     Light position (default: 5, 8, 5)\n");
    printf("  -lradius          Light radius for soft shadows (default: 0.5)\n");
    
    printf("\nFractal Parameters:\n");
    printf("  -offsetx, -offsety, -offsetz  Fractal offset (default: 0, 0, 0)\n");
    printf("  -scale            Fractal scale (default: 1.0)\n");
    printf("  -maxiter          Max iterations (default: 20)\n");
    printf("  -escape           Escape radius squared (default: 16.0)\n");
    printf("  -stepsize         Ray march step size (default: 0.005)\n");
    printf("  -distmax          Max ray distance (default: 10.0)\n");
    printf("  -delless          Banding start (default: 0.0)\n");
    printf("  -delgreater       Banding end (default: 1000.0)\n");
    printf("  -hollow           Hollow mode 0/1 (default: 0)\n");
    printf("  -seed             Random seed offset (default: 0)\n");
    
    printf("\nFloor:\n");
    printf("  -floory           Floor plane Y position (default: -1.5)\n");
    printf("  -floor            Enable floor 0/1 (default: 1)\n");
    printf("  -checker          Checkerboard size (default: 1.0)\n");
    
    printf("\n--help            Show this help\n");
}

Config parse_args(int argc, char** argv) {
    Config cfg = default_config();
    
    for (int i = 1; i < argc; i++) {
        // Image
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) cfg.width = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) cfg.height = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            free(cfg.output_file);
            cfg.output_file = strdup(argv[++i]);
        }
        // Camera
        else if (strcmp(argv[i], "-cx") == 0 && i + 1 < argc) cfg.camera_origin.x = atof(argv[++i]);
        else if (strcmp(argv[i], "-cy") == 0 && i + 1 < argc) cfg.camera_origin.y = atof(argv[++i]);
        else if (strcmp(argv[i], "-cz") == 0 && i + 1 < argc) cfg.camera_origin.z = atof(argv[++i]);
        else if (strcmp(argv[i], "-tx") == 0 && i + 1 < argc) cfg.camera_target.x = atof(argv[++i]);
        else if (strcmp(argv[i], "-ty") == 0 && i + 1 < argc) cfg.camera_target.y = atof(argv[++i]);
        else if (strcmp(argv[i], "-tz") == 0 && i + 1 < argc) cfg.camera_target.z = atof(argv[++i]);
        else if (strcmp(argv[i], "-fov") == 0 && i + 1 < argc) cfg.fov = atof(argv[++i]);
        // Light
        else if (strcmp(argv[i], "-lx") == 0 && i + 1 < argc) cfg.light_pos.x = atof(argv[++i]);
        else if (strcmp(argv[i], "-ly") == 0 && i + 1 < argc) cfg.light_pos.y = atof(argv[++i]);
        else if (strcmp(argv[i], "-lz") == 0 && i + 1 < argc) cfg.light_pos.z = atof(argv[++i]);
        else if (strcmp(argv[i], "-lradius") == 0 && i + 1 < argc) cfg.light_radius = atof(argv[++i]);
        // Fractal
        else if (strcmp(argv[i], "-offsetx") == 0 && i + 1 < argc) cfg.offset.x = atof(argv[++i]);
        else if (strcmp(argv[i], "-offsety") == 0 && i + 1 < argc) cfg.offset.y = atof(argv[++i]);
        else if (strcmp(argv[i], "-offsetz") == 0 && i + 1 < argc) cfg.offset.z = atof(argv[++i]);
        else if (strcmp(argv[i], "-scale") == 0 && i + 1 < argc) cfg.scale = atof(argv[++i]);
        else if (strcmp(argv[i], "-maxiter") == 0 && i + 1 < argc) cfg.max_iter = atoi(argv[++i]);
        else if (strcmp(argv[i], "-escape") == 0 && i + 1 < argc) cfg.escape = atof(argv[++i]);
        else if (strcmp(argv[i], "-stepsize") == 0 && i + 1 < argc) cfg.step_size = atof(argv[++i]);
        else if (strcmp(argv[i], "-distmax") == 0 && i + 1 < argc) cfg.dist_max = atof(argv[++i]);
        else if (strcmp(argv[i], "-delless") == 0 && i + 1 < argc) cfg.del_less = atof(argv[++i]);
        else if (strcmp(argv[i], "-delgreater") == 0 && i + 1 < argc) cfg.del_greater = atof(argv[++i]);
        else if (strcmp(argv[i], "-hollow") == 0 && i + 1 < argc) cfg.hollow = atoi(argv[++i]);
        else if (strcmp(argv[i], "-seed") == 0 && i + 1 < argc) cfg.seed_offset = atoi(argv[++i]);
        // Floor
        else if (strcmp(argv[i], "-floory") == 0 && i + 1 < argc) cfg.floor_y = atof(argv[++i]);
        else if (strcmp(argv[i], "-floor") == 0 && i + 1 < argc) cfg.floor_enable = atoi(argv[++i]);
        else if (strcmp(argv[i], "-checker") == 0 && i + 1 < argc) cfg.checker_size = atof(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
    }
    
    return cfg;
}

char* read_kernel_source(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open kernel file: %s\n", filename);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* source = (char*)malloc(size + 1);
    if (!source) {
        fclose(f);
        return NULL;
    }
    
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);
    
    return source;
}

void check_error(cl_int err, const char* msg) {
    if (err != CL_SUCCESS) {
        fprintf(stderr, "OpenCL Error (%d): %s\n", err, msg);
        exit(1);
    }
}

int main(int argc, char** argv) {
    printf("=== 3D Mandelbrot GPU Step-Marcher ===\n");
    printf("Based on Houdini OpenCL implementation\n\n");
    
    Config cfg = parse_args(argc, argv);
    
    printf("Resolution: %dx%d\n", cfg.width, cfg.height);
    printf("Camera: (%.2f, %.2f, %.2f) looking at (%.2f, %.2f, %.2f)\n",
           cfg.camera_origin.x, cfg.camera_origin.y, cfg.camera_origin.z,
           cfg.camera_target.x, cfg.camera_target.y, cfg.camera_target.z);
    printf("Fractal: max_iter=%d, step_size=%.4f, scale=%.2f\n",
           cfg.max_iter, cfg.step_size, cfg.scale);
    printf("Floor: %s at y=%.2f\n", cfg.floor_enable ? "enabled" : "disabled", cfg.floor_y);
    
    // Read kernel source
    char* kernel_source = read_kernel_source(KERNEL_FILE);
    if (!kernel_source) return 1;
    
    // Get OpenCL platform and device
    cl_platform_id platform;
    cl_uint num_platforms;
    cl_int err = clGetPlatformIDs(1, &platform, &num_platforms);
    check_error(err, "Failed to get platform");
    
    cl_device_id device;
    cl_uint num_devices;
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0) {
        printf("No GPU found, trying CPU...\n");
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, &num_devices);
        check_error(err, "Failed to get device");
    }
    
    char device_name[256];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
    printf("Device: %s\n\n", device_name);
    
    // Create context and queue
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    check_error(err, "Failed to create context");
    
    cl_queue_properties props[] = {CL_QUEUE_PROPERTIES, 0, 0};
    cl_command_queue queue = clCreateCommandQueueWithProperties(context, device, props, &err);
    check_error(err, "Failed to create command queue");
    
    // Create and build program
    cl_program program = clCreateProgramWithSource(context, 1, 
                                                    (const char**)&kernel_source, 
                                                    NULL, &err);
    check_error(err, "Failed to create program");
    
    err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        char* log = (char*)malloc(log_size);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
        fprintf(stderr, "Build error:\n%s\n", log);
        free(log);
        return 1;
    }
    
    // Create kernel
    cl_kernel kernel = clCreateKernel(program, "render", &err);
    check_error(err, "Failed to create kernel");
    
    // Create buffers
    size_t color_buffer_size = cfg.width * cfg.height * 3 * sizeof(cl_float);
    size_t depth_buffer_size = cfg.width * cfg.height * sizeof(cl_float);
    
    cl_mem color_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, color_buffer_size, NULL, &err);
    check_error(err, "Failed to create color buffer");
    
    cl_mem depth_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, depth_buffer_size, NULL, &err);
    check_error(err, "Failed to create depth buffer");
    
    // Set kernel arguments
    cl_int width = cfg.width;
    cl_int height = cfg.height;
    cl_float3 cam_origin = {{cfg.camera_origin.x, cfg.camera_origin.y, cfg.camera_origin.z}};
    cl_float3 cam_target = {{cfg.camera_target.x, cfg.camera_target.y, cfg.camera_target.z}};
    cl_float3 cam_up = {{cfg.camera_up.x, cfg.camera_up.y, cfg.camera_up.z}};
    cl_float fov = cfg.fov;
    cl_float3 light = {{cfg.light_pos.x, cfg.light_pos.y, cfg.light_pos.z}};
    cl_float3 offset = {{cfg.offset.x, cfg.offset.y, cfg.offset.z}};
    cl_float scale = cfg.scale;
    cl_int max_iter = cfg.max_iter;
    cl_float escape = cfg.escape;
    cl_float step_size = cfg.step_size;
    cl_float dist_max = cfg.dist_max;
    cl_float del_less = cfg.del_less;
    cl_float del_greater = cfg.del_greater;
    cl_int hollow = cfg.hollow;
    cl_int seed_offset = cfg.seed_offset;
    cl_float floor_y = cfg.floor_y;
    cl_int floor_enable = cfg.floor_enable;
    cl_float checker_size = cfg.checker_size;
    cl_float light_radius = cfg.light_radius;
    
    int arg = 0;
    err  = clSetKernelArg(kernel, arg++, sizeof(cl_mem), &color_buffer);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &depth_buffer);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &width);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &height);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float3), &cam_origin);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float3), &cam_target);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float3), &cam_up);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &fov);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float3), &light);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float3), &offset);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &scale);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &max_iter);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &escape);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &step_size);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &dist_max);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &del_less);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &del_greater);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &hollow);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &seed_offset);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &floor_y);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &floor_enable);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &checker_size);
    err |= clSetKernelArg(kernel, arg++, sizeof(cl_float), &light_radius);
    check_error(err, "Failed to set kernel arguments");
    
    // Execute kernel
    printf("Rendering...\n");
    size_t global_work_size[2] = {cfg.width, cfg.height};
    
    err = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, 
                                  global_work_size, NULL, 
                                  0, NULL, NULL);
    check_error(err, "Failed to enqueue kernel");
    
    // Read back results
    cl_float* raw_color = (cl_float*)malloc(color_buffer_size);
    cl_float* raw_depth = (cl_float*)malloc(depth_buffer_size);
    
    err = clEnqueueReadBuffer(queue, color_buffer, CL_TRUE, 0,
                              color_buffer_size, raw_color, 0, NULL, NULL);
    check_error(err, "Failed to read color buffer");
    
    err = clEnqueueReadBuffer(queue, depth_buffer, CL_TRUE, 0,
                              depth_buffer_size, raw_depth, 0, NULL, NULL);
    check_error(err, "Failed to read depth buffer");
    
    printf("Render complete. Processing output...\n");
    
    // Convert to image and save
    Image* img = image_create(cfg.width, cfg.height);
    if (!img) {
        fprintf(stderr, "Failed to create image\n");
        return 1;
    }
    
    for (int y = 0; y < cfg.height; y++) {
        for (int x = 0; x < cfg.width; x++) {
            int idx = y * cfg.width + x;
            image_set_pixel(img, x, y, 
                           raw_color[idx * 3 + 0],
                           raw_color[idx * 3 + 1],
                           raw_color[idx * 3 + 2]);
        }
    }
    
    if (!image_save_png(img, cfg.output_file)) {
        fprintf(stderr, "Failed to save image\n");
        return 1;
    }
    
    printf("\nDone! Output saved to: %s\n", cfg.output_file);
    
    // Cleanup
    image_free(img);
    free(raw_color);
    free(raw_depth);
    free(kernel_source);
    
    clReleaseMemObject(color_buffer);
    clReleaseMemObject(depth_buffer);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    
    free(cfg.output_file);
    
    return 0;
}
