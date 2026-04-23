#version 330 core

in vec4 iNormal;
in vec4 iFragPos;
in vec2 iTexCoord;

out vec4 pixelColor;

uniform sampler2D tex;
uniform int isLamp; 

#define MAX_LIGHTS 4
uniform vec4 lightPositions[MAX_LIGHTS]; 

// Zmienna sterowana przez mikrofon
uniform float soundVolume;

void main(void) {
    vec4 texColor = texture(tex, iTexCoord);
    
    // 1. LAMPA: Świeci własnym światłem (nie reaguje na soundVolume, bo to źródło)
    if (isLamp == 1) {
        pixelColor = vec4(texColor.rgb * 1.2, texColor.a); 
        return;
    }

    // 2. ŚCIANY/SUFIT: 
    vec3 n = normalize(iNormal.xyz);
    
    // Ambient - stałe, słabe światło, żeby nie było całkiem czarno
    vec3 ambient = vec3(0.2); 
    vec3 diffuseTotal = vec3(0.0);
    
    for(int i = 0; i < MAX_LIGHTS; i++) {
        // Najpierw liczymy wektory i odległości
        vec3 lightVec = lightPositions[i].xyz - iFragPos.xyz;
        float dist = length(lightVec); 
        vec3 lightDir = normalize(lightVec);
        
        // Kąt padania (Lambert)
        float diff = clamp(dot(n, lightDir), 0.0, 1.0);
        
        // Tłumienie (zanikanie z odległością)
        float attenuation = 1.0 / (1.0 + 0.14 * dist + 0.07 * (dist * dist));
        
        // SUMOWANIE ŚWIATŁA:
        // Tutaj aplikujemy soundVolume, żeby głośność wpływała na moc lampy
        diffuseTotal += vec3(1.0) * diff * attenuation * 0.7 * soundVolume; 
    }
    
    // Ostateczny kolor: (Światło tła + Suma lamp) * tekstura
    vec3 finalColor = (ambient + diffuseTotal) * texColor.rgb;
    pixelColor = vec4(finalColor, texColor.a);
}