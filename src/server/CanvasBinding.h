#pragma once
#include <stdint.h>
#include "shared/Common.h"

namespace PixelMapper {

class Canvas {
private:
    int m_width;
    int m_height;
    ColorRGBW* m_pixels;

public:
    Canvas(int width, int height, ColorRGBW* pixels);

    int width() const { return m_width; }
    int height() const { return m_height; }

    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t w_color);
    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color);

    void draw_line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color, int thickness);
    void draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color, bool fill);
    void draw_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color, bool fill);

    double noise(double x, double y, double z);
};

} // namespace PixelMapper
