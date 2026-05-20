/**
 * @file v_lambert.vert
 * @brief Vertex shader do oświetlenia Lamberta z proceduralnym mapowaniem tekstur (Box Mapping).
 * 
 * Odpowiada za transformację geometrii do przestrzeni ekranu, przekształcenie 
 * wektorów normalnych do przestrzeni świata, wyliczenie wektora kierunku na kamerę
 * oraz automatyczne nałożenie współrzędnych teksturowania opartych na pozycji (Triplanar/Box mapping).
 */

#version 330 core

/** @brief Lokalna pozycja wierzchołka w przestrzeni modelu. */
layout (location = 0) in vec4 vertex;   

/** @brief Lokalny wektor normalny wierzchołka. */
layout (location = 1) in vec4 normal;   

/** @brief Bazowe współrzędne teksturowania (są ignorowane i nadpisywane przez box mapping). */
layout (location = 2) in vec2 texCoord; 

/** @brief Macierz rzutowania (Projection - perspektywa). */
uniform mat4 P; 

/** @brief Macierz widoku (View - reprezentująca pozycję/orientację kamery). */
uniform mat4 V; 

/** @brief Macierz modelu (Model - transformacja obiektu w świecie). */
uniform mat4 M; 

/** @brief Znormalizowany wektor normalny w przestrzeni świata. */
out vec4 iNormal;

/** @brief Pozycja fragmentu (wierzchołka) w przestrzeni świata. */
out vec4 iFragPos;

/** @brief Proceduralnie wyliczone współrzędne teksturowania (UV). */
out vec2 iTexCoord;

/** @brief Nieznormalizowany wektor od wierzchołka w kierunku kamery (użyteczny do efektów View-Dependent). */
out vec3 iViewPosVec; 

/** @brief Wysokość wierzchołka w świecie (oś Y) - może służyć do efektów mgły gradientowej. */
out float iHeight;   

/**
 * @brief Główna funkcja vertex shadera.
 */
void main(void) {
    // 1. Obliczenie pozycji wierzchołka w przestrzeni świata
    vec4 worldPos = M * vertex;
    
    // 2. Transformacja wierzchołka na ekran (Mnożenie przez macierze View i Projection)
    gl_Position = P * V * worldPos;
    
    // Przekazanie pozycji w świecie do fragment shadera
    iFragPos = worldPos;

    // 3. Transformacja wektora normalnego do przestrzeni świata
    // Użycie transpozycji macierzy odwrotnej chroni przed zniekształceniami przy nierównomiernym skalowaniu
    mat3 normalMatrix = transpose(inverse(mat3(M)));
    vec3 worldNormal = normalize(normalMatrix * normal.xyz);
    iNormal = vec4(worldNormal, 0.0);

    // 4. Wyliczenie wektora w stronę kamery
    // Ekstrakcja pozycji kamery w świecie z odwróconej macierzy widoku
    vec3 camPos = vec3(inverse(V)[3]);
    // Wektor wskazuje OD wierzchołka DO kamery
    iViewPosVec = camPos - worldPos.xyz;

    // Przekazanie wysokości wprost
    iHeight = worldPos.y;

    // 5. Box mapping (Triplanar mapping)
    // Dynamiczne generowanie koordynatów tekstury
    if (abs(worldNormal.y) > 0.5) {
        iTexCoord = iFragPos.xz * 0.5;
    } else if (abs(worldNormal.x) > 0.5) {
        iTexCoord = iFragPos.zy * 0.5;
    } else {
        iTexCoord = iFragPos.xy * 0.5;
    }
}