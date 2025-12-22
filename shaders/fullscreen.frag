#version 430 core

in vec2 vUV;
out vec4 FragColor;

uniform vec2 uResolution;

void main() {
    // Simple test: color based on UV/pixel position
    vec2 uv = vUV;

    // Optional: visualize resolution dependence
    vec2 pixel = uv * uResolution;
    vec3 color = vec3(
        uv.x,
        uv.y,
        0.25 + 0.75 * sin(0.01 * pixel.x)
    );

    FragColor = vec4(color, 1.0);
}
