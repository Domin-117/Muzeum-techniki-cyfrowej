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

void main(void) {
    gl_Position = P * V * M * vertex;
    iFragPos = M * vertex;
    
    // Obliczamy normalną w przestrzeni świata
    mat3 normalMatrix = transpose(inverse(mat3(M)));
    vec3 worldNormal = normalize(normalMatrix * normal.xyz);
    iNormal = vec4(worldNormal, 0.0);
    
    // --- NIEZAWODNY BOX MAPPING ---
    // Używamy pozycji wierzchołka w świecie (iFragPos) zamiast texCoord
    // Dzięki temu tekstura zawsze pasuje do wymiarów ściany w metrach!
    
    if (abs(worldNormal.y) > 0.5) {
        // Podłoga / Sufit (płaszczyzna XZ)
        iTexCoord = iFragPos.xz;
    } else if (abs(worldNormal.x) > 0.5) {
        // Ściany boczne patrzace w boki (płaszczyzna ZY)
        iTexCoord = iFragPos.zy;
    } else {
        // Ściany patrzace przód/tył (płaszczyzna XY)
        iTexCoord = iFragPos.xy;
    }
}