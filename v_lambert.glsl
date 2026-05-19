#version 330 core

layout (location = 0) in vec4 vertex;   
layout (location = 1) in vec4 normal;   
layout (location = 2) in vec2 texCoord; 

uniform mat4 P; 
uniform mat4 V; 
uniform mat4 M; 

out vec4 iNormal;
out vec4 iFragPos;
out vec2 iTexCoord;
out vec3 iViewPosVec; // ZMIANA: przesyłamy wektor bez normalizacji
out float iHeight;   

void main(void) {
    vec4 worldPos = M * vertex;
    gl_Position = P * V * worldPos;
    iFragPos = worldPos;

    mat3 normalMatrix = transpose(inverse(mat3(M)));
    vec3 worldNormal = normalize(normalMatrix * normal.xyz);
    iNormal = vec4(worldNormal, 0.0);

    // Pozycja kamery i nieskalibrowany wektor w stronę kamery
    vec3 camPos = vec3(inverse(V)[3]);
    iViewPosVec = camPos - worldPos.xyz;

    iHeight = worldPos.y;

    // Box mapping
    if (abs(worldNormal.y) > 0.5) {
        iTexCoord = iFragPos.xz * 0.5;
    } else if (abs(worldNormal.x) > 0.5) {
        iTexCoord = iFragPos.zy * 0.5;
    } else {
        iTexCoord = iFragPos.xy * 0.5;
    }
}