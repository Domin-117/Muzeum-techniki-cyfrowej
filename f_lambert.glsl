/**
 * @file f_lambert.frag
 * @brief Fragment shader do zaawansowanego oświetlenia sceny.
 * 
 * Odpowiada za obliczanie finalnego koloru piksela, w tym: oświetlenie Blinn-Phong,
 * proceduralne cienie oparte na przecięciach promieni (Ray-AABB), proceduralną 
 * okluzję otoczenia (AO), efekt oświetlenia półsferycznego (Hemispheric Ambient), 
 * mgłę dystansową oraz korekcję gamma.
 */

#version 330 core

/** @brief Znormalizowany wektor normalny fragmentu z przestrzeni świata. */
in vec4 iNormal;

/** @brief Pozycja fragmentu w przestrzeni świata. */
in vec4 iFragPos;

/** @brief Współrzędne teksturowania (z uwzględnieniem Box Mappingu z vertex shadera). */
in vec2 iTexCoord;

/** @brief Nieznormalizowany wektor od fragmentu w kierunku kamery. */
in vec3 iViewPosVec; 

/** @brief Wysokość fragmentu w świecie (oś Y). */
in float iHeight;

/** @brief Finalny kolor piksela wyjściowego. */
out vec4 pixelColor;

/** @brief Tekstura przypisana do rysowanego materiału. */
uniform sampler2D tex;

/** @brief Flaga określająca właściwości emisyjne (1 = żarówka reagująca na dźwięk, 2 = dioda/ekran, 0 = zwykły obiekt). */
uniform int isLamp; 

#define MAX_LIGHTS 4
/** @brief Tablica pozycji źródeł światła w świecie. */
uniform vec4 lightPositions[MAX_LIGHTS];

/** @brief Zmienna sterująca jasnością świateł na podstawie rozpoznanej komendy głosowej / klawisza. */
uniform float soundVolume;

// Stałe oświetleniowe sceny
const vec3 SKY_COLOR   = vec3(0.18, 0.20, 0.28); ///< Kolor światła docierającego z góry (niebo)
const vec3 GROUND_COLOR= vec3(0.08, 0.07, 0.05); ///< Kolor światła odbitego od ziemi
const vec3 LAMP_COLOR  = vec3(1.00, 0.92, 0.75); ///< Barwa światła sztucznego (żarówki)
const float SPECULAR_STRENGTH = 0.35;            ///< Siła odblasków (Specular)
const float SHININESS         = 32.0;            ///< Skupienie odblasku (połyskliwość)

/**
 * @brief Wykonuje test przecięcia promienia z prostopadłościanem (AABB).
 * 
 * Funkcja używana do generowania analitycznych, rzucanych cieni (Faux-Raytracing).
 * Sprawdza, czy światło na drodze do piksela natrafia na przeszkodę.
 * 
 * @param ro Pozycja początkowa promienia (źródło światła).
 * @param rd Znormalizowany wektor kierunku promienia (w stronę piksela).
 * @param boxMin Minimalne koordynaty (XYZ) prostopadłościanu kolizyjnego.
 * @param boxMax Maksymalne koordynaty (XYZ) prostopadłościanu kolizyjnego.
 * @param maxDist Maksymalny dystans sprawdzenia (dystans od światła do piksela).
 * @return Wartość logiczna - true jeśli promień przecina box przed dotarciem do piksela.
 */
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

/**
 * @brief Główna logika obliczania koloru dla każdego fragmentu (piksela na ekranie).
 */
void main(void) {
    vec3 n = normalize(iNormal.xyz);
    
    // Ustalenie ostatecznych koordynatów tekstury
    vec2 finalTexCoord = iTexCoord;
    if (abs(n.y) > 0.5) {
        // Skalowanie UV dla powierzchni poziomych (podłoga/sufit) dla lepszego wyglądu płytek
        finalTexCoord = iFragPos.xz * 0.25; 
    }
    
    vec4 texColor = texture(tex, finalTexCoord);
    vec3 albedo   = texColor.rgb;

    // Szybkie wyjście dla materiałów emisyjnych (brak obliczeń oświetlenia)
    if (isLamp == 1) {
        float emissive = mix(0.1, 1.5, clamp(soundVolume, 0.0, 1.0));
        pixelColor = vec4(albedo * emissive, texColor.a);
        return;
    } else if (isLamp == 2) {
        pixelColor = vec4(albedo * 1.4, texColor.a);
        return;
    }

    // Obliczenia wektorów i dystansu względem kamery
    float camDist = length(iViewPosVec);
    vec3 viewDir = iViewPosVec / camDist;

    // Proceduralne Ambient Occlusion (Zacienienie otoczenia)
    float heightFactor = smoothstep(-0.4, 2.0, iHeight); 
    float normalFactor = clamp(n.y * 0.4 + 0.6, 0.0, 1.0); 
    float ao = mix(0.35, 1.0, heightFactor * normalFactor); 
    
    // Przyciemnianie rogów pokoi
    float edgeX = smoothstep(8.5, 10.0, abs(iFragPos.x));
    float edgeZ = smoothstep(8.5, 10.0, abs(iFragPos.z));
    float cornerFade = 1.0 - (edgeX * edgeZ * 0.3);
    ao *= cornerFade;

    // Oświetlenie półsferyczne (Hemispheric Ambient) symulujące rozproszone światło globalne
    vec3 hemiAmbient = mix(GROUND_COLOR, SKY_COLOR, (dot(n, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5));
    float ambientScale = mix(0.08, 1.0, clamp(soundVolume, 0.0, 1.0));
    vec3 ambient = hemiAmbient * ambientScale;

    // Inicjalizacja akumulatorów światła
    vec3 diffuseTotal  = vec3(0.0);
    vec3 specularTotal = vec3(0.0);

    // Iteracja po wszystkich źródłach światła (lampach sufitowych)
    for (int i = 0; i < MAX_LIGHTS; i++) {
        vec3  lightVec  = lightPositions[i].xyz - iFragPos.xyz;
        float dist      = length(lightVec);
        
        // ZMIANA 1: Kubełkowy limit odległości dla światła (Izolacja Pokoi)
        // Odległość od centrum pokoju do jego narożnika to ~8m. Do sąsiedniego > 10m.
        // Jeśli światło wędruje dalej niż 9.5 metra, natężenie płynnie spada do 0.
        float distanceLimit = 1.0 - smoothstep(7.5, 9.5, dist);
        
        // Jeśli światło fizycznie tu nie dociera, ignorujemy je całkowicie (Wielki skok wydajności)
        if (distanceLimit <= 0.0) continue; 
        
        vec3  lightDir  = normalize(lightVec);
        
        // Komponent rozproszony (Diffuse - Lambert)
        float diff = clamp(dot(n, lightDir), 0.0, 1.0);
        
        // Komponent kierunkowy/lustrzany (Specular - Blinn-Phong)
        vec3  halfDir = normalize(lightDir + viewDir);
        float spec    = pow(clamp(dot(n, halfDir), 0.0, 1.0), SHININESS);

        // Naturalny spadek jasności światła wraz z odległością
        float attenuation = 1.0 / (1.0 + 0.07 * dist + 0.02 * dist * dist);
        
        // Konfiguracja promienia rzucanego ze źródła światła do piksela w celu weryfikacji cieni
        vec3 ro = lightPositions[i].xyz;
        vec3 rd = -lightDir;            
        float maxDist = dist - 0.01;    
        
        float targetShadow = 1.0; // 1.0 = pełne oświetlenie, mniejsza wartość = w cieniu      
        
        // ZMIANA 2: Wymuszenie rzucania analitycznych cieni przez zdefiniowane bryły
        // Cienie sprzętów w pokojach (półprzepuszczalne / miękkie z racji ograniczeń silnika)
        if (rayAABB(ro, rd, vec3(3.7, 0.0, 4.3), vec3(6.3, 0.8, 5.7), maxDist)) targetShadow = 0.25;        
        else if (rayAABB(ro, rd, vec3(3.75, 0.0, -8.75), vec3(6.25, 2.0, -7.25), maxDist)) targetShadow = 0.25; 
        else if (rayAABB(ro, rd, vec3(4.3, 0.0, -5.4), vec3(5.7, 1.2, -4.6), maxDist)) targetShadow = 0.25;    
        else if (rayAABB(ro, rd, vec3(-8.75, 0.0, 3.25), vec3(-7.25, 1.5, 4.75), maxDist)) targetShadow = 0.25; 
        else if (rayAABB(ro, rd, vec3(-5.6, 0.0, 5.4), vec3(-3.4, 2.3, 7.6), maxDist)) targetShadow = 0.25;    
        else if (rayAABB(ro, rd, vec3(-8.15, 0.0, -9.4), vec3(-2.25, 3.5, -8.6), maxDist)) targetShadow = 0.25; 
        else if (rayAABB(ro, rd, vec3(-9.4, 0.0, -8.15), vec3(-8.6, 3.5, -3.45), maxDist)) targetShadow = 0.25; 
        
        // Ściany blokują światło całkowicie w 100% (targetShadow = 0.0)
        else if (rayAABB(ro, rd, vec3(-4.0, 0.0, -0.25), vec3(4.0, 4.0, 0.25), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-0.25, 0.0, -4.0), vec3(0.25, 4.0, 4.0), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-10.0, 0.0, -0.25), vec3(-6.0, 4.0, 0.25), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(6.0, 0.0, -0.25), vec3(10.0, 4.0, 0.25), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-0.25, 0.0, -10.0), vec3(0.25, 4.0, -6.0), maxDist)) targetShadow = 0.0;
        else if (rayAABB(ro, rd, vec3(-0.25, 0.0, 6.0), vec3(0.25, 4.0, 10.0), maxDist)) targetShadow = 0.0;
        
        // Płynne gaśnięcie cieni, gdy wyłącza się prąd (oświetlenie zanika = cienie blakną)
        float shadowBlend = smoothstep(0.1, 0.8, soundVolume);
        float shadowFactor = mix(1.0, targetShadow, shadowBlend);

        // Końcowa moc danego światła
        float power = attenuation * distanceLimit * soundVolume * 0.85 * shadowFactor;

        diffuseTotal  += LAMP_COLOR * diff * power;
        specularTotal += LAMP_COLOR * spec * power * SPECULAR_STRENGTH;
    }

    // Dodatek odbicia Fresnela (dla krawędzi obiektów pod kątem do kamery)
    float fresnelFactor = pow(1.0 - clamp(dot(n, viewDir), 0.0, 1.0), 3.0);
    vec3 fresnelColor = SKY_COLOR * fresnelFactor * ao * ambientScale * 0.4;

    // Skompletowanie oświetlenia
    vec3 finalColor = (ambient + diffuseTotal) * albedo * ao + specularTotal + fresnelColor;

    // Mgła dystansowa (odcinająca ostrość w dali)
    float fogDensity = 0.06;
    float fogFactor = exp(-pow(camDist * fogDensity, 2.0));
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    vec3 fogColor = GROUND_COLOR * ambientScale; 
        
    finalColor = mix(fogColor, finalColor, fogFactor);

    // Korekcja Gamma przed zapisaniem do piksela ekranu
    finalColor = pow(clamp(finalColor, 0.0, 1.0), vec3(1.0 / 2.2));
    pixelColor = vec4(finalColor, texColor.a);
}