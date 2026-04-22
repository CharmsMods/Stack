#version 430 core

layout(location = 0) out vec4 oColor;

in vec2 vUv;

uniform sampler2D uBaseTexture;
uniform sampler2D uObjectIdTexture;
uniform int uSelectedObjectId;
uniform ivec2 uResolution;

int decodeObjectId(vec4 encoded) {
    ivec3 bytes = ivec3(round(encoded.rgb * 255.0));
    return bytes.x | (bytes.y << 8) | (bytes.z << 16);
}

bool isSelected(vec2 uv) {
    return decodeObjectId(texture(uObjectIdTexture, uv)) == uSelectedObjectId;
}

void main() {
    vec3 baseColor = texture(uBaseTexture, vUv).rgb;
    if (uSelectedObjectId < 0 || uResolution.x <= 0 || uResolution.y <= 0) {
        oColor = vec4(baseColor, 1.0);
        return;
    }

    vec2 texel = 1.0 / vec2(uResolution);
    bool centerSelected = isSelected(vUv);
    bool edge = false;

    for (int y = -1; y <= 1 && !edge; ++y) {
        for (int x = -1; x <= 1; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }

            vec2 neighborUv = clamp(vUv + vec2(x, y) * texel, vec2(0.0), vec2(1.0));
            bool neighborSelected = isSelected(neighborUv);
            if (neighborSelected != centerSelected) {
                edge = true;
                break;
            }
        }
    }

    vec3 finalColor = baseColor;
    if (edge) {
        vec3 outlineColor = vec3(1.0, 0.84, 0.33);
        finalColor = mix(baseColor, outlineColor, centerSelected ? 0.95 : 0.65);
    }

    oColor = vec4(finalColor, 1.0);
}
