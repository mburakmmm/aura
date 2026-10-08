#ifndef AURA_PAINT_H
#define AURA_PAINT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int aura_paint_begin(int64_t width, int64_t height, double scale);
int aura_paint_clear(int64_t argb);
int aura_paint_rect(double x, double y, double w, double h, int64_t argb);
int aura_paint_text(double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family);
int aura_paint_opacity(double opacity);
int aura_paint_opacity_pop(void);
int aura_paint_image(int64_t image, double x, double y, double w, double h);
int aura_paint_end(void);
const uint8_t *aura_paint_pixels(void);
int64_t aura_paint_pixel_width(void);
int64_t aura_paint_pixel_height(void);
int64_t aura_paint_stride(void);

double aura_text_width(const char *text, double size, int64_t weight, const char *family);
double aura_text_height(double size, int64_t weight, const char *family);

int64_t aura_image_open_file(const char *path);
int64_t aura_image_buffer_reset(void);
int64_t aura_image_buffer_byte(int64_t value);
int64_t aura_image_buffer_commit(void);
int64_t aura_image_width(int64_t handle);
int64_t aura_image_height(int64_t handle);
int64_t aura_image_dispose(int64_t handle);

double aura_mono_time(void);

#ifdef __cplusplus
}
#endif

#endif
