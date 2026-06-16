#version 150
in vec3 vPixelPos3D;
in vec2 vPixelPos2D;
out vec4 fragColor;
#define iPixelPos3D vPixelPos3D
#define iPixelPos2D vPixelPos2D

uniform float time;
uniform vec2 resolution;

void main() {
    float x = iPixelPos2D.x * 10.0;
    float y = iPixelPos2D.y * 10.0;
    
    float v1 = sin(x + time);
    float v2 = sin(10.0 * (x * sin(time / 2.0) + y * cos(time / 3.0)) + time);
    
    float cx = x + 5.0 * sin(time / 5.0);
    float cy = y + 5.0 * cos(time / 3.0);
    float v3 = sin(sqrt(cx*cx + cy*cy + 1.0) - time);
    
    float v = (v1 + v2 + v3) / 3.0;
    
    float r = sin(v * 3.1415) * 0.5 + 0.5;
    float g = sin(v * 3.1415 + 2.094) * 0.5 + 0.5;
    float b = sin(v * 3.1415 + 4.188) * 0.5 + 0.5;
    
    fragColor = vec4(r, g, b, 1.0);
}
