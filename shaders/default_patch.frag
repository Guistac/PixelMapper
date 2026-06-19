#version 150
in vec3 vPixelPos3D;
in vec2 vPixelPos2D;
out vec4 fragColor;
#define iPixelPos3D vPixelPos3D
#define iPixelPos2D vPixelPos2D
uniform float time;
uniform vec2 resolution;

void main() {
    float speed = 0.5;
    float sweep = mod(time * speed, 1.0);
    float bar   = smoothstep(sweep - 0.04, sweep, iPixelPos2D.x)
                - smoothstep(sweep, sweep + 0.04, iPixelPos2D.x);
    float bg = 0.05;
    float brightness = bg + bar * (1.0 - bg);
    fragColor = vec4(brightness, brightness, brightness * 0.8, 1.0);
}
