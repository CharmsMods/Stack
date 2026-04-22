#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in float aMaterialIndex;
layout(location = 4) in vec3 aTint;
layout(location = 5) in float aObjectId;

uniform mat4 uViewProjection;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vUv;
out vec3 vTint;
flat out int vMaterialIndex;
flat out int vObjectId;

void main() {
    vWorldPosition = aPosition;
    vWorldNormal = aNormal;
    vUv = aUv;
    vTint = aTint;
    vMaterialIndex = int(aMaterialIndex + 0.5);
    vObjectId = int(aObjectId + 0.5);
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
