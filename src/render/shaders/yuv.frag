#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D yTexture;
layout(binding = 2) uniform sampler2D uTexture;
layout(binding = 3) uniform sampler2D vTexture;
layout(std140, binding = 4) uniform YuvParameters {
    vec4 coefficients; // redV, greenU, greenV, blueU
    vec4 ranges;       // yOffset, yScale, chromaCenter, chromaScale
    vec4 flags;        // planar, swapUV, orientation, reserved
};

vec2 sourceUvForOrientation(vec2 displayUv) {
    float orientation = flags.z;
    if (orientation > 0.5 && orientation < 1.5) {
        return vec2(displayUv.y, 1.0 - displayUv.x);
    }
    if (orientation > 1.5 && orientation < 2.5) {
        return vec2(1.0 - displayUv.x, 1.0 - displayUv.y);
    }
    if (orientation > 2.5) {
        return vec2(1.0 - displayUv.y, displayUv.x);
    }
    return displayUv;
}

void main() {
    vec2 sourceUv = sourceUvForOrientation(uv);
    float y = texture(yTexture, sourceUv).r;
    vec2 chroma;
    if (flags.x > 0.5) {
        chroma = vec2(texture(uTexture, sourceUv).r, texture(vTexture, sourceUv).r);
    } else {
        chroma = texture(uTexture, sourceUv).rg;
        if (flags.y > 0.5) {
            chroma = chroma.yx;
        }
    }
    float luma = (y - ranges.x) / ranges.y;
    vec2 centered = (chroma - vec2(ranges.z)) / ranges.w;
    vec3 rgb = vec3(
        luma + coefficients.x * centered.y,
        luma - coefficients.y * centered.x - coefficients.z * centered.y,
        luma + coefficients.w * centered.x
    );
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
