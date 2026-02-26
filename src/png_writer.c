#include "png_writer.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Image* image_create(int width, int height) {
    Image* img = (Image*)malloc(sizeof(Image));
    if (!img) return NULL;
    
    img->width = width;
    img->height = height;
    img->pixels = (Color3f*)calloc(width * height, sizeof(Color3f));
    
    if (!img->pixels) {
        free(img);
        return NULL;
    }
    
    return img;
}

void image_free(Image* img) {
    if (img) {
        free(img->pixels);
        free(img);
    }
}

void image_set_pixel(Image* img, int x, int y, float r, float g, float b) {
    if (!img || x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    
    int idx = y * img->width + x;
    img->pixels[idx].r = r;
    img->pixels[idx].g = g;
    img->pixels[idx].b = b;
}

// Tone mapping helper - simple Reinhard
static float tone_map(float x) {
    return x / (1.0f + x);
}

// Gamma correction
static float gamma_correct(float x, float gamma) {
    if (x <= 0.0f) return 0.0f;
    return powf(x, 1.0f / gamma);
}

bool image_save_png(const Image* img, const char* filename) {
    if (!img || !filename) return false;
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return false;
    }
    
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return false;
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return false;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }
    
    png_init_io(png, fp);
    
    // Write header
    png_set_IHDR(png, info, img->width, img->height,
                 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png, info);
    
    // Allocate row pointers
    png_bytep row = (png_bytep)malloc(3 * img->width * sizeof(png_byte));
    if (!row) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }
    
    // Write pixel data
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            int idx = y * img->width + x;
            Color3f p = img->pixels[idx];
            
            // Tone mapping for HDR values
            float r = tone_map(p.r);
            float g = tone_map(p.g);
            float b = tone_map(p.b);
            
            // Gamma correction (gamma 2.2)
            r = gamma_correct(r, 2.2f);
            g = gamma_correct(g, 2.2f);
            b = gamma_correct(b, 2.2f);
            
            // Clamp to 0-255
            row[x * 3 + 0] = (png_byte)(fmaxf(0.0f, fminf(255.0f, r * 255.0f)));
            row[x * 3 + 1] = (png_byte)(fmaxf(0.0f, fminf(255.0f, g * 255.0f)));
            row[x * 3 + 2] = (png_byte)(fmaxf(0.0f, fminf(255.0f, b * 255.0f)));
        }
        png_write_row(png, row);
    }
    
    png_write_end(png, NULL);
    
    free(row);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    
    printf("Saved image to %s (%dx%d)\n", filename, img->width, img->height);
    return true;
}

bool image_save_raw(const Image* img, const char* filename) {
    if (!img || !filename) return false;
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) return false;
    
    // Write dimensions
    fwrite(&img->width, sizeof(int), 1, fp);
    fwrite(&img->height, sizeof(int), 1, fp);
    
    // Write raw float data
    fwrite(img->pixels, sizeof(Color3f), img->width * img->height, fp);
    
    fclose(fp);
    return true;
}
