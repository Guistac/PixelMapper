#include "CanvasBinding.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace PixelMapper {

// 3D Perlin Noise tables and helper structures
namespace {
    int p[512];
    bool p_initialized = false;

    void init_p() {
        if (p_initialized) return;
        static const int permutation[256] = {
            151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
            190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,
            20,125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,
            230,220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,
            169,200,196,135,130,116,188,189,125,111,241,98,3,85,252,146,236,141,110,72,180,242,120,101,178,
            136,54,223,50,224,193,124,78,8,150,46,129,115,31,79,139,127,167,43,121,99,163,9,147,22,243,136,
            244,28,254,81,120,191,246,202,217,204,59,115,235,90,206,172,115,10,186,22,38,208,125,111,214,152,
            191,128,105,119,166,223,19,71,73,236,224,21,242,169,187,24,150,50,252,141,128,103,148,252,140,205,
            130,137,85,40,84,251,155,167,244,142,120,41,90,148,24,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29
        };
        for (int i = 0; i < 256; i++) {
            p[i] = permutation[i];
            p[256 + i] = permutation[i];
        }
        p_initialized = true;
    }

    double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    double lerp(double t, double a, double b) { return a + t * (b - a); }
    double grad(int hash, double x, double y, double z) {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
}

Canvas::Canvas(int width, int height, ColorRGBW* pixels)
    : m_width(width), m_height(height), m_pixels(pixels) {
    init_p();
}

void Canvas::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t w_color) {
    if (!m_pixels) return;
    int size = m_width * m_height;
    for (int i = 0; i < size; i++) {
        m_pixels[i] = {r, g, b, w_color};
    }
}

void Canvas::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color) {
    if (!m_pixels || x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    m_pixels[y * m_width + x] = {r, g, b, w_color};
}

void Canvas::draw_line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color, int thickness) {
    if (thickness <= 1) {
        int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
        int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
        int err = dx + dy, e2;
        while (true) {
            set_pixel(x1, y1, r, g, b, w_color);
            if (x1 == x2 && y1 == y2) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    } else {
        int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
        int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
        int err = dx + dy, e2;
        int radius = thickness / 2;
        while (true) {
            draw_circle(x1, y1, radius, r, g, b, w_color, true);
            if (x1 == x2 && y1 == y2) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    }
}

void Canvas::draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color, bool fill) {
    if (fill) {
        for (int dy = 0; dy < h; dy++) {
            for (int dx = 0; dx < w; dx++) {
                set_pixel(x + dx, y + dy, r, g, b, w_color);
            }
        }
    } else {
        for (int dx = 0; dx < w; dx++) {
            set_pixel(x + dx, y, r, g, b, w_color);
            set_pixel(x + dx, y + h - 1, r, g, b, w_color);
        }
        for (int dy = 0; dy < h; dy++) {
            set_pixel(x, y + dy, r, g, b, w_color);
            set_pixel(x + w - 1, y + dy, r, g, b, w_color);
        }
    }
}

void Canvas::draw_circle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t w_color, bool fill) {
    if (radius < 0) return;
    if (fill) {
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                if (x*x + y*y <= radius*radius) {
                    set_pixel(cx + x, cy + y, r, g, b, w_color);
                }
            }
        }
    } else {
        int x = radius;
        int y = 0;
        int err = 0;
        while (x >= y) {
            set_pixel(cx + x, cy + y, r, g, b, w_color);
            set_pixel(cx + y, cy + x, r, g, b, w_color);
            set_pixel(cx - y, cy + x, r, g, b, w_color);
            set_pixel(cx - x, cy + y, r, g, b, w_color);
            set_pixel(cx - x, cy - y, r, g, b, w_color);
            set_pixel(cx - y, cy - x, r, g, b, w_color);
            set_pixel(cx + y, cy - x, r, g, b, w_color);
            set_pixel(cx + x, cy - y, r, g, b, w_color);
            y += 1;
            if (err <= 0) {
                err += 2 * y + 1;
            }
            if (err > 0) {
                x -= 1;
                err -= 2 * x + 1;
            }
        }
    }
}

double Canvas::noise(double x, double y, double z) {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    int Z = (int)std::floor(z) & 255;
    
    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);
    
    double u = fade(x);
    double v = fade(y);
    double w = fade(z);
    
    int A = p[X] + Y;
    int AA = p[A & 255] + Z;
    int AB = p[(A + 1) & 255] + Z;
    int B = p[(X + 1) & 255] + Y;
    int BA = p[B & 255] + Z;
    int BB = p[(B + 1) & 255] + Z;
    
    return lerp(w, lerp(v, lerp(u, grad(p[AA & 255], x, y, z),
                                    grad(p[BA & 255], x - 1, y, z)),
                            lerp(u, grad(p[AB & 255], x, y - 1, z),
                                    grad(p[BB & 255], x - 1, y - 1, z))),
                    lerp(v, lerp(u, grad(p[(AA + 1) & 255], x, y, z - 1),
                                    grad(p[(BA + 1) & 255], x - 1, y, z - 1)),
                            lerp(u, grad(p[(AB + 1) & 255], x, y - 1, z - 1),
                                    grad(p[(BB + 1) & 255], x - 1, y - 1, z - 1))));
}

} // namespace PixelMapper
