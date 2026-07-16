#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2D imageTexture;
layout(binding = 2) uniform sampler2D comparisonTexture;
layout(std140, binding = 3) uniform CompareParameters {
    vec4 compareParams;
    vec4 comparisonTransform;
};

void main() {
    vec4 source = texture(imageTexture, uv);
    vec2 comparisonUv = uv;
    bool comparisonInside = all(greaterThanEqual(comparisonUv, vec2(0.0))) &&
                            all(lessThanEqual(comparisonUv, vec2(1.0)));
    vec4 comparison = comparisonInside ? texture(comparisonTexture, comparisonUv) : vec4(0.0);
    int mode = int(compareParams.x + 0.5);
    if (mode == 1) {
        source = uv.x <= compareParams.y ? source : comparison;
    } else if (mode == 2) {
        source = uv.y <= compareParams.y ? source : comparison;
    }
    float seamDistance = mode == 1 ? abs(uv.x - compareParams.y) / max(fwidth(uv.x), 0.000001)
                                   : abs(uv.y - compareParams.y) / max(fwidth(uv.y), 0.000001);
    if (mode != 0 && seamDistance <= 1.0) {
        fragColor = vec4(1.0);
        return;
    }
    vec2 cell = floor(gl_FragCoord.xy / 10.0);
    float checker = mod(cell.x + cell.y, 2.0);
    vec3 background = mix(vec3(0.20), vec3(0.28), checker);
    fragColor = vec4(mix(background, source.rgb, source.a), 1.0);
}
