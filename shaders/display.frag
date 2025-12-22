#version 430 core

in vec2 vUV;
out vec4 FragColor;

layout(binding = 0) uniform sampler2D sRenderTexture;
uniform uint uFrameIndex;

void main() {
    vec3 accum = texture(sRenderTexture, vUV).rgb;

    float frames = max(float(uFrameIndex), 1.0);
    vec3 color = accum / frames;

    // simple tone mapping + gamma
    float exposure = 1.0;
    color = vec3(1.0) - exp(-color * exposure);
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
