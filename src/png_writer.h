#ifndef PNG_WRITER_H
#define PNG_WRITER_H

#include <stdint.h>
#include <stdbool.h>

// RGB float color
typedef struct {
    float r;
    float g;
    float b;
} Color3f;

// Image buffer
typedef struct {
    int width;
    int height;
    Color3f* pixels;  // Row-major order
} Image;

// Create a new image
Image* image_create(int width, int height);

// Free image memory
void image_free(Image* img);

// Set a pixel color
void image_set_pixel(Image* img, int x, int y, float r, float g, float b);

// Save image as PNG
bool image_save_png(const Image* img, const char* filename);

// Save raw float data (for debugging)
bool image_save_raw(const Image* img, const char* filename);

#endif // PNG_WRITER_H
