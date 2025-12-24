#version 430 core

in vec2 vUV;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D sRenderTexture;
uniform uint uFrameIndex;

vec3 acesTonemap(vec3 x) {
    // Simple ACES-approx tonemap [web:180][web:189]
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 accum = texture(sRenderTexture, vUV).rgb;
    float frames = max(float(uFrameIndex), 1.0);
    vec3 color = accum / frames;          // average

    float exposure = 1.5;                 // tweak between ~0.8–2.5
    color *= exposure;

    color = acesTonemap(color);
    color = pow(color, vec3(1.0 / 2.2));  // gamma

    FragColor = vec4(color, 1.0);
}
