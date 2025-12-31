#version 430 core

in vec2 vUV;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D sRenderTexture;
uniform uint uFrameIndex;

// Improved ACES tonemap for spectral rendering
vec3 acesTonemap(vec3 x) {
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
    vec3 color = accum / frames;
    
    // ORIGINAL exposure that gave you the "charm"
    float exposure = 1.2;  // Back to sweet spot
    color *= exposure;
    
    // Keep ACES but adjust for sparkle preservation
    color = acesTonemap(color * 0.8);  // Slightly tame highlights
    color = pow(color, vec3(1.0 / 2.2));
    
    FragColor = vec4(color, 1.0);
}

