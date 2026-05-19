#version 330 core

in vec4 iNormal;
in vec4 iFragPos;
in vec2 iTexCoord;
in vec3 iViewPosVec; 
in float iHeight;

out vec4 pixelColor;

uniform sampler2D tex;
uniform int isLamp; 

#define MAX_LIGHTS 4
uniform vec4 lightPositions[MAX_LIGHTS];
uniform float soundVolume;

const vec3 SKY_COLOR   = vec3(0.18, 0.20, 0.28);
const vec3 GROUND_COLOR= vec3(0.08, 0.07, 0.05);
const vec3 LAMP_COLOR  = vec3(1.00, 0.92, 0.75);
const float SPECULAR_STRENGTH = 0.35;
const float SHININESS         = 32.0;

bool rayAABB(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax, float maxDist) {
    vec3 invDir = 1.0 / (rd + vec3(1e-6));
    vec3 t0 = (boxMin - ro) * invDir;
    vec3 t1 = (boxMax - ro) * invDir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tNear = max(max(tmin.x, tmin.y), tmin.z);
    float tFar = min(min(tmax.x, tmax.y), tmax.z);
    return tNear <= tFar && tFar > 0.0 && tNear < maxDist;
}

void main(void) {
    vec3 n = normalize(iNormal.xyz);
    
    vec2 finalTexCoord = iTexCoord;
    if (abs(n.y) > 0.5) {
        finalTexCoord = iFragPos.xz * 0.25; 
    }
    
    vec4 texColor = texture(tex, finalTexCoord);
    vec3 albedo   = texColor.rgb;

    if (isLamp == 1) {
        float emissive = mix(0.1, 1.5, clamp(soundVolume, 0.0, 1.0));
        pixelColor = vec4(albedo * emissive, texColor.a);
        return;
    } else if (isLamp == 2) {
        pixelColor = vec4(albedo * 1.4, texColor.a);
        return;
    }

    float camDist = length(iViewPosVec);
    vec3 viewDir = iViewPosVec / camDist;

    float heightFactor = smoothstep(-0.4, 2.0, iHeight); 
    float normalFactor = clamp(n.y * 0.4 + 0.6, 0.0, 1.0); 
    float ao = mix(0.35, 1.0, heightFactor * normalFactor); 
    
    float edgeX = smoothstep(8.5, 10.0, abs(iFragPos.x));
    float edgeZ = smoothstep(8.5, 10.0, abs(iFragPos.z));
    float cornerFade = 1.0 - (edgeX * edgeZ * 0.3);
    ao *= cornerFade;

    vec3 hemiAmbient = mix(GROUND_COLOR, SKY_COLOR, (dot(n, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5));
    float ambientScale = mix(0.08, 1.0, clamp(soundVolume, 0.0, 1.0));
    vec3 ambient = hemiAmbient * ambientScale;

    vec3 diffuseTotal  = vec3(0.0);
    vec3 specularTotal = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++) {
        vec3  lightVec  = lightPositions[i].xyz - iFragPos.xyz;
        float dist      = length(lightVec);
        
        // --- ZMIANA 1: Kubełkowy limit odległości dla światła (Izolacja Pokoi) ---
        // Odległość od centrum pokoju do jego narożnika to ~8m. Do sąsiedniego > 10m.
        // Jeśli światło wędruje dalej niż 9.5 metra, natężenie spada do 0.
        float distanceLimit = 1.0 - smoothstep(7.5, 9.5, dist);
        
        // Jeśli światło fizycznie tu nie dociera, ignorujemy je całkowicie! (Wielki skok wydajności)
        if (distanceLimit <= 0.0) continue; 
        
        vec3  lightDir  = normalize(lightVec);
        float diff = clamp(dot(n, lightDir), 0.0, 1.0);
        vec3  halfDir = normalize(lightDir + viewDir);
        float spec    = pow(clamp(dot(n, halfDir), 0.0, 1.0), SHININESS);

        float attenuation = 1.0 / (1.0 + 0.07 * dist + 0.02 * dist * dist);
        
        vec3 ro = lightPositions[i].xyz;
        vec3 rd = -lightDir;            
        float maxDist = dist - 0.01;    
        
        float targetShadow = 1.0;       
        
        // --- ZMIANA 2: Wymuszenie rzucania pełnych cieni przez obiekty w danym pokoju ---
        if (rayAABB(ro, rd, vec3(3.7, 0.0, 4.3), vec3(6.3, 0.8, 5.7), maxDist)) targetShadow = 0.25;        
        else if (rayAABB(ro, rd, vec3(3.75, 0.0, -8.75), vec3(6.25, 2.0, -7.25), maxDist)) targetShadow = 0.25; 
        else if (rayAABB(ro, rd, vec3(4.3, 0.0, -5.4), vec3(5.7, 1.2, -4.6), maxDist)) targetShadow = 0.25;    
        else if (rayAABB(ro, rd, vec3(-8.75, 0.0, 3.25), vec3(-7.25, 1.5, 4.75), maxDist)) targetShadow = 0.25; 
        else if (rayAABB(ro, rd, vec3(-5.6, 0.0, 5.4), vec3(-3.4, 2.3, 7.6), maxDist)) targetShadow = 0.25;    
        else if (rayAABB(ro, rd, vec3(-8.15, 0.0, -9.4), vec3(-2.25, 3.5, -8.6), maxDist)) targetShadow = 0.25; 
        else if (rayAABB(ro, rd, vec3(-9.4, 0.0, -8.15), vec3(-8.6, 3.5, -3.45), maxDist)) targetShadow = 0.25; 
        
        // Ściany blokują światło w 100%
        else if (rayAABB(ro, rd, vec3(-4.0, 0.0, -0.25), vec3(4.0, 4.0, 0.25), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-0.25, 0.0, -4.0), vec3(0.25, 4.0, 4.0), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-10.0, 0.0, -0.25), vec3(-6.0, 4.0, 0.25), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(6.0, 0.0, -0.25), vec3(10.0, 4.0, 0.25), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-0.25, 0.0, -10.0), vec3(0.25, 4.0, -6.0), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-0.25, 0.0, 6.0), vec3(0.25, 4.0, 10.0), maxDist)) targetShadow = 0.0;
        
        // Płynne gaśnięcie cieni, gdy wyłącza się prąd
        float shadowBlend = smoothstep(0.1, 0.8, soundVolume);
        float shadowFactor = mix(1.0, targetShadow, shadowBlend);

        // Limitujemy ogólną jasność światła naszą nową zmienną distanceLimit!
        float power = attenuation * distanceLimit * soundVolume * 0.85 * shadowFactor;

        diffuseTotal  += LAMP_COLOR * diff * power;
        specularTotal += LAMP_COLOR * spec * power * SPECULAR_STRENGTH;
    }

    float fresnelFactor = pow(1.0 - clamp(dot(n, viewDir), 0.0, 1.0), 3.0);
    vec3 fresnelColor = SKY_COLOR * fresnelFactor * ao * ambientScale * 0.4;

    vec3 finalColor = (ambient + diffuseTotal) * albedo * ao + specularTotal + fresnelColor;

    float fogDensity = 0.06;
    float fogFactor = exp(-pow(camDist * fogDensity, 2.0));
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    vec3 fogColor = GROUND_COLOR * ambientScale; 
        
    finalColor = mix(fogColor, finalColor, fogFactor);

    finalColor = pow(clamp(finalColor, 0.0, 1.0), vec3(1.0 / 2.2));
    pixelColor = vec4(finalColor, texColor.a);
}