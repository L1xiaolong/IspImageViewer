#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D rawTexture;
layout(std140, binding = 4) uniform BayerParameters {
    vec4 imageAndStorage; // width, height, row stride bytes, format: RAW10/RAW12/RAW16
    vec4 layoutFlags;     // CFA pattern, little endian, MSB aligned, valid bits
    vec4 levelsAndGamma;  // black level, white level, display gamma, orientation
    vec4 whiteBalance;    // R, G, B, reserved
    mat3 colorCorrection;
};

int byteAt(int byteX, int y) {
    return int(round(texelFetch(rawTexture, ivec2(byteX, y), 0).r * 255.0));
}

int rawValueAt(ivec2 pixel) {
    int format = int(round(imageAndStorage.w));
    int x = pixel.x;
    int y = pixel.y;
    if (format == 0) {
        int group = (x / 4) * 5;
        int lane = x % 4;
        int high = byteAt(group + lane, y);
        int low = (byteAt(group + 4, y) >> (lane * 2)) & 3;
        return (high << 2) | low;
    }
    if (format == 1) {
        int group = (x / 2) * 3;
        int lane = x % 2;
        int high = byteAt(group + lane, y);
        int low = (byteAt(group + 2, y) >> (lane * 4)) & 15;
        return (high << 4) | low;
    }

    int offset = x * 2;
    int first = byteAt(offset, y);
    int second = byteAt(offset + 1, y);
    int value = layoutFlags.y > 0.5 ? (first | (second << 8)) : ((first << 8) | second);
    int validBits = int(round(layoutFlags.w));
    if (validBits < 16) {
        value = layoutFlags.z > 0.5 ? value >> (16 - validBits)
                                     : value & ((1 << validBits) - 1);
    }
    return value;
}

int cfaChannel(ivec2 pixel) {
    bool evenX = (pixel.x & 1) == 0;
    bool evenY = (pixel.y & 1) == 0;
    int pattern = int(round(layoutFlags.x));
    if (pattern == 0) { // RGGB
        return evenY ? (evenX ? 0 : 1) : (evenX ? 1 : 2);
    }
    if (pattern == 1) { // GRBG
        return evenY ? (evenX ? 1 : 0) : (evenX ? 2 : 1);
    }
    if (pattern == 2) { // GBRG
        return evenY ? (evenX ? 1 : 2) : (evenX ? 0 : 1);
    }
    return evenY ? (evenX ? 2 : 1) : (evenX ? 1 : 0); // BGGR
}

float normalizedRaw(ivec2 pixel) {
    float denominator = levelsAndGamma.y - levelsAndGamma.x;
    return clamp((float(rawValueAt(pixel)) - levelsAndGamma.x) / denominator, 0.0, 1.0);
}

vec2 sourceUvForOrientation(vec2 displayUv) {
    int orientation = int(round(levelsAndGamma.w));
    if (orientation == 1) {
        return vec2(displayUv.y, 1.0 - displayUv.x);
    }
    if (orientation == 2) {
        return vec2(1.0 - displayUv.x, 1.0 - displayUv.y);
    }
    if (orientation == 3) {
        return vec2(1.0 - displayUv.y, displayUv.x);
    }
    return displayUv;
}

void main() {
    ivec2 imageSize = ivec2(round(imageAndStorage.xy));
    vec2 sourceUv = sourceUvForOrientation(uv);
    ivec2 center =
        clamp(ivec2(floor(sourceUv * vec2(imageSize))), ivec2(0), imageSize - ivec2(1));
    if (whiteBalance.w < 0.5) {
        float encoded = pow(normalizedRaw(center), 1.0 / levelsAndGamma.z);
        fragColor = vec4(vec3(encoded), 1.0);
        return;
    }
    vec3 sums = vec3(0.0);
    vec3 counts = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 samplePixel = clamp(center + ivec2(dx, dy), ivec2(0), imageSize - ivec2(1));
            int channel = cfaChannel(samplePixel);
            sums[channel] += normalizedRaw(samplePixel);
            counts[channel] += 1.0;
        }
    }
    vec3 balanced = (sums / counts) * whiteBalance.rgb;
    vec3 corrected = clamp(colorCorrection * balanced, 0.0, 1.0);
    vec3 encoded = pow(corrected, vec3(1.0 / levelsAndGamma.z));
    fragColor = vec4(encoded, 1.0);
}
