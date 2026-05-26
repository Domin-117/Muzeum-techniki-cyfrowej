/**
 * @file main_file.cpp
 * @brief Główny plik aplikacji "Muzeum Maszyn Cyfrowych".
 * @details Aplikacja renderuje wirtualne muzeum przy użyciu OpenGL (GLEW, GLFW).
 * Wykorzystuje GLM do operacji matematycznych 3D, LodePNG do tekstur, Miniaudio i Vosk
 * do rozpoznawania mowy (sterowanie oświetleniem) oraz Dear ImGui do interfejsu 2D.
 */

#define GLM_FORCE_RADIANS
#define MINIAUDIO_IMPLEMENTATION
#define _USE_MATH_DEFINES

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <atomic>

#include "constants.h"
#include "lodepng.h"
#include "shaderprogram.h"
#include "miniaudio.h"
#include "vosk_api.h"
#include "cylinder.h"
#include "gui/imgui.h"
#include "gui/imgui_impl_glfw.h"
#include "gui/imgui_impl_opengl3.h"

#pragma comment(lib, "winmm.lib")

 /// Globalny obiekt siatki cylindra.
Cylinder cylinderMesh;

/// Docelowa jasność oświetlenia (zmieniana głosem lub klawiszem).
std::atomic<float> targetBrightness(1.0f);

/// Aktualna, płynnie interpolowana jasność oświetlenia.
float currentBrightness = 1.0f;

/// Wskaźnik na model rozpoznawania mowy biblioteki Vosk.
VoskModel* g_model;

/// Wskaźnik na rozpoznawacz mowy biblioteki Vosk.
VoskRecognizer* g_recognizer;

/// Zmienna przechowująca indeks aktualnie oglądanego eksponatu (-1 jeśli gracz jest za daleko).
int activeExhibit = -1;

/**
 * @brief Struktura reprezentująca eksponat w muzeum.
 * Zawiera informacje o jego położeniu oraz dane tekstowe dla interfejsu graficznego.
 */
struct Exhibit {
    glm::vec2 pos2D;       ///< Pozycja eksponatu w przestrzeni 2D (osie X i Z).
    float radius;          ///< Promień strefy detekcji (jak blisko musi podejść gracz).
    const char* name;      ///< Nazwa eksponatu.
    const char* year;      ///< Rok produkcji/powstania.
    const char* specs[5];  ///< Tablica do 5 specyfikacji technicznych lub ciekawostek.
    int specCount;         ///< Faktyczna liczba użytych specyfikacji.
};

/// Baza danych wszystkich eksponatów dostępnych w muzeum.
std::vector<Exhibit> exhibits = {
    { glm::vec2(-6.0f, -6.0f), 4.0f, "ENIAC", "1945",
      {"Waga: 27 ton", "18 000 lamp prozniowych", "Moc: 150 kW",
       "Predkosc: 5 000 operacji/sek", "Pierwszy komputer ogolnego przeznaczenia"}, 5 },
    { glm::vec2(5.0f, -5.0f), 5.0f, "ODRA 1305", "1973",
      {"Producent: Elwro Wroclaw", "RAM: max 256 KB", "Jezyk: Fortran / Cobol",
       "Cena: ok. 4 mln zlotych", "Legenda polskiej informatyki"}, 5 },
    { glm::vec2(5.0f, 5.0f), 4.0f, "Strefa Retro", "1982 / 1983",
      {"Commodore 64: 64 KB RAM", "Atari 800XL: 64 KB RAM",
       "Procesor: MOS 6502 / 6510", "8-bitowa rewolucja domowa", "Sprzedano lacznie 30 mln sztuk"}, 5 },
    { glm::vec2(-4.5f, 6.5f), 3.0f, "CRAY-1", "1975",
      {"Moc obliczeniowa: 160 MFLOPS", "RAM: 8 MB", "Waga: 5.5 tony",
       "Chlodzenie freonowe w siedzisku", "Superkomputer wektorowy"}, 5 },
    { glm::vec2(-8.0f, 4.0f), 3.0f, "Connection Machine CM-2", "1987",
      {"65 536 mikroprocesorow", "Architektura SIMD", "Moc: 65 536 MFLOPS",
       "Produkowany przez Thinking Machines", "Przetwarzanie masowo rownolegle"}, 5 },
};

/**
 * @brief Callback wywoływany przez bibliotekę Miniaudio, gdy dostępne są dane z mikrofonu.
 * @details Przekazuje próbki dźwięku do biblioteki Vosk, która rozpoznaje komendy
 * ("jasno", "ciemno", "normalnie") i odpowiednio modyfikuje docelową jasność oświetlenia.
 * * @param pDevice Wskaźnik na urządzenie audio.
 * @param pOutput Bufor wyjściowy (nieużywany przy nagrywaniu).
 * @param pInput Bufor wejściowy zawierający próbki audio.
 * @param frameCount Liczba ramek audio w buforze.
 */
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    if (vosk_recognizer_accept_waveform(g_recognizer, (const char*)pInput, frameCount * 2)) {
        const char* result = vosk_recognizer_result(g_recognizer);

        if (strstr(result, "jasno")) targetBrightness = 1.8f;
        if (strstr(result, "ciemno")) targetBrightness = 0.2f;
        if (strstr(result, "normalnie")) targetBrightness = 1.0f;

        printf("Wynik końcowy: %s\n", result);
    }
    else {
        const char* partial = vosk_recognizer_partial_result(g_recognizer);

        if (strstr(partial, "jasno")) targetBrightness = 1.8f;
        if (strstr(partial, "ciemno")) targetBrightness = 0.0f;
        if (strstr(partial, "normalnie")) targetBrightness = 1.0f;
    }
}

/**
 * @brief Callback wywoływany przy zmianie rozmiaru okna GLFW.
 * * @param window Wskaźnik na okno.
 * @param width Nowa szerokość okna.
 * @param height Nowa wysokość okna.
 */
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// --- PARAMETRY KAMERY I STEROWANIA ---
glm::vec3 cameraPos = glm::vec3(-3.0f, 1.5f, -3.0f); ///< Pozycja kamery w przestrzeni świata.
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f); ///< Znormalizowany wektor kierunku patrzenia.
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f); ///< Wektor "w górę" określający orientację kamery.

float yaw = -90.0f;   ///< Kąt obrotu kamery w poziomie (oś Y).
float pitch = 0.0f;   ///< Kąt obrotu kamery w pionie (oś X).
float lastX = 400.0f; ///< Poprzednia pozycja kursora X na ekranie.
float lastY = 300.0f; ///< Poprzednia pozycja kursora Y na ekranie.
bool firstMouse = true; ///< Flaga zabezpieczająca przed gwałtownym skokiem kamery przy pierwszym ruchu.

/**
 * @brief Callback obsługujący ruch myszki, aktualizujący orientację kamery.
 * * @param window Wskaźnik na okno GLFW.
 * @param xpos Aktualna pozycja kursora na osi X.
 * @param ypos Aktualna pozycja kursora na osi Y.
 */
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos; // Odwrócone, bo oś Y rośnie w dół
    lastX = (float)xpos;
    lastY = (float)ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // Zabezpieczenie przed zjawiskiem Gimbal Lock
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

// --- UCHWYTY ZASOBÓW OPENGL ---
GLuint texWall, texFloor, texCeiling, texLamp, texEniacBody, texBricks, texEniac, texRed, texBlack, texCard, texDesk, texGreen, texGauge, texBlue, texYellow, texOdraFrame, texOdraPanel;
GLuint texWood, texC64Beige, texAtariBeige, texDarkKeys, texC64Screen, texAtariScreen;
GLuint cubeVAO, cubeVBO;

/**
 * @brief Struktura reprezentująca prostopadłościenny obszar kolizji (Axis-Aligned Bounding Box).
 */
struct AABB {
    float minX, maxX, minZ, maxZ;
};

/**
 * @brief Konstruuje obszar kolizji AABB na podstawie podanego środka i wymiarów.
 * * @param cx Współrzędna X środka obszaru.
 * @param cz Współrzędna Z środka obszaru.
 * @param width Szerokość obszaru (rozmiar wzdłuż osi X).
 * @param depth Głębokość obszaru (rozmiar wzdłuż osi Z).
 * @return Gotowa struktura AABB.
 */
AABB createBox(float cx, float cz, float width, float depth) {
    return { cx - width / 2.0f, cx + width / 2.0f, cz - depth / 2.0f, cz + depth / 2.0f };
}

/// Statyczna mapa obszarów kolizji reprezentująca ściany budynku muzeum.
std::vector<AABB> walls = {
    createBox(0.0f, -10.0f, 20.0f, 0.5f),
    createBox(0.0f,  10.0f, 20.0f, 0.5f),
    createBox(-10.0f,  0.0f,  0.5f, 20.0f),
    createBox(10.0f,  0.0f,  0.5f, 20.0f),
    createBox(0.0f, 0.0f, 8.0f, 0.5f),
    createBox(0.0f, 0.0f, 0.5f, 8.0f),
    createBox(-8.0f,  0.0f, 4.0f, 0.5f),
    createBox(8.0f,  0.0f, 4.0f, 0.5f),
    createBox(0.0f, -8.0f, 0.5f, 4.0f),
    createBox(0.0f,  8.0f, 0.5f, 4.0f),
    createBox(-5.8f, -8.8f, 7.2f, 1.3f),
    createBox(-8.8f, -5.8f, 1.3f, 4.8f),
    createBox(5.0f, -8.0f, 2.8f, 1.8f),
    createBox(8.5f, -5.3f, 1.2f, 4.0f),
    createBox(5.0f, -5.0f, 1.8f, 1.2f),
    createBox(5.0f, 5.0f, 2.6f, 1.4f),
    createBox(-8.0f, 4.0f, 1.8f, 1.8f),
    createBox(-4.5f, 6.5f, 2.4f, 1.4f),
    createBox(-4.5f, 6.5f, 1.4f, 2.4f)
};

/// Dynamiczna mapa obszarów kolizji aktualizowana co klatkę, zawierająca pozycje postaci NPC.
std::vector<AABB> npcBoxes;

/**
 * @brief Sprawdza, czy punkt o zadanych współrzędnych znajduje się wewnątrz jakiegokolwiek obszaru kolizji.
 * * @param pos Sprawdzana pozycja w przestrzeni 3D.
 * @return true jeśli występuje kolizja ze ścianą lub NPC, false w przeciwnym razie.
 */
bool checkCollision(glm::vec3 pos) {
    float playerRadius = 0.2f;

    for (const auto& wall : walls) {
        bool collisionX = pos.x + playerRadius > wall.minX && pos.x - playerRadius < wall.maxX;
        bool collisionZ = pos.z + playerRadius > wall.minZ && pos.z - playerRadius < wall.maxZ;
        if (collisionX && collisionZ) return true;
    }

    for (const auto& npc : npcBoxes) {
        bool collisionX = pos.x + playerRadius > npc.minX && pos.x - playerRadius < npc.maxX;
        bool collisionZ = pos.z + playerRadius > npc.minZ && pos.z - playerRadius < npc.maxZ;
        if (collisionX && collisionZ) return true;
    }

    return false;
}

/// Tablica wierzchołków definiująca podstawowy, oteksturowany model 3D sześcianu.
float cubeVertices[] = {
    // Współrzędne (3)   Normalne (3)    Tekstura (2)
    -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f
};

/**
 * @brief Struktura reprezentująca interaktywny włącznik światła w wirtualnym środowisku.
 */
struct LightSwitch {
    glm::vec3 pos; ///< Pozycja włącznika w świecie 3D.
    float rotY;    ///< Obrót wokół osi Y (niezbędne do poprawnego przylegania do ściany).
};

/// Lista zdefiniowanych włączników oświetlenia w muzeum.
std::vector<LightSwitch> lightSwitches = {
    { glm::vec3(-0.26f, 1.3f, -2.0f), -90.0f }, // Ściana przy ekspozycji ENIAC
    { glm::vec3(2.0f, 1.3f, -0.26f), 180.0f },  // Ściana przy instalacji ODRA
    { glm::vec3(0.26f, 1.3f, 2.0f), 90.0f },    // Ściana przy pokoju Retro
    { glm::vec3(-2.0f, 1.3f, 0.26f), 0.0f }     // Ściana przy maszynach Cray/CM
};

/**
 * @brief Ładuje teksturę z pliku graficznego PNG.
 * * @param filename Ścieżka do pliku obrazu.
 * @return Wewnętrzny identyfikator wygenerowanej tekstury OpenGL (GLuint).
 */
GLuint readTexture(const char* filename) {
    std::vector<unsigned char> image;
    unsigned width, height;
    unsigned error = lodepng::decode(image, width, height, filename);
    if (error) printf("Blad ladowania tekstury %s: %s\n", filename, lodepng_error_text(error));

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

/**
 * @brief Generuje jednokolorową, jednolitą teksturę proceduralną o wymiarach 1x1 piksel.
 * Wykorzystywane powszechnie do kolorowania modeli bez używania osobnych plików graficznych.
 * * @param r Wartość składowej czerwonej (0-255).
 * @param g Wartość składowej zielonej (0-255).
 * @param b Wartość składowej niebieskiej (0-255).
 * @return Identyfikator wygenerowanej tekstury OpenGL.
 */
GLuint createSolidColorTexture(unsigned char r, unsigned char g, unsigned char b) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    unsigned char data[] = { r, g, b, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

/**
 * @brief Główny callback błędów dla biblioteki GLFW.
 * * @param error Kod błędu.
 * @param description Tekstowy opis zaistniałego błędu.
 */
void error_callback(int error, const char* description) {
    fputs(description, stderr);
}

/**
 * @brief Callback obsługujący punktowe, jednorazowe interakcje z klawiaturą (wciśnięcia/puszczenia klawiszy).
 * Różni się od funkcji processInput tym, że rejestruje tylko stan momentalny, niezbędny do interakcji
 * (np. użycie przełącznika klawiszem 'E', lub wyjście 'ESC').
 * * @param window Wskaźnik głównego okna programu.
 * @param key Wciśnięty kod klawisza.
 * @param scancode Sprzętowy kod skanowania klawisza.
 * @param action Akcja (np. GLFW_PRESS, GLFW_RELEASE).
 * @param mod Zastosowane modyfikatory (Shift, Ctrl, Alt).
 */
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mod) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // Obsługa interakcji z włącznikami światła
    if (key == GLFW_KEY_E && action == GLFW_PRESS) {
        bool nearSwitch = false;
        for (const auto& sw : lightSwitches) {
            if (glm::distance(cameraPos, sw.pos) < 1.5f) {
                nearSwitch = true;
                break;
            }
        }

        if (nearSwitch) {
            if (targetBrightness > 0.5f) {
                targetBrightness = 0.2f; // Przejście do stanu zgaszonego
            }
            else {
                targetBrightness = 1.0f; // Przejście do stanu zapalonego
            }
        }
    }
}

/**
 * @brief Przetwarza wejście klawiatury dla stanów ciągłych (np. ruch gracza, fizyka).
 * @details Funkcja wywoływana co klatkę. Sprawdza wciśnięcia klawiszy W, S, A, D i modyfikuje pozycję
 * kamery po uwzględnieniu systemu detekcji kolizji. Osobne testy dla osi X i Z umożliwiają "ślizganie"
 * się wzdłuż ścian bez całkowitego zatrzymywania bohatera.
 * * @param window Wskaźnik na okno aplikacji.
 */
void processInput(GLFWwindow* window) {
    float speed = 0.05f;
    glm::vec3 targetDirection = glm::vec3(0.0f);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) targetDirection += cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) targetDirection -= cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) targetDirection -= glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) targetDirection += glm::normalize(glm::cross(cameraFront, cameraUp));

    glm::vec3 movement = targetDirection * speed;

    // Detekcja kolizji niezależnie dla osi X
    glm::vec3 testPosX = glm::vec3(cameraPos.x + movement.x, cameraPos.y, cameraPos.z);
    if (!checkCollision(testPosX)) {
        cameraPos.x += movement.x;
    }

    // Detekcja kolizji niezależnie dla osi Z
    glm::vec3 testPosZ = glm::vec3(cameraPos.x, cameraPos.y, cameraPos.z + movement.z);
    if (!checkCollision(testPosZ)) {
        cameraPos.z += movement.z;
    }

    cameraPos.y = 1.5f; // Utrzymanie zablokowanej stałej wysokości kamery ("oczu" gracza)
}

/**
 * @brief Konfiguruje i inicjalizuje główny stan maszyny OpenGL, VAO/VBO, shadery oraz wgrywa niezbędne zasoby do pamięci VRAM.
 * * @param window Wskaźnik docelowego okna GLFW.
 */
void initOpenGLProgram(GLFWwindow* window) {
    initShaders();
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glfwSetKeyCallback(window, key_callback);

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);

    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    // Bindowanie atrybutów: Współrzędne werteksów (layout 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Bindowanie atrybutów: Wektory normalne dla oświetlenia (layout 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Bindowanie atrybutów: Koordynaty UV tekstury (layout 2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // Proceduralne i fizyczne tekstury dla budynków i eksponatów
    texWall = createSolidColorTexture(75, 95, 120);
    texFloor = createSolidColorTexture(80, 80, 85);
    texEniacBody = createSolidColorTexture(35, 38, 35);
    texBricks = readTexture("bricks.png");
    texEniac = readTexture("eniac_panel.png");
    texCeiling = createSolidColorTexture(255, 255, 255);
    texLamp = createSolidColorTexture(255, 255, 220);
    texRed = createSolidColorTexture(255, 50, 50);
    texBlack = createSolidColorTexture(30, 30, 30);
    texCard = createSolidColorTexture(220, 200, 160);
    texDesk = createSolidColorTexture(70, 75, 80);
    texGreen = createSolidColorTexture(50, 255, 50);
    texGauge = createSolidColorTexture(200, 190, 170);
    texBlue = createSolidColorTexture(50, 50, 255);
    texYellow = createSolidColorTexture(255, 255, 50);
    texOdraFrame = createSolidColorTexture(230, 225, 210);
    texOdraPanel = createSolidColorTexture(210, 70, 20);
    cylinderMesh.init(32);
    texWood = createSolidColorTexture(110, 70, 40);
    texC64Beige = createSolidColorTexture(180, 175, 155);
    texAtariBeige = createSolidColorTexture(200, 195, 185);
    texDarkKeys = createSolidColorTexture(60, 50, 45);
    texC64Screen = createSolidColorTexture(120, 120, 230);
    texAtariScreen = createSolidColorTexture(30, 60, 180);
}

/**
 * @brief Funkcja wywoływana przed wyłączeniem programu. Niszczy bufory i zwalnia zasoby na karcie graficznej.
 * * @param window Wskaźnik do usuwanego okna GLFW.
 */
void freeOpenGLProgram(GLFWwindow* window) {
    freeShaders();
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    cylinderMesh.free();
}

/**
 * @brief Rysuje i nakłada tekstury na obiekt w kształcie walca/cylindra.
 * * @param M Pełna macierz transformacji modelu (Przesunięcie * Obrót * Skalowanie).
 * @param tex Odniesienie do zbindowanej tekstury.
 * @param sp Wskaźnik używanego zestawu shaderów.
 * @param isLamp Kod typu zachowania materiału względem światła (0 - przyjmuje cień, 1/2 - źródło/emisja).
 */
void drawCylinder(glm::mat4 M, GLuint tex, ShaderProgram* sp, int isLamp = 0) {
    glUniformMatrix4fv(sp->u("M"), 1, false, glm::value_ptr(M));
    glUniform1i(sp->u("isLamp"), isLamp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(sp->u("tex"), 0);
    cylinderMesh.draw();
}

/**
 * @brief Główna funkcja wykonująca rysowanie prostopadłościanów stanowiących większość geometrii w programie.
 * * @param M Macierz transformacji modelu określająca finalną pozycję obiektu.
 * @param tex Uchwyt nadawanej tekstury.
 * @param sp Wykorzystywany zintegrowany program cieniujący.
 * @param isLamp Flaga trybu emisji.
 */
void drawObject(glm::mat4 M, GLuint tex, ShaderProgram* sp, int isLamp = 0) {
    glUniformMatrix4fv(sp->u("M"), 1, false, glm::value_ptr(M));
    glUniform1i(sp->u("isLamp"), isLamp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(sp->u("tex"), 0);

    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

/**
 * @brief Konstruuje szafę (segment) amerykańskiego superkomputera ENIAC.
 * @details Używa zmiennej numerycznej `seed` zależnej od fizycznej pozycji w świecie do
 * wariacyjnego rozmieszczenia poszczególnych komponentów elektroniki (rurek elektronowych, przewodów, styków).
 * * @param pos Środkowy punkt umieszczenia segmentu szafy.
 * @param rotY Ustawienie obrotu wokół osi Y.
 * @param sp Program cieniujący do narysowania geometrii.
 * @param isLast Ustawia flagę ograniczającą rysowanie grubych kabli łączących jeżeli segment jest ostatnim w klastrze.
 */
void drawEniacCabinet(glm::vec3 pos, float rotY, ShaderProgram* sp, bool isLast = false) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    int seed = (int)(abs(pos.x * 111.0f + pos.z * 43.0f));

    glm::mat4 mCab = glm::translate(mBase, glm::vec3(0.0f, 1.75f, 0.0f));
    mCab = glm::scale(mCab, glm::vec3(1.1f, 3.5f, 0.8f));
    drawObject(mCab, texEniacBody, sp, 0);

    for (int v = 0; v < 8; v++) {
        glm::mat4 mVent = glm::translate(mBase, glm::vec3(-0.38f + v * 0.11f, 0.22f, 0.41f));
        mVent = glm::scale(mVent, glm::vec3(0.06f, 0.28f, 0.04f));
        drawObject(mVent, texBlack, sp, 0);
    }

    for (int v = 0; v < 4; v++) {
        glm::mat4 mVentH = glm::translate(mBase, glm::vec3(0.0f, 0.12f + v * 0.07f, 0.41f));
        mVentH = glm::scale(mVentH, glm::vec3(0.9f, 0.012f, 0.04f));
        drawObject(mVentH, texBlack, sp, 0);
    }

    glm::mat4 mDesk = glm::translate(mBase, glm::vec3(0.0f, 0.8f, 0.6f));
    mDesk = glm::scale(mDesk, glm::vec3(1.1f, 0.1f, 0.5f));
    drawObject(mDesk, texDesk, sp, 0);

    glm::mat4 mSockets = glm::translate(mBase, glm::vec3(0.0f, 0.86f, 0.65f));
    mSockets = glm::scale(mSockets, glm::vec3(0.9f, 0.02f, 0.2f));
    drawObject(mSockets, texBlack, sp, 0);

    glm::mat4 mLegL = glm::translate(mBase, glm::vec3(-0.45f, 0.4f, 0.75f));
    mLegL = glm::scale(mLegL, glm::vec3(0.05f, 0.8f, 0.05f));
    drawObject(mLegL, texDesk, sp, 0);
    glm::mat4 mLegR = glm::translate(mBase, glm::vec3(0.45f, 0.4f, 0.75f));
    mLegR = glm::scale(mLegR, glm::vec3(0.05f, 0.8f, 0.05f));
    drawObject(mLegR, texDesk, sp, 0);

    glm::mat4 mPanelLow = glm::translate(mBase, glm::vec3(0.0f, 1.3f, 0.41f));
    mPanelLow = glm::scale(mPanelLow, glm::vec3(0.9f, 0.6f, 0.05f));
    drawObject(mPanelLow, texBlack, sp, 0);

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 10; col++) {
            glm::mat4 mKnob = glm::translate(mBase, glm::vec3(-0.40f + col * 0.088f, 1.08f + row * 0.16f, 0.44f));
            mKnob = glm::scale(mKnob, glm::vec3(0.045f, 0.045f, 0.025f));
            drawObject(mKnob, texGauge, sp, 0);
            glm::mat4 mKnobC = glm::translate(mBase, glm::vec3(-0.40f + col * 0.088f, 1.08f + row * 0.16f, 0.455f));
            mKnobC = glm::scale(mKnobC, glm::vec3(0.018f, 0.018f, 0.01f));
            drawObject(mKnobC, texBlack, sp, 0);
        }
    }

    for (int i = 0; i < 5; i++) {
        float xOffset = -0.35f + (i * 0.17f);
        glm::mat4 mSwitch = glm::translate(mBase, glm::vec3(xOffset, 1.55f, 0.44f));
        mSwitch = glm::scale(mSwitch, glm::vec3(0.025f, 0.12f, 0.04f));
        drawObject(mSwitch, texCeiling, sp, 0);
        glm::mat4 mSwitchTop = glm::translate(mBase, glm::vec3(xOffset, 1.62f, 0.445f));
        mSwitchTop = glm::scale(mSwitchTop, glm::vec3(0.035f, 0.02f, 0.025f));
        drawObject(mSwitchTop, texGauge, sp, 0);
    }

    glm::mat4 mPanelUp = glm::translate(mBase, glm::vec3(0.0f, 2.4f, 0.41f));
    mPanelUp = glm::scale(mPanelUp, glm::vec3(0.9f, 1.0f, 0.05f));
    drawObject(mPanelUp, texBlack, sp, 0);

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 8; col++) {
            float lx = -0.37f + col * 0.105f;
            float ly = 1.98f + row * 0.175f;

            glm::mat4 mTubeBase = glm::translate(mBase, glm::vec3(lx, ly, 0.44f));
            mTubeBase = glm::scale(mTubeBase, glm::vec3(0.040f, 0.030f, 0.020f));
            drawObject(mTubeBase, texDesk, sp, 0);

            glm::mat4 mTube = glm::translate(mBase, glm::vec3(lx, ly + 0.055f, 0.44f));
            mTube = glm::scale(mTube, glm::vec3(0.028f, 0.075f, 0.028f));
            int isGlowing = ((seed + row * 13 + col * 7) % 10 > 2) ? 2 : 0;
            drawObject(mTube, isGlowing ? texYellow : texGauge, sp, isGlowing);

            glm::mat4 mTubeTop = glm::translate(mBase, glm::vec3(lx, ly + 0.100f, 0.44f));
            mTubeTop = glm::scale(mTubeTop, glm::vec3(0.016f, 0.020f, 0.016f));
            drawObject(mTubeTop, texBlack, sp, 0);

            for (int pin = 0; pin < 3; pin++) {
                glm::mat4 mPin = glm::translate(mBase, glm::vec3(lx - 0.012f + pin * 0.012f, ly - 0.018f, 0.44f));
                mPin = glm::scale(mPin, glm::vec3(0.004f, 0.022f, 0.004f));
                drawObject(mPin, texBlack, sp, 0);
            }
        }
    }

    // Generator zwisających, połączonych kabli (wykorzystanie algorytmu krzywych Beziera)
    for (int k = 0; k < 12; k++) {
        int knobRow = ((seed + k * 5) % 3);
        int knobCol = ((seed + k * 11) % 10);
        float startX = -0.40f + knobCol * 0.088f;
        float startY = 1.08f + knobRow * 0.16f;
        float startZ = 0.46f;

        float endX = -0.38f + ((seed + k * 7) % 9) * 0.095f;
        float endY = 0.875f;
        float endZ = 0.65f;

        glm::vec3 p1(startX, startY, startZ);
        glm::vec3 p2(endX, endY, endZ);

        float ctrlY = glm::max(endY + 0.05f, (startY + endY) * 0.5f - 0.05f);
        float ctrlZ = startZ + (endZ - startZ) * 0.3f;
        glm::vec3 ctrl(
            (startX + endX) * 0.5f + ((seed + k) % 3 - 1) * 0.06f,
            ctrlY,
            ctrlZ
        );

        glm::mat4 mPlug = glm::translate(mBase, p1 + glm::vec3(0.0f, 0.0f, 0.018f));
        mPlug = glm::scale(mPlug, glm::vec3(0.016f, 0.012f, 0.022f));
        drawObject(mPlug, texDesk, sp, 0);

        glm::mat4 mPlug2 = glm::translate(mBase, p2 + glm::vec3(0.0f, 0.008f, 0.0f));
        mPlug2 = glm::rotate(mPlug2, glm::radians(90.0f), glm::vec3(1, 0, 0));
        mPlug2 = glm::scale(mPlug2, glm::vec3(0.016f, 0.022f, 0.012f));
        drawObject(mPlug2, texDesk, sp, 0);

        const int segs = 7;
        glm::vec3 prev = p1;
        for (int s = 1; s <= segs; s++) {
            float t = (float)s / segs;
            float mt = 1.0f - t;

            glm::vec3 cur = mt * mt * p1 + 2.0f * mt * t * ctrl + t * t * p2;

            cur.y = glm::max(cur.y, endY + 0.005f);

            glm::vec3 d = cur - prev;
            float len = glm::length(d);
            if (len < 0.001f) { prev = cur; continue; }

            glm::vec3 center = (prev + cur) * 0.5f;
            glm::vec3 nd = glm::normalize(d);
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 axis = glm::cross(up, nd);
            float dotVal = glm::clamp(glm::dot(up, nd), -1.0f, 1.0f);
            float angle = acos(dotVal);

            GLuint wireTex = ((seed + k) % 3 == 0) ? texRed :
                ((seed + k) % 3 == 1) ? texBlack : texDesk;

            glm::mat4 mSeg = glm::translate(mBase, center);
            if (glm::length(axis) > 0.001f)
                mSeg = glm::rotate(mSeg, angle, glm::normalize(axis));
            mSeg = glm::scale(mSeg, glm::vec3(0.006f, len, 0.006f));
            drawObject(mSeg, wireTex, sp, 0);

            prev = cur;
        }
    }

    if (seed % 3 == 0) {
        glm::mat4 mReader = glm::translate(mBase, glm::vec3(0.2f, 0.95f, 0.65f));
        mReader = glm::scale(mReader, glm::vec3(0.35f, 0.2f, 0.3f));
        drawObject(mReader, texDesk, sp, 0);

        glm::mat4 mSlot = glm::translate(mBase, glm::vec3(0.2f, 0.96f, 0.82f));
        mSlot = glm::scale(mSlot, glm::vec3(0.28f, 0.015f, 0.012f));
        drawObject(mSlot, texBlack, sp, 0);

        glm::mat4 mPaper = glm::translate(mBase, glm::vec3(0.2f, 0.98f, 0.88f));
        mPaper = glm::rotate(mPaper, glm::radians(20.0f), glm::vec3(1, 0, 0));
        mPaper = glm::scale(mPaper, glm::vec3(0.22f, 0.01f, 0.35f));
        drawObject(mPaper, texCard, sp, 0);
    }

    glm::mat4 mTopPanel = glm::translate(mBase, glm::vec3(0.0f, 3.18f, 0.41f));
    mTopPanel = glm::scale(mTopPanel, glm::vec3(0.88f, 0.26f, 0.05f));
    drawObject(mTopPanel, texDesk, sp, 0);

    for (int i = 0; i < 3; i++) {
        glm::mat4 mGauge = glm::translate(mBase, glm::vec3(-0.28f + (i * 0.28f), 3.20f, 0.445f));
        mGauge = glm::scale(mGauge, glm::vec3(0.16f, 0.16f, 0.02f));
        drawObject(mGauge, texGauge, sp, 0);
        glm::mat4 mNeedle = glm::translate(mBase, glm::vec3(-0.28f + (i * 0.28f) + ((seed + i) % 5 - 2) * 0.025f, 3.20f, 0.455f));
        mNeedle = glm::rotate(mNeedle, glm::radians(-30.0f + ((seed + i) % 7) * 10.0f), glm::vec3(0, 0, 1));
        mNeedle = glm::scale(mNeedle, glm::vec3(0.005f, 0.10f, 0.005f));
        drawObject(mNeedle, texBlack, sp, 0);
    }

    for (int i = 0; i < 4; i++) {
        GLuint sTex = ((seed + i) % 3 == 0) ? texGreen : texRed;
        int isLit = ((seed + i * 3) % 4 > 1) ? 2 : 0;
        glm::mat4 mStatus = glm::translate(mBase, glm::vec3(0.15f + (i * 0.055f), 3.10f, 0.445f));
        mStatus = glm::scale(mStatus, glm::vec3(0.030f, 0.030f, 0.015f));
        drawObject(mStatus, sTex, sp, isLit);
    }

    if (!isLast) {
        for (int c = 0; c < 5; c++) {
            GLuint cTex = (c % 2 == 0) ? texBlack : texRed;
            glm::mat4 mLink = glm::translate(mBase, glm::vec3(0.6f, 0.45f + (c * 0.08f), 0.35f));
            mLink = glm::scale(mLink, glm::vec3(1.2f, 0.018f, 0.018f));
            drawObject(mLink, cTex, sp, 0);
        }
        glm::mat4 mBigLink = glm::translate(mBase, glm::vec3(0.6f, 0.85f, 0.38f));
        mBigLink = glm::scale(mBigLink, glm::vec3(1.2f, 0.06f, 0.06f));
        drawObject(mBigLink, texBlack, sp, 0);
    }
}

/**
 * @brief Animuje i renderuje wizualizację napędów na dyski/taśmy magnetyczne do maszyny Odra 1305.
 * * @param pos Wektor pozycjonujący.
 * @param rotY Obrót własny napędu.
 * @param sp Wskaźnik instancji ShaderProgram do rysowania figur.
 * @param index Identyfikator napędu w tablicy (potrzebny do generowania różnic faz szpul z taśmą).
 */
void drawTapeDrive(glm::vec3 pos, float rotY, ShaderProgram* sp, int index) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mCab = glm::translate(mBase, glm::vec3(0.0f, 1.4f, 0.0f));
    mCab = glm::scale(mCab, glm::vec3(0.9f, 2.8f, 0.7f));
    drawObject(mCab, texOdraFrame, sp, 0);

    glm::mat4 mDoor = glm::translate(mBase, glm::vec3(0.0f, 0.65f, 0.355f));
    mDoor = glm::scale(mDoor, glm::vec3(0.85f, 1.2f, 0.05f));
    drawObject(mDoor, texOdraPanel, sp, 0);

    glm::mat4 mPanel = glm::translate(mBase, glm::vec3(0.0f, 1.8f, 0.36f));
    mPanel = glm::scale(mPanel, glm::vec3(0.8f, 1.0f, 0.05f));
    drawObject(mPanel, texBlack, sp, 0);

    float time = (float)glfwGetTime();

    float tapePhase = time * 0.6f + index * 2.5f;
    float tapeFill = (sin(tapePhase) + 1.0f) * 0.5f;
    float spoolAngle = tapeFill * 35.0f;

    glm::mat4 mReel1 = glm::translate(mBase, glm::vec3(-0.22f, 1.9f, 0.39f));
    mReel1 = glm::rotate(mReel1, spoolAngle, glm::vec3(0, 0, 1));
    mReel1 = glm::rotate(mReel1, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mReel1 = glm::scale(mReel1, glm::vec3(0.28f, 0.04f, 0.28f));
    drawCylinder(mReel1, texCeiling, sp, 0);

    float r1 = 0.085f + tapeFill * 0.17f;
    glm::mat4 mTapeWound1 = glm::translate(mBase, glm::vec3(-0.22f, 1.9f, 0.39f));
    mTapeWound1 = glm::rotate(mTapeWound1, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mTapeWound1 = glm::scale(mTapeWound1, glm::vec3(r1, 0.045f, r1));
    drawCylinder(mTapeWound1, texBlack, sp, 0);

    glm::mat4 mHub1 = glm::translate(mBase, glm::vec3(-0.22f, 1.9f, 0.395f));
    mHub1 = glm::rotate(mHub1, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mHub1 = glm::scale(mHub1, glm::vec3(0.08f, 0.05f, 0.08f));
    drawCylinder(mHub1, texBlack, sp, 0);

    glm::mat4 mReel2 = glm::translate(mBase, glm::vec3(0.22f, 1.9f, 0.39f));
    mReel2 = glm::rotate(mReel2, spoolAngle, glm::vec3(0, 0, 1));
    mReel2 = glm::rotate(mReel2, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mReel2 = glm::scale(mReel2, glm::vec3(0.28f, 0.04f, 0.28f));
    drawCylinder(mReel2, texCeiling, sp, 0);

    float r2 = 0.085f + (1.0f - tapeFill) * 0.17f;
    glm::mat4 mTapeWound2 = glm::translate(mBase, glm::vec3(0.22f, 1.9f, 0.39f));
    mTapeWound2 = glm::rotate(mTapeWound2, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mTapeWound2 = glm::scale(mTapeWound2, glm::vec3(r2, 0.045f, r2));
    drawCylinder(mTapeWound2, texBlack, sp, 0);

    glm::mat4 mHub2 = glm::translate(mBase, glm::vec3(0.22f, 1.9f, 0.395f));
    mHub2 = glm::rotate(mHub2, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mHub2 = glm::scale(mHub2, glm::vec3(0.08f, 0.05f, 0.08f));
    drawCylinder(mHub2, texBlack, sp, 0);

    float tapeWidth = 0.48f;
    float tapeStartX = -0.24f;

    float bottomOffset = tapeFill * 5.0f;
    float topOffset = -tapeFill * 5.0f;

    glm::mat4 mTapeBottom = glm::translate(mBase, glm::vec3(0.0f, 1.78f, 0.39f));
    mTapeBottom = glm::scale(mTapeBottom, glm::vec3(tapeWidth, 0.02f, 0.01f));
    drawObject(mTapeBottom, texBlack, sp, 0);

    for (int m = 0; m < 5; m++) {
        float fraction = fmod(bottomOffset + m * 0.2f, 1.0f);
        if (fraction < 0.0f) fraction += 1.0f;
        float mx = tapeStartX + fraction * tapeWidth;

        glm::mat4 mMarker = glm::translate(mBase, glm::vec3(mx, 1.78f, 0.396f));
        mMarker = glm::scale(mMarker, glm::vec3(0.015f, 0.022f, 0.003f));
        drawObject(mMarker, texDesk, sp, 0);
    }

    glm::mat4 mTapeTop = glm::translate(mBase, glm::vec3(0.0f, 2.02f, 0.39f));
    mTapeTop = glm::scale(mTapeTop, glm::vec3(tapeWidth, 0.02f, 0.01f));
    drawObject(mTapeTop, texBlack, sp, 0);

    for (int m = 0; m < 5; m++) {
        float fraction = fmod(topOffset + m * 0.2f, 1.0f);
        if (fraction < 0.0f) fraction += 1.0f;
        float mx = tapeStartX + fraction * tapeWidth;

        glm::mat4 mMarker = glm::translate(mBase, glm::vec3(mx, 2.02f, 0.396f));
        mMarker = glm::scale(mMarker, glm::vec3(0.015f, 0.022f, 0.003f));
        drawObject(mMarker, texDesk, sp, 0);
    }

    // Dynamicznie wybiera z trzech wariantów przycisków / interfejsu
    if (index == 0) {
        glm::mat4 mSubPanel = glm::translate(mBase, glm::vec3(0.0f, 0.9f, 0.36f));
        mSubPanel = glm::scale(mSubPanel, glm::vec3(0.5f, 0.15f, 0.05f));
        drawObject(mSubPanel, texBlack, sp, 0);

        for (int j = 0; j < 5; j++) {
            glm::mat4 mLed = glm::translate(mBase, glm::vec3(-0.15f + (j * 0.075f), 0.9f, 0.39f));
            mLed = glm::scale(mLed, glm::vec3(0.04f, 0.04f, 0.02f));
            drawObject(mLed, texRed, sp, 2);
        }

        glm::mat4 mPlate = glm::translate(mBase, glm::vec3(0.0f, 0.5f, 0.36f));
        mPlate = glm::scale(mPlate, glm::vec3(0.4f, 0.2f, 0.05f));
        drawObject(mPlate, texGauge, sp, 0);
    }
    else if (index == 1) {
        GLuint btnColors[4] = { texRed, texBlue, texGreen, texYellow };
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 2; c++) {
                glm::mat4 mBtn = glm::translate(mBase, glm::vec3(-0.15f + (c * 0.08f), 0.95f - (r * 0.08f), 0.36f));
                mBtn = glm::scale(mBtn, glm::vec3(0.05f, 0.05f, 0.02f));
                drawObject(mBtn, btnColors[r * 2 + c], sp, 2);
            }
        }
        for (int sw = 0; sw < 3; sw++) {
            glm::mat4 mSw = glm::translate(mBase, glm::vec3(0.05f + (sw * 0.08f), 0.91f, 0.36f));
            mSw = glm::rotate(mSw, glm::radians(30.0f), glm::vec3(1, 0, 0));
            mSw = glm::scale(mSw, glm::vec3(0.02f, 0.08f, 0.02f));
            drawObject(mSw, texCeiling, sp, 0);
        }
    }
    else if (index == 2) {
        glm::mat4 mSubPanel = glm::translate(mBase, glm::vec3(0.0f, 0.9f, 0.36f));
        mSubPanel = glm::scale(mSubPanel, glm::vec3(0.5f, 0.15f, 0.05f));
        drawObject(mSubPanel, texBlack, sp, 0);

        for (int j = 0; j < 3; j++) {
            glm::mat4 mBar = glm::translate(mBase, glm::vec3(-0.1f + (j * 0.1f), 0.9f, 0.39f));
            mBar = glm::scale(mBar, glm::vec3(0.08f, 0.08f, 0.02f));
            drawObject(mBar, texGreen, sp, 2);
        }

        glm::mat4 mPlate = glm::translate(mBase, glm::vec3(0.0f, 0.5f, 0.36f));
        mPlate = glm::scale(mPlate, glm::vec3(0.4f, 0.2f, 0.05f));
        drawObject(mPlate, texGauge, sp, 0);
    }
}

/**
 * @brief Odwzorowuje geometrycznie terminal wideo zintegrowany z maszyną Mainframe.
 * * @param pos Środek biurka konsoli.
 * @param rotY Obrót całego modelu wzdłuż osi Y.
 * @param sp Wskaźnik ładujący bieżący układ cieniowania.
 */
void drawMainframeConsole(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mDesk = glm::translate(mBase, glm::vec3(0.0f, 0.7f, 0.0f));
    mDesk = glm::scale(mDesk, glm::vec3(1.4f, 0.05f, 0.8f));
    drawObject(mDesk, texDesk, sp, 0);

    glm::mat4 mLeg1 = glm::translate(mBase, glm::vec3(-0.65f, 0.35f, 0.0f));
    mLeg1 = glm::scale(mLeg1, glm::vec3(0.05f, 0.7f, 0.7f));
    drawObject(mLeg1, texBlack, sp, 0);

    glm::mat4 mLeg2 = glm::translate(mBase, glm::vec3(0.65f, 0.35f, 0.0f));
    mLeg2 = glm::scale(mLeg2, glm::vec3(0.05f, 0.7f, 0.7f));
    drawObject(mLeg2, texBlack, sp, 0);

    glm::mat4 mMonBase = glm::translate(mBase, glm::vec3(0.0f, 0.75f, -0.1f));
    mMonBase = glm::scale(mMonBase, glm::vec3(0.25f, 0.1f, 0.25f));
    drawObject(mMonBase, texOdraFrame, sp, 0);

    glm::mat4 mMonitor = glm::translate(mBase, glm::vec3(0.0f, 0.98f, -0.05f));
    mMonitor = glm::rotate(mMonitor, glm::radians(5.0f), glm::vec3(1, 0, 0));
    mMonitor = glm::scale(mMonitor, glm::vec3(0.5f, 0.45f, 0.45f));
    drawObject(mMonitor, texOdraFrame, sp, 0);

    glm::mat4 mScreen = glm::translate(mBase, glm::vec3(0.0f, 0.98f, 0.18f));
    mScreen = glm::rotate(mScreen, glm::radians(5.0f), glm::vec3(1, 0, 0));
    mScreen = glm::scale(mScreen, glm::vec3(0.42f, 0.35f, 0.02f));
    drawObject(mScreen, texGreen, sp, 2);

    glm::mat4 mKeybDeck = glm::translate(mBase, glm::vec3(0.0f, 0.74f, 0.28f));
    mKeybDeck = glm::rotate(mKeybDeck, glm::radians(10.0f), glm::vec3(1, 0, 0));

    glm::mat4 mKeybBase = glm::scale(mKeybDeck, glm::vec3(0.6f, 0.04f, 0.2f));
    drawObject(mKeybBase, texBlack, sp, 0);

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 15; c++) {
            if (r == 3 && c > 3 && c < 11) continue;
            glm::mat4 mKey = glm::translate(mKeybDeck, glm::vec3(-0.24f + (c * 0.034f), 0.025f, -0.06f + (r * 0.035f)));
            mKey = glm::scale(mKey, glm::vec3(0.028f, 0.015f, 0.028f));
            drawObject(mKey, texOdraFrame, sp, 0);
        }
    }
    glm::mat4 mSpace = glm::translate(mKeybDeck, glm::vec3(0.0f, 0.025f, 0.045f));
    mSpace = glm::scale(mSpace, glm::vec3(0.23f, 0.015f, 0.028f));
    drawObject(mSpace, texOdraFrame, sp, 0);

    glm::mat4 mCable = glm::translate(mBase, glm::vec3(0.0f, 0.73f, 0.12f));
    mCable = glm::scale(mCable, glm::vec3(0.02f, 0.02f, 0.2f));
    drawObject(mCable, texBlack, sp, 0);
}

/**
 * @brief Rysuje i konstruuje kompletny zestaw instalacji ODRA 1305 (Szafa CPU, napędy taśmowe i konsolę).
 * * @param centerPos Ogólny wektor centralny, wokół którego układane są poszczególne elementy peryferyjne.
 * @param sp Wskaźnik do wykorzystywanego programu cieniującego.
 */
void drawOdra1305(glm::vec3 centerPos, ShaderProgram* sp) {
    glm::mat4 mCpuBase = glm::translate(glm::mat4(1.0f), centerPos + glm::vec3(0.0f, 1.0f, -3.0f));
    glm::mat4 mCpu = glm::scale(mCpuBase, glm::vec3(2.5f, 2.0f, 1.5f));
    drawObject(mCpu, texOdraFrame, sp, 0);

    glm::mat4 mCpuFront = glm::translate(mCpuBase, glm::vec3(0.0f, 0.1f, 0.755f));
    mCpuFront = glm::scale(mCpuFront, glm::vec3(2.4f, 1.6f, 0.02f));
    drawObject(mCpuFront, texOdraPanel, sp, 0);

    glm::mat4 mCpuPanel = glm::translate(mCpuBase, glm::vec3(-0.5f, 0.3f, 0.76f));
    mCpuPanel = glm::scale(mCpuPanel, glm::vec3(0.8f, 0.4f, 0.05f));
    drawObject(mCpuPanel, texBlack, sp, 0);

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            glm::mat4 mBtn = glm::translate(mCpuBase, glm::vec3(-0.7f + (c * 0.12f), 0.36f - (r * 0.12f), 0.79f));
            mBtn = glm::scale(mBtn, glm::vec3(0.08f, 0.08f, 0.02f));
            drawObject(mBtn, texRed, sp, 2);
        }
    }

    glm::mat4 mDial = glm::translate(mCpuBase, glm::vec3(-0.35f, 0.3f, 0.79f));
    mDial = glm::scale(mDial, glm::vec3(0.12f, 0.12f, 0.03f));
    drawObject(mDial, texDesk, sp, 0);

    for (int v = 0; v < 5; v++) {
        glm::mat4 mVent = glm::translate(mCpuBase, glm::vec3(0.2f + (v * 0.15f), 0.3f, 0.76f));
        mVent = glm::scale(mVent, glm::vec3(0.05f, 0.4f, 0.02f));
        drawObject(mVent, texBlack, sp, 0);
    }

    for (int i = 0; i < 3; i++) {
        drawTapeDrive(centerPos + glm::vec3(3.5f, 0.0f, -1.5f + (i * 1.2f)), -90.0f, sp, i);
    }

    drawMainframeConsole(centerPos + glm::vec3(0.0f, 0.0f, 0.0f), 0.0f, sp);

    // Listwy maskujące uziemienie
    glm::mat4 mCable1 = glm::translate(glm::mat4(1.0f), centerPos + glm::vec3(0.0f, 0.05f, -1.65f));
    mCable1 = glm::scale(mCable1, glm::vec3(0.15f, 0.1f, 2.7f));
    drawObject(mCable1, texBlack, sp, 0);

    glm::mat4 mCable2 = glm::translate(glm::mat4(1.0f), centerPos + glm::vec3(1.75f, 0.05f, -1.5f));
    mCable2 = glm::scale(mCable2, glm::vec3(3.5f, 0.1f, 0.15f));
    drawObject(mCable2, texBlack, sp, 0);

    glm::mat4 mCable3 = glm::translate(glm::mat4(1.0f), centerPos + glm::vec3(3.3f, 0.05f, -0.3f));
    mCable3 = glm::scale(mCable3, glm::vec3(0.15f, 0.1f, 2.6f));
    drawObject(mCable3, texBlack, sp, 0);

    glm::mat4 mCableUp = glm::translate(glm::mat4(1.0f), centerPos + glm::vec3(0.0f, 0.35f, -0.3f));
    mCableUp = glm::scale(mCableUp, glm::vec3(0.15f, 0.7f, 0.1f));
    drawObject(mCableUp, texBlack, sp, 0);
}

/**
 * @brief Rysuje w muzeum wielką instalację komputera ENIAC składającą się z wielu modułów w kształcie litery L.
 * * @param centerPos Środek przestrzeni ekspozycji maszyny ENIAC.
 * @param sp Wskaźnik programu cieniującego.
 */
void drawUltimateEniac(glm::vec3 centerPos, ShaderProgram* sp) {
    glm::mat4 mCorner = glm::translate(glm::mat4(1.0f), glm::vec3(-8.775f, 1.75f, -8.775f));
    mCorner = glm::scale(mCorner, glm::vec3(1.25f, 3.5f, 1.25f));
    drawObject(mCorner, texEniacBody, sp, 0);

    for (int i = 0; i < 5; i++) {
        glm::vec3 pos = glm::vec3(-7.6f + (i * 1.2f), 0.0f, -9.0f);
        bool isLastInRow = (i == 4);
        drawEniacCabinet(pos, 0.0f, sp, isLastInRow);
    }

    for (int i = 0; i < 4; i++) {
        glm::vec3 pos = glm::vec3(-9.0f, 0.0f, -7.6f + (i * 1.2f));
        bool isLastInRow = (i == 0);
        drawEniacCabinet(pos, 90.0f, sp, isLastInRow);
    }
}

/**
 * @brief Generuje niskopoligonowy model tradycyjnego "drążka lotniczego" - Joysticka epoki Atari.
 * * @param pos Bazowa pozycja geometryczna.
 * @param rotY Obrót podkładki urządzenia w stopniach.
 * @param sp Program cieniujący do użycia.
 */
void drawClassicJoystick(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mJoyBase = glm::translate(mBase, glm::vec3(0.0f, 0.025f, 0.0f));
    mJoyBase = glm::scale(mJoyBase, glm::vec3(0.12f, 0.05f, 0.12f));
    drawObject(mJoyBase, texBlack, sp, 0);

    glm::mat4 mStick = glm::translate(mBase, glm::vec3(0.0f, 0.12f, 0.0f));
    mStick = glm::rotate(mStick, glm::radians(10.0f), glm::vec3(1, 0, 0));
    mStick = glm::scale(mStick, glm::vec3(0.02f, 0.15f, 0.02f));
    drawObject(mStick, texBlack, sp, 0);

    glm::mat4 mBtn = glm::translate(mBase, glm::vec3(-0.04f, 0.055f, 0.04f));
    mBtn = glm::scale(mBtn, glm::vec3(0.03f, 0.02f, 0.03f));
    drawObject(mBtn, texRed, sp, 0);
}

/**
 * @brief Renderuje stację Commodore 64 - komputer z wbudowaną klawiaturą, stację dyskietek i monitor.
 * * @param pos Centrum układu biurkowego.
 * @param rotY Obrót zestawu dla realizmu.
 * @param sp Aktualnie używany shader.
 */
void drawCommodore64(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mBody = glm::translate(mBase, glm::vec3(0.0f, 0.03f, 0.0f));
    mBody = glm::scale(mBody, glm::vec3(0.55f, 0.06f, 0.35f));
    drawObject(mBody, texC64Beige, sp, 0);
    glm::mat4 mBack = glm::translate(mBase, glm::vec3(0.0f, 0.07f, -0.1f));
    mBack = glm::scale(mBack, glm::vec3(0.55f, 0.06f, 0.15f));
    drawObject(mBack, texC64Beige, sp, 0);

    glm::mat4 mKeyArea = glm::translate(mBase, glm::vec3(0.0f, 0.075f, 0.05f));
    mKeyArea = glm::rotate(mKeyArea, glm::radians(10.0f), glm::vec3(1, 0, 0));
    mKeyArea = glm::scale(mKeyArea, glm::vec3(0.48f, 0.02f, 0.16f));
    drawObject(mKeyArea, texDarkKeys, sp, 0);

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 15; c++) {
            if (r == 3 && c > 3 && c < 11) continue;
            glm::mat4 mKey = glm::translate(mBase, glm::vec3(-0.22f + (c * 0.031f), 0.09f - (r * 0.005f), -0.01f + (r * 0.035f)));
            mKey = glm::rotate(mKey, glm::radians(10.0f), glm::vec3(1, 0, 0));
            mKey = glm::scale(mKey, glm::vec3(0.025f, 0.02f, 0.025f));
            drawObject(mKey, texBlack, sp, 0);
        }
    }
    glm::mat4 mSpace = glm::translate(mBase, glm::vec3(0.0f, 0.075f, 0.095f));
    mSpace = glm::rotate(mSpace, glm::radians(10.0f), glm::vec3(1, 0, 0));
    mSpace = glm::scale(mSpace, glm::vec3(0.2f, 0.02f, 0.025f));
    drawObject(mSpace, texBlack, sp, 0);

    glm::mat4 mMonBase = glm::translate(mBase, glm::vec3(0.0f, 0.0f, -0.35f));
    glm::mat4 mMonitor = glm::translate(mMonBase, glm::vec3(0.0f, 0.25f, 0.0f));
    mMonitor = glm::scale(mMonitor, glm::vec3(0.45f, 0.4f, 0.35f));
    drawObject(mMonitor, texC64Beige, sp, 0);

    glm::mat4 mScreen = glm::translate(mMonBase, glm::vec3(0.0f, 0.26f, 0.18f));
    mScreen = glm::scale(mScreen, glm::vec3(0.38f, 0.3f, 0.02f));
    drawObject(mScreen, texC64Screen, sp, 2);

    glm::mat4 mFloppy = glm::translate(mBase, glm::vec3(0.45f, 0.06f, 0.0f));
    mFloppy = glm::scale(mFloppy, glm::vec3(0.25f, 0.12f, 0.4f));
    drawObject(mFloppy, texC64Beige, sp, 0);
    glm::mat4 mFloppySlot = glm::translate(mBase, glm::vec3(0.45f, 0.06f, 0.205f));
    mFloppySlot = glm::scale(mFloppySlot, glm::vec3(0.18f, 0.015f, 0.01f));
    drawObject(mFloppySlot, texBlack, sp, 0);
    glm::mat4 mFloppyLed = glm::translate(mBase, glm::vec3(0.38f, 0.03f, 0.205f));
    mFloppyLed = glm::scale(mFloppyLed, glm::vec3(0.015f, 0.015f, 0.01f));
    drawObject(mFloppyLed, texRed, sp, 2);

    drawClassicJoystick(pos + glm::vec3(0.3f, 0.0f, 0.2f), 15.0f, sp);
}

/**
 * @brief Rysuje obiekt klasycznego mikrokomputera z serii Atari 8-bit.
 * * @param pos Płaska pozycja centrum w świecie.
 * @param rotY Obrót obiektu.
 * @param sp Wskaźnik programu cieniującego.
 */
void drawAtari(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mBodyFront = glm::translate(mBase, glm::vec3(0.0f, 0.025f, 0.05f));
    mBodyFront = glm::scale(mBodyFront, glm::vec3(0.62f, 0.05f, 0.28f));
    drawObject(mBodyFront, texAtariBeige, sp, 0);

    glm::mat4 mBodyBack = glm::translate(mBase, glm::vec3(0.0f, 0.055f, -0.10f));
    mBodyBack = glm::scale(mBodyBack, glm::vec3(0.62f, 0.055f, 0.22f));
    drawObject(mBodyBack, texAtariBeige, sp, 0);

    glm::mat4 mSideL = glm::translate(mBase, glm::vec3(-0.31f, 0.04f, -0.02f));
    mSideL = glm::scale(mSideL, glm::vec3(0.01f, 0.07f, 0.46f));
    drawObject(mSideL, texAtariBeige, sp, 0);

    glm::mat4 mSideR = glm::translate(mBase, glm::vec3(0.31f, 0.04f, -0.02f));
    mSideR = glm::scale(mSideR, glm::vec3(0.01f, 0.07f, 0.46f));
    drawObject(mSideR, texAtariBeige, sp, 0);

    glm::mat4 mSilver = glm::translate(mBase, glm::vec3(0.0f, 0.083f, -0.075f));
    mSilver = glm::scale(mSilver, glm::vec3(0.60f, 0.008f, 0.19f));
    drawObject(mSilver, texDesk, sp, 0);

    glm::mat4 mLogo = glm::translate(mBase, glm::vec3(-0.18f, 0.088f, -0.06f));
    mLogo = glm::scale(mLogo, glm::vec3(0.10f, 0.006f, 0.025f));
    drawObject(mLogo, texBlack, sp, 0);

    glm::mat4 mCart = glm::translate(mBase, glm::vec3(0.12f, 0.088f, -0.10f));
    mCart = glm::scale(mCart, glm::vec3(0.16f, 0.008f, 0.06f));
    drawObject(mCart, texBlack, sp, 0);

    glm::mat4 mCartLed = glm::translate(mBase, glm::vec3(0.21f, 0.088f, -0.085f));
    mCartLed = glm::scale(mCartLed, glm::vec3(0.012f, 0.008f, 0.012f));
    drawObject(mCartLed, texRed, sp, 2);

    glm::mat4 mKeyDeck = glm::translate(mBase, glm::vec3(-0.04f, 0.058f, 0.08f));
    mKeyDeck = glm::rotate(mKeyDeck, glm::radians(8.0f), glm::vec3(1, 0, 0));

    glm::mat4 mKeyArea = glm::scale(mKeyDeck, glm::vec3(0.50f, 0.005f, 0.16f));
    drawObject(mKeyArea, texBlack, sp, 0);

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 13; c++) {
            if (r == 3 && c >= 3 && c <= 9) continue;
            glm::mat4 mKey = glm::translate(mKeyDeck,
                glm::vec3(-0.185f + (c * 0.031f), 0.008f, -0.055f + (r * 0.035f)));
            mKey = glm::scale(mKey, glm::vec3(0.026f, 0.01f, 0.028f));
            drawObject(mKey, texDarkKeys, sp, 0);
        }
    }

    glm::mat4 mSpace = glm::translate(mKeyDeck, glm::vec3(-0.002f, 0.008f, 0.05f));
    mSpace = glm::scale(mSpace, glm::vec3(0.21f, 0.01f, 0.028f));
    drawObject(mSpace, texDarkKeys, sp, 0);

    GLuint fnColors[4] = { texAtariBeige, texAtariBeige, texAtariBeige, texRed };
    for (int f = 0; f < 4; f++) {
        glm::mat4 mFn = glm::translate(mKeyDeck,
            glm::vec3(0.225f, 0.008f, -0.055f + (f * 0.035f)));
        mFn = glm::scale(mFn, glm::vec3(0.034f, 0.01f, 0.028f));
        drawObject(mFn, fnColors[f], sp, f == 3 ? 0 : 0);
    }

    glm::mat4 mMonBase = glm::translate(mBase, glm::vec3(0.0f, 0.0f, -0.42f));

    glm::mat4 mMonitor = glm::translate(mMonBase, glm::vec3(0.0f, 0.26f, 0.0f));
    mMonitor = glm::scale(mMonitor, glm::vec3(0.52f, 0.44f, 0.42f));
    drawObject(mMonitor, texAtariBeige, sp, 0);

    glm::mat4 mBezel = glm::translate(mMonBase, glm::vec3(0.0f, 0.265f, 0.212f));
    mBezel = glm::scale(mBezel, glm::vec3(0.47f, 0.39f, 0.015f));
    drawObject(mBezel, texBlack, sp, 0);

    glm::mat4 mScreen = glm::translate(mMonBase, glm::vec3(0.0f, 0.268f, 0.218f));
    mScreen = glm::scale(mScreen, glm::vec3(0.40f, 0.32f, 0.012f));
    drawObject(mScreen, texAtariScreen, sp, 2);

    glm::mat4 mMonFoot = glm::translate(mMonBase, glm::vec3(0.0f, 0.03f, 0.05f));
    mMonFoot = glm::scale(mMonFoot, glm::vec3(0.30f, 0.06f, 0.20f));
    drawObject(mMonFoot, texAtariBeige, sp, 0);

    glm::mat4 mKnob1 = glm::translate(mMonBase, glm::vec3(0.265f, 0.18f, 0.18f));
    mKnob1 = glm::scale(mKnob1, glm::vec3(0.02f, 0.02f, 0.02f));
    drawObject(mKnob1, texBlack, sp, 0);

    glm::mat4 mKnob2 = glm::translate(mMonBase, glm::vec3(0.265f, 0.24f, 0.18f));
    mKnob2 = glm::scale(mKnob2, glm::vec3(0.02f, 0.02f, 0.02f));
    drawObject(mKnob2, texBlack, sp, 0);

    glm::vec4 localJoyPos = glm::vec4(0.45f, 0.0f, 0.20f, 1.0f);
    glm::vec3 joyWorldPos = glm::vec3(mBase * localJoyPos);

    drawClassicJoystick(joyWorldPos, rotY - 20.0f, sp);

    glm::mat4 mCable1 = glm::translate(mBase, glm::vec3(0.38f, 0.005f, 0.05f));
    mCable1 = glm::scale(mCable1, glm::vec3(0.14f, 0.01f, 0.01f));
    drawObject(mCable1, texBlack, sp, 0);

    glm::mat4 mCable2 = glm::translate(mBase, glm::vec3(0.45f, 0.005f, 0.125f));
    mCable2 = glm::scale(mCable2, glm::vec3(0.01f, 0.01f, 0.16f));
    drawObject(mCable2, texBlack, sp, 0);
}

/**
 * @brief Rysuje kompletne środowisko ekspozycji "Pokój Retro", zawierające biurko oraz ulokowane na nim domowe komputery 8-bitowe.
 * * @param centerPos Globalny środek pokoju.
 * @param rotY Obrót obiektu meblowego i stojącej na nim aparatury.
 * @param sp Wskaźnik do aktualnie włączonego programu GLSL.
 */
void drawRetroRoom(glm::vec3 centerPos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), centerPos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mDeskTop = glm::translate(mBase, glm::vec3(0.0f, 0.75f, 0.0f));
    mDeskTop = glm::scale(mDeskTop, glm::vec3(2.6f, 0.05f, 1.4f));
    drawObject(mDeskTop, texWood, sp, 0);

    glm::mat4 mLeg1 = glm::translate(mBase, glm::vec3(-1.2f, 0.375f, 0.0f));
    mLeg1 = glm::scale(mLeg1, glm::vec3(0.05f, 0.75f, 1.3f));
    drawObject(mLeg1, texWood, sp, 0);

    glm::mat4 mLeg2 = glm::translate(mBase, glm::vec3(1.2f, 0.375f, 0.0f));
    mLeg2 = glm::scale(mLeg2, glm::vec3(0.05f, 0.75f, 1.3f));
    drawObject(mLeg2, texWood, sp, 0);

    glm::vec3 c64Pos = glm::vec3(mBase * glm::vec4(-0.6f, 0.775f, 0.0f, 1.0f));
    drawCommodore64(c64Pos, rotY + 10.0f, sp);

    glm::vec3 atariPos = glm::vec3(mBase * glm::vec4(0.6f, 0.775f, 0.0f, 1.0f));
    drawAtari(atariPos, rotY - 10.0f, sp);
}

/**
 * @brief Animuje model potężnego, opartego o formę kostki superkomputera "Connection Machine".
 * @details Moduł ten posiada setki animowanych punktów świetlnych symulujących pracę maszyny poprzez generatory faz.
 * * @param pos Środek sześcianu.
 * @param rotY Obrót obiektu.
 * @param sp Program cieniujący używany do renderowania obudowy i diod.
 */
void drawConnectionMachine(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));
    float time = (float)glfwGetTime();

    float cubeSize = 0.75f;
    float half = cubeSize / 2.0f;

    for (int ix = 0; ix < 2; ix++) {
        for (int iy = 0; iy < 2; iy++) {
            for (int iz = 0; iz < 2; iz++) {
                float cx = (ix == 0) ? -half : half;
                float cy = (iy == 0) ? half : half * 3.0f;
                float cz = (iz == 0) ? -half : half;

                glm::mat4 mSub = glm::translate(mBase, glm::vec3(cx, cy, cz));

                glm::mat4 mBody = glm::scale(mSub, glm::vec3(cubeSize, cubeSize, cubeSize));
                drawObject(mBody, texBlack, sp, 0);

                float fh = half + 0.005f;

                if (iz == 1) {
                    glm::mat4 mFace = glm::translate(mSub, glm::vec3(0.0f, 0.0f, fh));
                    glm::mat4 mPanel = glm::scale(mFace, glm::vec3(cubeSize - 0.02f, cubeSize - 0.02f, 0.008f));
                    drawObject(mPanel, texEniacBody, sp, 0);
                    for (int row = 0; row < 8; row++) {
                        for (int col = 0; col < 8; col++) {
                            float phase = sin(time * 3.5f + row * 0.5f + col * 0.5f + ix * 1.1f + iy * 0.9f + iz * 1.3f);
                            float phase2 = sin(time * 2.1f - row * 0.4f + col * 0.6f);
                            int isLit = (phase * phase2 > 0.05f) ? 2 : 0;
                            glm::mat4 mLed = glm::translate(mFace, glm::vec3(-0.28f + col * 0.08f, -0.28f + row * 0.08f, 0.012f));
                            mLed = glm::scale(mLed, glm::vec3(0.033f, 0.033f, 0.005f));
                            drawObject(mLed, texRed, sp, isLit);
                        }
                    }
                }

                if (iz == 0) {
                    glm::mat4 mFace = glm::translate(mSub, glm::vec3(0.0f, 0.0f, -fh));
                    mFace = glm::rotate(mFace, glm::radians(180.0f), glm::vec3(0, 1, 0));
                    glm::mat4 mPanel = glm::scale(mFace, glm::vec3(cubeSize - 0.02f, cubeSize - 0.02f, 0.008f));
                    drawObject(mPanel, texEniacBody, sp, 0);
                    for (int row = 0; row < 8; row++) {
                        for (int col = 0; col < 8; col++) {
                            float phase = sin(time * 3.5f + row * 0.5f + col * 0.5f + ix * 1.1f + iy * 0.9f + iz * 1.3f + 2.0f);
                            float phase2 = sin(time * 2.1f - row * 0.4f + col * 0.6f);
                            int isLit = (phase * phase2 > 0.05f) ? 2 : 0;
                            glm::mat4 mLed = glm::translate(mFace, glm::vec3(-0.28f + col * 0.08f, -0.28f + row * 0.08f, 0.012f));
                            mLed = glm::scale(mLed, glm::vec3(0.033f, 0.033f, 0.005f));
                            drawObject(mLed, texRed, sp, isLit);
                        }
                    }
                }

                if (ix == 1) {
                    glm::mat4 mFace = glm::translate(mSub, glm::vec3(fh, 0.0f, 0.0f));
                    mFace = glm::rotate(mFace, glm::radians(90.0f), glm::vec3(0, 1, 0));
                    glm::mat4 mPanel = glm::scale(mFace, glm::vec3(cubeSize - 0.02f, cubeSize - 0.02f, 0.008f));
                    drawObject(mPanel, texEniacBody, sp, 0);
                    for (int row = 0; row < 8; row++) {
                        for (int col = 0; col < 8; col++) {
                            float phase = sin(time * 3.5f + row * 0.5f + col * 0.5f + ix * 1.1f + iy * 0.9f + iz * 1.3f + 4.0f);
                            float phase2 = sin(time * 2.1f - row * 0.4f + col * 0.6f);
                            int isLit = (phase * phase2 > 0.05f) ? 2 : 0;
                            glm::mat4 mLed = glm::translate(mFace, glm::vec3(-0.28f + col * 0.08f, -0.28f + row * 0.08f, 0.012f));
                            mLed = glm::scale(mLed, glm::vec3(0.033f, 0.033f, 0.005f));
                            drawObject(mLed, texRed, sp, isLit);
                        }
                    }
                }

                if (ix == 0) {
                    glm::mat4 mFace = glm::translate(mSub, glm::vec3(-fh, 0.0f, 0.0f));
                    mFace = glm::rotate(mFace, glm::radians(-90.0f), glm::vec3(0, 1, 0));
                    glm::mat4 mPanel = glm::scale(mFace, glm::vec3(cubeSize - 0.02f, cubeSize - 0.02f, 0.008f));
                    drawObject(mPanel, texEniacBody, sp, 0);
                    for (int row = 0; row < 8; row++) {
                        for (int col = 0; col < 8; col++) {
                            float phase = sin(time * 3.5f + row * 0.5f + col * 0.5f + ix * 1.1f + iy * 0.9f + iz * 1.3f + 6.0f);
                            float phase2 = sin(time * 2.1f - row * 0.4f + col * 0.6f);
                            int isLit = (phase * phase2 > 0.05f) ? 2 : 0;
                            glm::mat4 mLed = glm::translate(mFace, glm::vec3(-0.28f + col * 0.08f, -0.28f + row * 0.08f, 0.012f));
                            mLed = glm::scale(mLed, glm::vec3(0.033f, 0.033f, 0.005f));
                            drawObject(mLed, texRed, sp, isLit);
                        }
                    }
                }

                if (iy == 1) {
                    glm::mat4 mFace = glm::translate(mSub, glm::vec3(0.0f, fh, 0.0f));
                    mFace = glm::rotate(mFace, glm::radians(-90.0f), glm::vec3(1, 0, 0));
                    glm::mat4 mPanel = glm::scale(mFace, glm::vec3(cubeSize - 0.02f, cubeSize - 0.02f, 0.008f));
                    drawObject(mPanel, texEniacBody, sp, 0);
                    for (int g = 0; g < 5; g++) {
                        glm::mat4 mSlot = glm::translate(mFace, glm::vec3(-0.28f + g * 0.14f, 0.0f, 0.004f));
                        mSlot = glm::scale(mSlot, glm::vec3(0.04f, 0.6f, 0.004f));
                        drawObject(mSlot, texBlack, sp, 0);
                    }
                }
            }
        }
    }

    float grooveDepth = 0.03f;
    float grooveW = 0.05f;
    float grooveThin = 0.004f;

    glm::mat4 mGV = glm::translate(mBase, glm::vec3(0.0f, cubeSize, cubeSize - grooveDepth));
    mGV = glm::scale(mGV, glm::vec3(grooveW, cubeSize * 2.0f, grooveThin));
    drawObject(mGV, texEniacBody, sp, 0);
    glm::mat4 mGH = glm::translate(mBase, glm::vec3(0.0f, cubeSize, cubeSize - grooveDepth));
    mGH = glm::scale(mGH, glm::vec3(cubeSize * 2.0f, grooveW, grooveThin));
    drawObject(mGH, texEniacBody, sp, 0);

    glm::mat4 mGV2 = glm::translate(mBase, glm::vec3(0.0f, cubeSize, -(cubeSize - grooveDepth)));
    mGV2 = glm::scale(mGV2, glm::vec3(grooveW, cubeSize * 2.0f, grooveThin));
    drawObject(mGV2, texEniacBody, sp, 0);
    glm::mat4 mGH2 = glm::translate(mBase, glm::vec3(0.0f, cubeSize, -(cubeSize - grooveDepth)));
    mGH2 = glm::scale(mGH2, glm::vec3(cubeSize * 2.0f, grooveW, grooveThin));
    drawObject(mGH2, texEniacBody, sp, 0);

    glm::mat4 mGV3 = glm::translate(mBase, glm::vec3(cubeSize - grooveDepth, cubeSize, 0.0f));
    mGV3 = glm::scale(mGV3, glm::vec3(grooveThin, cubeSize * 2.0f, grooveW));
    drawObject(mGV3, texEniacBody, sp, 0);
    glm::mat4 mGH3 = glm::translate(mBase, glm::vec3(cubeSize - grooveDepth, cubeSize, 0.0f));
    mGH3 = glm::scale(mGH3, glm::vec3(grooveThin, grooveW, cubeSize * 2.0f));
    drawObject(mGH3, texEniacBody, sp, 0);

    glm::mat4 mGV4 = glm::translate(mBase, glm::vec3(-(cubeSize - grooveDepth), cubeSize, 0.0f));
    mGV4 = glm::scale(mGV4, glm::vec3(grooveThin, cubeSize * 2.0f, grooveW));
    drawObject(mGV4, texEniacBody, sp, 0);
    glm::mat4 mGH4 = glm::translate(mBase, glm::vec3(-(cubeSize - grooveDepth), cubeSize, 0.0f));
    mGH4 = glm::scale(mGH4, glm::vec3(grooveThin, grooveW, cubeSize * 2.0f));
    drawObject(mGH4, texEniacBody, sp, 0);

    float baseW = cubeSize * 2.0f + 0.1f;
    glm::mat4 mRim = glm::translate(mBase, glm::vec3(0.0f, 0.04f, 0.0f));
    mRim = glm::scale(mRim, glm::vec3(baseW, 0.08f, baseW));
    drawObject(mRim, texDesk, sp, 0);

    float hw = baseW / 2.0f - 0.08f;
    glm::mat4 mW1 = glm::translate(mBase, glm::vec3(-hw, 0.04f, -hw));
    mW1 = glm::rotate(mW1, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mW1 = glm::scale(mW1, glm::vec3(0.07f, 0.05f, 0.07f));
    drawCylinder(mW1, texBlack, sp, 0);

    glm::mat4 mW2 = glm::translate(mBase, glm::vec3(hw, 0.04f, -hw));
    mW2 = glm::rotate(mW2, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mW2 = glm::scale(mW2, glm::vec3(0.07f, 0.05f, 0.07f));
    drawCylinder(mW2, texBlack, sp, 0);

    glm::mat4 mW3 = glm::translate(mBase, glm::vec3(hw, 0.04f, hw));
    mW3 = glm::rotate(mW3, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mW3 = glm::scale(mW3, glm::vec3(0.07f, 0.05f, 0.07f));
    drawCylinder(mW3, texBlack, sp, 0);

    glm::mat4 mW4 = glm::translate(mBase, glm::vec3(-hw, 0.04f, hw));
    mW4 = glm::rotate(mW4, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mW4 = glm::scale(mW4, glm::vec3(0.07f, 0.05f, 0.07f));
    drawCylinder(mW4, texBlack, sp, 0);
}

/**
 * @brief Rysuje w muzeum legendarny superkomputer wektorowy Cray-1 (charakterystyczny walec z ławeczką z chłodzeniem).
 * * @param pos Środkowa pozycja cylindrycznej kolumny głównej maszyny.
 * @param rotY Obrót obiektu w osi Y.
 * @param sp Wskaźnik do wybranego, zbindowanego programu GLSL.
 */
void drawCray1(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));
    float time = (float)glfwGetTime();

    int numSeg = 12;
    float arcDeg = 270.0f;
    float radius = 0.9f;
    float startAngle = -135.0f;

    for (int b = 0; b < 36; b++) {
        float ba = glm::radians(b * (360.0f / 36));
        float bx = sin(ba) * 1.08f;
        float bz = cos(ba) * 1.08f;
        float bAngle = glm::degrees(ba);

        glm::mat4 mBaseSeg = glm::translate(mBase, glm::vec3(bx, 0.025f, bz));
        mBaseSeg = glm::rotate(mBaseSeg, glm::radians(bAngle), glm::vec3(0, 1, 0));
        mBaseSeg = glm::scale(mBaseSeg, glm::vec3(0.20f, 0.05f, 0.20f));
        drawObject(mBaseSeg, texDesk, sp, 0);

        glm::mat4 mBaseRim = glm::translate(mBase, glm::vec3(bx * 0.98f, 0.06f, bz * 0.98f));
        mBaseRim = glm::rotate(mBaseRim, glm::radians(bAngle), glm::vec3(0, 1, 0));
        mBaseRim = glm::scale(mBaseRim, glm::vec3(0.20f, 0.03f, 0.05f));
        drawObject(mBaseRim, texOdraPanel, sp, 0);
    }

    for (int i = 0; i < numSeg; i++) {
        float t = (float)i / (numSeg - 1);
        float angle = glm::radians(startAngle + t * arcDeg);
        float sx = sin(angle) * radius;
        float sz = cos(angle) * radius;
        float towerRotY = glm::degrees(atan2(sx, sz)) + 180.0f;

        glm::mat4 mTower = glm::translate(mBase, glm::vec3(sx, 0.0f, sz));
        mTower = glm::rotate(mTower, glm::radians(towerRotY), glm::vec3(0, 1, 0));

        glm::mat4 mCol = glm::translate(mTower, glm::vec3(0.0f, 1.15f, 0.0f));
        mCol = glm::scale(mCol, glm::vec3(0.30f, 2.30f, 0.22f));
        drawObject(mCol, texOdraFrame, sp, 0);

        glm::mat4 mSL = glm::translate(mTower, glm::vec3(-0.155f, 1.15f, 0.01f));
        mSL = glm::scale(mSL, glm::vec3(0.01f, 2.28f, 0.20f));
        drawObject(mSL, texOdraPanel, sp, 0);

        glm::mat4 mSR = glm::translate(mTower, glm::vec3(0.155f, 1.15f, 0.01f));
        mSR = glm::scale(mSR, glm::vec3(0.01f, 2.28f, 0.20f));
        drawObject(mSR, texOdraPanel, sp, 0);

        glm::mat4 mFront = glm::translate(mTower, glm::vec3(0.0f, 1.15f, -0.12f));
        mFront = glm::scale(mFront, glm::vec3(0.28f, 2.26f, 0.01f));
        drawObject(mFront, texBlack, sp, 0);

        glm::mat4 mBackPanel = glm::translate(mTower, glm::vec3(0.0f, 1.15f, 0.12f));
        mBackPanel = glm::scale(mBackPanel, glm::vec3(0.28f, 2.26f, 0.01f));
        drawObject(mBackPanel, texEniacBody, sp, 0);

        for (int strip = 0; strip < 7; strip++) {
            glm::mat4 mStrip = glm::translate(mTower, glm::vec3(0.0f, 0.30f + strip * 0.30f, 0.125f));
            mStrip = glm::scale(mStrip, glm::vec3(0.29f, 0.018f, 0.010f));
            drawObject(mStrip, texGauge, sp, 0);
        }

        for (int card = 0; card < 5; card++) {
            glm::mat4 mCard = glm::translate(mTower, glm::vec3(0.0f, 0.42f + card * 0.30f, 0.124f));
            mCard = glm::scale(mCard, glm::vec3(0.22f, 0.14f, 0.008f));
            drawObject(mCard, texDesk, sp, 0);

            for (int col = 0; col < 2; col++) {
                int isLit = (sin(time * 1.5f + i * 1.1f + card * 0.6f + col * 2.3f) > 0.4f) ? 2 : 0;
                GLuint cTex = (col == 0) ? texGreen : texYellow;
                glm::mat4 mBackChip = glm::translate(mTower, glm::vec3(-0.05f + col * 0.10f, 0.42f + card * 0.30f, 0.128f));
                mBackChip = glm::scale(mBackChip, glm::vec3(0.05f, 0.08f, 0.005f));
                drawObject(mBackChip, cTex, sp, isLit);
            }
        }

        for (int d = 0; d < 4; d++) {
            int isLit = (sin(time * 1.8f + i * 0.9f + d * 1.5f) > 0.0f) ? 2 : 0;
            GLuint ledTex = (d % 3 == 0) ? texGreen : texRed;
            glm::mat4 mLed = glm::translate(mTower, glm::vec3(-0.10f + d * 0.068f, 2.20f, 0.126f));
            mLed = glm::scale(mLed, glm::vec3(0.030f, 0.018f, 0.006f));
            drawObject(mLed, ledTex, sp, isLit);
        }

        for (int d = 0; d < 4; d++) {
            int isLit = (sin(time * 2.3f - i * 0.7f + d * 1.1f + 1.0f) > 0.15f) ? 2 : 0;
            glm::mat4 mLed = glm::translate(mTower, glm::vec3(-0.10f + d * 0.068f, 0.26f, 0.126f));
            mLed = glm::scale(mLed, glm::vec3(0.030f, 0.018f, 0.006f));
            drawObject(mLed, texYellow, sp, isLit);
        }

        for (int d = 0; d < 5; d++) {
            int isLit = (sin(time * 2.6f + i * 1.2f + d * 0.9f + 0.5f) > 0.1f) ? 2 : 0;
            GLuint dTex = (d % 3 == 0) ? texGreen : ((d % 3 == 1) ? texRed : texYellow);
            glm::mat4 mDin = glm::translate(mTower, glm::vec3(-0.08f + d * 0.04f, 2.10f, 0.126f));
            mDin = glm::scale(mDin, glm::vec3(0.018f, 0.018f, 0.006f));
            drawObject(mDin, dTex, sp, isLit);
        }
    }

    int benchSegs = 24;
    float benchRadius = radius + 0.32f;
    for (int b = 0; b < benchSegs; b++) {
        float bt = (float)b / (benchSegs - 1);
        float bangle = glm::radians(startAngle + bt * arcDeg);
        float bx = sin(bangle) * benchRadius;
        float bz = cos(bangle) * benchRadius;
        float bRotY = glm::degrees(atan2(bx, bz)) + 180.0f;

        glm::mat4 mBench = glm::translate(mBase, glm::vec3(bx, 0.0f, bz));
        mBench = glm::rotate(mBench, glm::radians(bRotY), glm::vec3(0, 1, 0));

        glm::mat4 mSeatBase = glm::translate(mBench, glm::vec3(0.0f, 0.21f, 0.0f));
        mSeatBase = glm::scale(mSeatBase, glm::vec3(0.34f, 0.42f, 0.44f));
        drawObject(mSeatBase, texDesk, sp, 0);

        glm::mat4 mSeat = glm::translate(mBench, glm::vec3(0.0f, 0.44f, 0.0f));
        mSeat = glm::scale(mSeat, glm::vec3(0.34f, 0.05f, 0.44f));
        drawObject(mSeat, texOdraPanel, sp, 0);

        glm::mat4 mSeatTrim = glm::translate(mBench, glm::vec3(0.0f, 0.025f, 0.21f));
        mSeatTrim = glm::scale(mSeatTrim, glm::vec3(0.32f, 0.05f, 0.03f));
        drawObject(mSeatTrim, texBlack, sp, 0);
    }
}

/**
 * @brief Rysuje naścienny, interaktywny włącznik światła.
 * Dźwignia animuje się w górę lub w dół, na podstawie fizycznego natężenia aktualnego oświetlenia.
 * * @param pos Punkt montażowy na ścianie.
 * @param rotY Obrót dopasowujący do wybranej ściany (0, 90, 180 lub -90 stopni).
 * @param sp Wskaźnik do aktualnie włączonego programu GLSL.
 */
void drawLightSwitch(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mFrame = glm::scale(mBase, glm::vec3(0.12f, 0.16f, 0.01f));
    drawObject(mFrame, texCeiling, sp, 0);

    float animVal = (currentBrightness - 0.2f) / 0.8f;
    if (animVal > 1.0f) animVal = 1.0f;
    if (animVal < 0.0f) animVal = 0.0f;

    float switchAngle = -15.0f + (animVal * 30.0f);

    glm::mat4 mBtnBase = glm::translate(mBase, glm::vec3(0.0f, 0.0f, 0.005f));
    glm::mat4 mBtn = glm::rotate(mBtnBase, glm::radians(switchAngle), glm::vec3(1, 0, 0));
    mBtn = glm::scale(mBtn, glm::vec3(0.06f, 0.08f, 0.015f));
    drawObject(mBtn, texDesk, sp, 0);
}

/**
 * @brief Rysuje animowaną proceduralnie kamerę przemysłową (CCTV) z elementami szkła oraz świecącą diodą kontrolną.
 * * @param pos Punkt przymocowania u sufitu.
 * @param baseRotY Spoczynkowy obrót celowania kamery.
 * @param sp Parametr programu cieniującego.
 */
void drawCamera(glm::vec3 pos, float baseRotY, ShaderProgram* sp) {
    float time = (float)glfwGetTime();
    float swing = sin(time * 0.8f + baseRotY * 0.05f) * 20.0f;
    float rotY = baseRotY + swing;

    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);

    glm::mat4 mMount = glm::translate(mBase, glm::vec3(0.0f, -0.12f, 0.0f));
    mMount = glm::scale(mMount, glm::vec3(0.06f, 0.24f, 0.06f));
    drawObject(mMount, texBlack, sp, 0);

    glm::mat4 mPivot = glm::translate(mBase, glm::vec3(0.0f, -0.26f, 0.0f));
    mPivot = glm::rotate(mPivot, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mBall = glm::scale(mPivot, glm::vec3(0.08f, 0.08f, 0.08f));
    drawObject(mBall, texDesk, sp, 0);

    glm::mat4 mBody = glm::translate(mPivot, glm::vec3(0.0f, -0.04f, 0.06f));
    mBody = glm::scale(mBody, glm::vec3(0.14f, 0.10f, 0.18f));
    drawObject(mBody, texBlack, sp, 0);

    glm::mat4 mLens = glm::translate(mPivot, glm::vec3(0.0f, -0.04f, 0.16f));
    mLens = glm::rotate(mLens, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mLens = glm::scale(mLens, glm::vec3(0.05f, 0.06f, 0.05f));
    drawCylinder(mLens, texBlack, sp, 0);

    glm::mat4 mGlass = glm::translate(mPivot, glm::vec3(0.0f, -0.04f, 0.195f));
    mGlass = glm::rotate(mGlass, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mGlass = glm::scale(mGlass, glm::vec3(0.032f, 0.008f, 0.032f));
    drawCylinder(mGlass, texEniacBody, sp, 0);

    int ledOn = (sin(time * 2.0f + baseRotY) > 0.6f) ? 2 : 0;
    glm::mat4 mLed = glm::translate(mPivot, glm::vec3(0.05f, -0.005f, 0.155f));
    mLed = glm::scale(mLed, glm::vec3(0.018f, 0.018f, 0.010f));
    drawObject(mLed, texRed, sp, ledOn);

    glm::mat4 mCable = glm::translate(mBase, glm::vec3(0.0f, -0.06f, 0.0f));
    mCable = glm::scale(mCable, glm::vec3(0.012f, 0.12f, 0.012f));
    drawObject(mCable, texBlack, sp, 0);
}

/**
 * @brief Rysuje fizyczny obiekt parkowej, drewniano-metalowej ławki pod ścianą muzeum.
 * * @param pos Centralny środek desek siedliskowych.
 * @param rotY Obrót pionowy ustalający ułożenie (najczęściej przylega wzdłuż ściany).
 * @param sp Wskaźnik odwołujący się do zadanego programu GLSL.
 */
void drawBench(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    glm::mat4 mSeat = glm::translate(mBase, glm::vec3(0.0f, 0.45f, 0.0f));
    mSeat = glm::scale(mSeat, glm::vec3(2.4f, 0.05f, 0.5f));
    drawObject(mSeat, texWood, sp, 0);

    for (int i = -1; i <= 1; i += 2) {
        glm::mat4 mLegFront = glm::translate(mBase, glm::vec3(1.0f * i, 0.225f, 0.15f));
        mLegFront = glm::scale(mLegFront, glm::vec3(0.05f, 0.45f, 0.05f));
        drawObject(mLegFront, texBlack, sp, 0);

        glm::mat4 mLegBack = glm::translate(mBase, glm::vec3(1.0f * i, 0.225f, -0.15f));
        mLegBack = glm::scale(mLegBack, glm::vec3(0.05f, 0.45f, 0.05f));
        drawObject(mLegBack, texBlack, sp, 0);
    }
}

/**
 * @brief Rysuje w pełni modelowaną siatkę parametryczną człowieka wykorzystywanego w roli niezależnego NPC.
 * @details Funkcja zintegrowana ma proste odtwarzanie animacji (chodzenie vs pozycja statyczna vs siedzenie na ławce).
 * Przebudowuje również wewnętrzne tablice boksów kolizji dla wygenerowanej postury, aby gracz przez niego nie przenikał.
 * * @param pos Miejsce na podłodze, od którego budowana jest hierarchia pozycjonowania ludzika.
 * @param faceYaw Określa ostateczny kierunek obrotu w przestrzeni w jakim patrzy aktualnie dany NPC.
 * @param speed Pływająca zmienna modulująca fizyczną szybkość kroku odzwierciedlaną animacją kończyn i tułowia.
 * @param walking Flaga logiczna wyzwalająca procedurę rozciągającą sinusoidy animacji kroku na kąty zgięć kości.
 * @param sp Wskaźnik shadera nakładającego odpowiednie kolory lub pliki na elementy składowe (koszula/skóra/włosy).
 * @param variant Ustalony numeryczny identyfikator koloru tekstury ubioru pozwalający oddzielić typy ludzików.
 * @param sitting Zabezpieczenie nakazujące zmianę szkieletu spoczynkowego z pozycji wertykalnej na wygięcie nóg.
 * @return Struct AABB Pudełko opisujące kolizje postaci niezbędne do wypełnienia globalnej tablicy kolizji otoczenia.
 */
AABB drawHuman(glm::vec3 pos, float faceYaw, float speed, bool walking, ShaderProgram* sp, int variant = 0, bool sitting = false) {
    float time = (float)glfwGetTime();
    const float S = 1.3f;

    float swing = walking ? sin(time * speed * 8.0f) * 35.0f : 0.0f;
    float bob = walking ? abs(sin(time * speed * 8.0f)) * 0.04f * S : 0.0f;

    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(faceYaw), glm::vec3(0, 1, 0));

    GLuint shirtTex = (variant == 0) ? texOdraPanel : (variant == 1) ? texBlue : (variant == 2) ? texGreen : (variant == 3) ? texDesk : texRed;
    GLuint pantsTex = (variant < 4) ? texBlack : texEniacBody;
    GLuint skinTex = (variant == 2) ? texGauge : texCard;
    GLuint hairTex = (variant == 0) ? texBlack : (variant == 1) ? texYellow : (variant == 2) ? texBlack : (variant == 3) ? texRed : texEniacBody;
    GLuint shoesTex = texBlack;

    float sitOffset = sitting ? -0.15f * S : 0.0f;

    for (int leg = 0; leg < 2; leg++) {
        float lx = (leg == 0) ? -0.10f * S : 0.10f * S;
        float lSwing = (leg == 0) ? swing : -swing;

        glm::mat4 mLegRoot = glm::translate(mBase, glm::vec3(lx, 0.52f * S + bob + sitOffset, 0.0f));

        if (sitting) {
            glm::mat4 mThigh = glm::translate(mLegRoot, glm::vec3(0.0f, 0.0f, 0.14f * S));
            mThigh = glm::rotate(mThigh, glm::radians(90.0f), glm::vec3(1, 0, 0));
            mThigh = glm::scale(mThigh, glm::vec3(0.155f * S, 0.28f * S, 0.155f * S));
            drawObject(mThigh, pantsTex, sp, 0);

            glm::mat4 mShin = glm::translate(mLegRoot, glm::vec3(0.0f, -0.20f * S, 0.28f * S));
            mShin = glm::scale(mShin, glm::vec3(0.13f * S, 0.24f * S, 0.13f * S));
            drawObject(mShin, pantsTex, sp, 0);

            glm::mat4 mFoot = glm::translate(mLegRoot, glm::vec3(0.0f, -0.34f * S, 0.32f * S));
            mFoot = glm::scale(mFoot, glm::vec3(0.14f * S, 0.07f * S, 0.20f * S));
            drawObject(mFoot, shoesTex, sp, 0);
        }
        else {
            mLegRoot = glm::rotate(mLegRoot, glm::radians(lSwing), glm::vec3(1, 0, 0));

            glm::mat4 mThigh = glm::translate(mLegRoot, glm::vec3(0.0f, -0.14f * S, 0.0f));
            mThigh = glm::scale(mThigh, glm::vec3(0.155f * S, 0.28f * S, 0.155f * S));
            drawObject(mThigh, pantsTex, sp, 0);

            glm::mat4 mShin = glm::translate(mLegRoot, glm::vec3(0.0f, -0.36f * S, 0.0f));
            mShin = glm::scale(mShin, glm::vec3(0.13f * S, 0.24f * S, 0.13f * S));
            drawObject(mShin, pantsTex, sp, 0);

            glm::mat4 mFoot = glm::translate(mLegRoot, glm::vec3(0.0f, -0.50f * S, 0.04f * S));
            mFoot = glm::scale(mFoot, glm::vec3(0.14f * S, 0.07f * S, 0.20f * S));
            drawObject(mFoot, shoesTex, sp, 0);
        }
    }

    glm::mat4 mTorso = glm::translate(mBase, glm::vec3(0.0f, 0.78f * S + bob + sitOffset, 0.0f));

    glm::mat4 mBody = glm::scale(mTorso, glm::vec3(0.38f * S, 0.52f * S, 0.20f * S));
    drawObject(mBody, shirtTex, sp, 0);

    glm::mat4 mCollar = glm::translate(mTorso, glm::vec3(0.0f, 0.22f * S, 0.06f * S));
    mCollar = glm::scale(mCollar, glm::vec3(0.18f * S, 0.06f * S, 0.06f * S));
    drawObject(mCollar, texCeiling, sp, 0);

    for (int arm = 0; arm < 2; arm++) {
        float ax = (arm == 0) ? -0.24f * S : 0.24f * S;
        float aBase = sitting ? -15.0f : 0.0f;
        float aSwing = (arm == 0) ? -swing : swing;

        glm::mat4 mArmRoot = glm::translate(mTorso, glm::vec3(ax, 0.16f * S, 0.0f));
        mArmRoot = glm::rotate(mArmRoot, glm::radians(aBase + aSwing), glm::vec3(1, 0, 0));

        glm::mat4 mUpperArm = glm::translate(mArmRoot, glm::vec3(0.0f, -0.12f * S, 0.0f));
        mUpperArm = glm::scale(mUpperArm, glm::vec3(0.11f * S, 0.24f * S, 0.11f * S));
        drawObject(mUpperArm, shirtTex, sp, 0);

        glm::mat4 mForeArm = glm::translate(mArmRoot, glm::vec3(0.0f, -0.30f * S, 0.0f));
        mForeArm = glm::scale(mForeArm, glm::vec3(0.095f * S, 0.20f * S, 0.095f * S));
        drawObject(mForeArm, skinTex, sp, 0);

        glm::mat4 mHand = glm::translate(mArmRoot, glm::vec3(0.0f, -0.44f * S, 0.0f));
        mHand = glm::scale(mHand, glm::vec3(0.10f * S, 0.09f * S, 0.08f * S));
        drawObject(mHand, skinTex, sp, 0);
    }

    glm::mat4 mNeck = glm::translate(mTorso, glm::vec3(0.0f, 0.30f * S, 0.0f));
    mNeck = glm::scale(mNeck, glm::vec3(0.10f * S, 0.10f * S, 0.10f * S));
    drawObject(mNeck, skinTex, sp, 0);

    glm::mat4 mHeadRoot = glm::translate(mTorso, glm::vec3(0.0f, 0.42f * S, 0.0f));

    glm::mat4 mHead = glm::scale(mHeadRoot, glm::vec3(0.24f * S, 0.26f * S, 0.22f * S));
    drawObject(mHead, skinTex, sp, 0);

    glm::mat4 mHairTop = glm::translate(mHeadRoot, glm::vec3(0.0f, 0.10f * S, -0.01f * S));
    mHairTop = glm::scale(mHairTop, glm::vec3(0.25f * S, 0.10f * S, 0.23f * S));
    drawObject(mHairTop, hairTex, sp, 0);

    glm::mat4 mHairBack = glm::translate(mHeadRoot, glm::vec3(0.0f, 0.04f * S, -0.115f * S));
    mHairBack = glm::scale(mHairBack, glm::vec3(0.23f * S, 0.20f * S, 0.04f * S));
    drawObject(mHairBack, hairTex, sp, 0);

    glm::mat4 mHairSL = glm::translate(mHeadRoot, glm::vec3(-0.118f * S, 0.02f * S, -0.02f * S));
    mHairSL = glm::scale(mHairSL, glm::vec3(0.04f * S, 0.18f * S, 0.18f * S));
    drawObject(mHairSL, hairTex, sp, 0);

    glm::mat4 mHairSR = glm::translate(mHeadRoot, glm::vec3(0.118f * S, 0.02f * S, -0.02f * S));
    mHairSR = glm::scale(mHairSR, glm::vec3(0.04f * S, 0.18f * S, 0.18f * S));
    drawObject(mHairSR, hairTex, sp, 0);

    for (int eye = 0; eye < 2; eye++) {
        float ex = (eye == 0) ? -0.055f * S : 0.055f * S;
        glm::mat4 mEyeWhite = glm::translate(mHeadRoot, glm::vec3(ex, 0.04f * S, 0.112f * S));
        mEyeWhite = glm::scale(mEyeWhite, glm::vec3(0.045f * S, 0.038f * S, 0.012f * S));
        drawObject(mEyeWhite, texCeiling, sp, 0);

        glm::mat4 mPupil = glm::translate(mHeadRoot, glm::vec3(ex, 0.04f * S, 0.118f * S));
        mPupil = glm::scale(mPupil, glm::vec3(0.022f * S, 0.022f * S, 0.008f * S));
        drawObject(mPupil, texBlack, sp, 0);
    }

    glm::mat4 mBrowL = glm::translate(mHeadRoot, glm::vec3(-0.055f * S, 0.075f * S, 0.113f * S));
    mBrowL = glm::scale(mBrowL, glm::vec3(0.048f * S, 0.012f * S, 0.008f * S));
    drawObject(mBrowL, hairTex, sp, 0);

    glm::mat4 mBrowR = glm::translate(mHeadRoot, glm::vec3(0.055f * S, 0.075f * S, 0.113f * S));
    mBrowR = glm::scale(mBrowR, glm::vec3(0.048f * S, 0.012f * S, 0.008f * S));
    drawObject(mBrowR, hairTex, sp, 0);

    glm::mat4 mNose = glm::translate(mHeadRoot, glm::vec3(0.0f, 0.01f * S, 0.118f * S));
    mNose = glm::scale(mNose, glm::vec3(0.022f * S, 0.030f * S, 0.018f * S));
    drawObject(mNose, skinTex, sp, 0);

    glm::mat4 mMouth = glm::translate(mHeadRoot, glm::vec3(0.0f, -0.038f * S, 0.114f * S));
    mMouth = glm::scale(mMouth, glm::vec3(0.085f * S, 0.014f * S, 0.010f * S));
    drawObject(mMouth, texOdraPanel, sp, 0);

    for (int corner = 0; corner < 2; corner++) {
        float cx = (corner == 0) ? -0.046f * S : 0.046f * S;
        glm::mat4 mCorner = glm::translate(mHeadRoot, glm::vec3(cx, -0.030f * S, 0.113f * S));
        mCorner = glm::scale(mCorner, glm::vec3(0.016f * S, 0.016f * S, 0.009f * S));
        drawObject(mCorner, texOdraPanel, sp, 0);
    }

    glm::mat4 mEarL = glm::translate(mHeadRoot, glm::vec3(-0.122f * S, 0.01f * S, 0.0f));
    mEarL = glm::scale(mEarL, glm::vec3(0.020f * S, 0.040f * S, 0.025f * S));
    drawObject(mEarL, skinTex, sp, 0);

    glm::mat4 mEarR = glm::translate(mHeadRoot, glm::vec3(0.122f * S, 0.01f * S, 0.0f));
    mEarR = glm::scale(mEarR, glm::vec3(0.020f * S, 0.040f * S, 0.025f * S));
    drawObject(mEarR, skinTex, sp, 0);

    float hw2 = 0.22f * S;
    float hd = 0.18f * S;
    return createBox(pos.x, pos.z, hw2 * 2.0f, hd * 2.0f);
}

/**
 * @brief Główna funkcja dokonująca kompozycji (renderowania) klatki.
 * @details Konfiguruje macierze kamery, przelicza trajektorię autonomicznych NPC przebywających w muzeum,
 * renderuje bryłę budynku, a następnie wstawia eksponaty (w tym te z elementami fizycznie ruchomymi/świecącymi).
 * Na koniec analizuje ustawienie kamery w poszukiwaniu najbliższego oglądanego obiektu.
 * * @param window Główny wskaźnik okna renderującego GLFW przekazywany dla ustalenia proporcji klatki.
 */
void drawScene(GLFWwindow* window) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float time = (float)glfwGetTime();
    static float lastT = (float)glfwGetTime();
    float dt = time - lastT;
    lastT = time;

    // --- LOGIKA AI PRZEWODNIKA ---
    static int currentWP = 0;
    static glm::vec3 currentPos = glm::vec3(-4.0f, 0.0f, -4.0f);
    static float currentYaw = 180.0f;
    static float targetYaw1 = 180.0f;
    static float waitTimer = 4.0f;
    static bool  isWalking = false;

    // Trasa patrolowa wyznaczona węzłami wektorowymi
    static std::vector<glm::vec3> waypoints = {
        glm::vec3(-4.0f, 0.0f, -4.0f), // Przed ENIAC
        glm::vec3(0.0f,  0.0f, -5.0f), // Drzwi Północne
        glm::vec3(4.0f,  0.0f, -4.0f), // Przed Odra 1305
        glm::vec3(5.0f,  0.0f,  0.0f), // Drzwi Wschodnie
        glm::vec3(3.0f,  0.0f,  3.0f), // Retro Pokój
        glm::vec3(0.0f,  0.0f,  5.0f), // Drzwi Południowe
        glm::vec3(-4.0f, 0.0f,  4.0f), // Superkomputery
        glm::vec3(-5.0f, 0.0f,  0.0f), // Drzwi Zachodnie
    };

    if (waitTimer > 0.0f) {
        waitTimer -= dt;
        isWalking = false;
        float lookYaws[] = { 180.0f, 0.0f, 135.0f, 90.0f, 45.0f, 0.0f, -45.0f, -90.0f };
        targetYaw1 = lookYaws[currentWP];
    }
    else {
        int nextWP = (currentWP + 1) % (int)waypoints.size();
        glm::vec3 dir = waypoints[nextWP] - currentPos;
        float dist = glm::length(dir);

        targetYaw1 = glm::degrees(atan2(dir.x, dir.z));

        float yawDiffToWalk = targetYaw1 - currentYaw;
        while (yawDiffToWalk > 180.0f) yawDiffToWalk -= 360.0f;
        while (yawDiffToWalk < -180.0f) yawDiffToWalk += 360.0f;

        if (abs(yawDiffToWalk) < 30.0f) {
            if (dist < 0.1f) {
                isWalking = false;
                currentWP = nextWP;
                currentPos = waypoints[nextWP];

                if (currentWP % 2 == 0) waitTimer = 4.0f;
                else waitTimer = 0.0f;
            }
            else {
                isWalking = true;
                dir = glm::normalize(dir);
                currentPos += dir * 1.4f * dt;
            }
        }
        else {
            isWalking = false;
        }
    }

    float t1YawDiff = targetYaw1 - currentYaw;
    while (t1YawDiff > 180.0f) t1YawDiff -= 360.0f;
    while (t1YawDiff < -180.0f) t1YawDiff += 360.0f;
    currentYaw += t1YawDiff * 5.0f * dt;


    // --- AI PRZECHODNIA: ENIAC ---
    static glm::vec3 t2Pos = glm::vec3(-7.0f, 0.0f, -6.8f);
    static float t2Yaw = 90.0f;
    static float t2Wait = 0.0f;
    static int t2Dir = 1;
    static bool t2VisitedCenter = false;
    bool t2Walking = false;

    if (t2Wait > 0.0f) {
        t2Wait -= dt;
        t2Walking = false;
        t2Yaw = 180.0f;
    }
    else {
        t2Walking = true;
        t2Yaw = (t2Dir == 1) ? 90.0f : -90.0f;

        float move = 1.2f * dt * t2Dir;
        t2Pos.x += move;

        if (!t2VisitedCenter && ((t2Dir == 1 && t2Pos.x >= -5.0f) || (t2Dir == -1 && t2Pos.x <= -5.0f))) {
            t2Pos.x = -5.0f;
            t2VisitedCenter = true;
            t2Wait = 4.0f;
        }

        if (t2Pos.x > -3.0f) {
            t2Pos.x = -3.0f;
            t2Dir = -1;
            t2VisitedCenter = false;
            t2Wait = 4.0f;
        }
        else if (t2Pos.x < -7.0f) {
            t2Pos.x = -7.0f;
            t2Dir = 1;
            t2VisitedCenter = false;
            t2Wait = 4.0f;
        }
    }


    // --- AI PRZECHODNIA: MASZYNA CRAY ---
    static float crayAngle = 0.0f;
    static float crayWait = 0.0f;
    static float crayTargetYaw = 90.0f;
    static float crayYaw = 90.0f;
    static float angleAccumulator = 0.0f;
    bool crayWalking = false;
    glm::vec3 crayCenter(-4.5f, 0.0f, 6.5f);

    if (crayWait > 0.0f) {
        crayWait -= dt;
        crayWalking = false;
        crayTargetYaw = glm::degrees(crayAngle) + 180.0f;
    }
    else {
        crayTargetYaw = glm::degrees(crayAngle) + 90.0f;

        float yawDiffToWalk = crayTargetYaw - crayYaw;
        while (yawDiffToWalk > 180.0f) yawDiffToWalk -= 360.0f;
        while (yawDiffToWalk < -180.0f) yawDiffToWalk += 360.0f;

        if (abs(yawDiffToWalk) < 30.0f) {
            crayWalking = true;
            float moveSpeed = 0.6f * dt;
            crayAngle += moveSpeed;
            angleAccumulator += moveSpeed;

            if (angleAccumulator >= 1.5708f) {
                angleAccumulator -= 1.5708f;
                crayWait = 4.0f;
                crayWalking = false;
            }
        }
        else {
            crayWalking = false;
        }
    }

    float crayYawDiff = crayTargetYaw - crayYaw;
    while (crayYawDiff > 180.0f) crayYawDiff -= 360.0f;
    while (crayYawDiff < -180.0f) crayYawDiff += 360.0f;
    crayYaw += crayYawDiff * 5.0f * dt;
    glm::vec3 crayWalker = crayCenter + glm::vec3(sin(crayAngle) * 2.2f, 0.0f, cos(crayAngle) * 2.2f);


    // --- AI PRZECHODNIA: POKÓJ RETRO ---
    static glm::vec3 t6Pos = glm::vec3(4.0f, 0.0f, 3.6f);
    static float t6Yaw = 90.0f;
    static float t6TargetYaw = 90.0f;
    static float t6Wait = 0.0f;
    static int t6Dir = -1;
    static bool t6VisitedCenter = false;
    bool t6Walking = false;

    if (t6Wait > 0.0f) {
        t6Wait -= dt;
        t6Walking = false;
        t6TargetYaw = 0.0f;
    }
    else {
        t6TargetYaw = (t6Dir == 1) ? 90.0f : -90.0f;

        float yawDiffToWalk = t6TargetYaw - t6Yaw;
        while (yawDiffToWalk > 180.0f) yawDiffToWalk -= 360.0f;
        while (yawDiffToWalk < -180.0f) yawDiffToWalk += 360.0f;

        if (abs(yawDiffToWalk) < 30.0f) {
            t6Walking = true;
            float move = 1.0f * dt * t6Dir;
            t6Pos.x += move;

            if (!t6VisitedCenter && ((t6Dir == 1 && t6Pos.x >= 5.0f) || (t6Dir == -1 && t6Pos.x <= 5.0f))) {
                t6Pos.x = 5.0f;
                t6VisitedCenter = true;
                t6Wait = 4.0f; // Patrzy na środek
            }
            if (t6Pos.x > 6.0f) {
                t6Pos.x = 6.0f;
                t6Dir = -1;
                t6VisitedCenter = false;
                t6Wait = 8.0f; // Dłuższy postój
            }
            else if (t6Pos.x < 4.0f) {
                t6Pos.x = 4.0f;
                t6Dir = 1;
                t6VisitedCenter = false;
                t6Wait = 8.0f; // Dłuższy postój
            }
        }
    }

    float t6YawDiff = t6TargetYaw - t6Yaw;
    while (t6YawDiff > 180.0f) t6YawDiff -= 360.0f;
    while (t6YawDiff < -180.0f) t6YawDiff += 360.0f;
    t6Yaw += t6YawDiff * 5.0f * dt;

    // Przesłanie skrzynek detekcyjnych do wektora kolizji
    glm::vec3 odraPos(7.2f, 0.0f, -5.4f);
    float odraYaw = 75.0f;
    glm::vec3 cmPos(-6.0f, 0.0f, 3.5f);
    float cmYaw = -90.0f;
    glm::vec3 benchPos(0.6f, 0.0f, -2.0f);

    npcBoxes.clear();
    const float S = 1.3f;
    float hw = 0.22f * S;
    float hd = 0.18f * S;

    npcBoxes.push_back(createBox(odraPos.x, odraPos.z, hw * 2.0f, hd * 2.0f));
    npcBoxes.push_back(createBox(cmPos.x, cmPos.z, hw * 2.0f, hd * 2.0f));
    npcBoxes.push_back(createBox(benchPos.x, benchPos.z, 0.8f, 2.6f));

    npcBoxes.push_back(createBox(currentPos.x, currentPos.z, hw * 2.0f, hd * 2.0f));
    npcBoxes.push_back(createBox(t2Pos.x, t2Pos.z, hw * 2.0f, hd * 2.0f));
    npcBoxes.push_back(createBox(crayWalker.x, crayWalker.z, hw * 2.0f, hd * 2.0f));
    npcBoxes.push_back(createBox(t6Pos.x, t6Pos.z, hw * 2.0f, hd * 2.0f));

    // --- LOGIKA AKTYWOWANIA OPISÓW Z SYSTEMEM WZROKU (DOT PRODUCT) ---
    glm::vec2 playerPos2D = glm::vec2(cameraPos.x, cameraPos.z);

    // Rzutowanie wektora wzroku (środek ekranu) na ustrój dwuwymiarowy
    glm::vec2 camFront2D = glm::vec2(cameraFront.x, cameraFront.z);
    if (glm::length(camFront2D) > 0.001f) {
        camFront2D = glm::normalize(camFront2D);
    }

    int nearExhibit = -1;
    for (int i = 0; i < (int)exhibits.size(); i++) {
        glm::vec2 dirToExhibit = exhibits[i].pos2D - playerPos2D;
        float dist = glm::length(dirToExhibit);

        if (dist < exhibits[i].radius) {
            // Unikamy dzielenia przez zero przy drastycznie bliskich odległościach
            if (dist < 0.1f) {
                nearExhibit = i;
                break;
            }
            else {
                dirToExhibit /= dist;
                // Zbadanie zbieżności kąta w oparciu o iloczyn skalarny (~53 stopnie pola widzenia)
                if (glm::dot(camFront2D, dirToExhibit) > 0.6f) {
                    nearExhibit = i;
                    break;
                }
            }
        }
    }
    activeExhibit = nearExhibit;
    glfwSetWindowTitle(window, activeExhibit >= 0 ? exhibits[activeExhibit].name : "Muzeum Maszyn Cyfrowych");


    // Zmienne środowiskowe wysyłane z poziomu procesora do sprzętowego potoku cieniowania
    currentBrightness += (targetBrightness - currentBrightness) * 0.02f;

    spLambert->use();
    glUniform1f(spLambert->u("soundVolume"), currentBrightness);

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);

    glm::mat4 V = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    if (height == 0) height = 1;
    float aspectRatio = (float)width / (float)height;

    glm::mat4 P = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, 100.0f);

    glUniformMatrix4fv(spLambert->u("P"), 1, false, glm::value_ptr(P));
    glUniformMatrix4fv(spLambert->u("V"), 1, false, glm::value_ptr(V));

    glm::vec4 lightPos[4] = {
        glm::vec4(-5.0f, 3.8f, -5.0f, 1.0f), // Pokój z ENIAC
        glm::vec4(5.0f, 3.8f, -5.0f, 1.0f), // Pokój z Odra 1305
        glm::vec4(-5.0f, 3.8f,  5.0f, 1.0f), // Pokój Superkomputerów (Cray/CM)
        glm::vec4(5.0f, 3.8f,  5.0f, 1.0f)  // Pokój Retro
    };
    glUniform4fv(spLambert->u("lightPositions"), 4, glm::value_ptr(lightPos[0]));

    // Rysowanie budynku i bryły muzeum
    glm::mat4 mFloor = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec3(20.0f, 0.1f, 20.0f));
    drawObject(mFloor, texFloor, spLambert);

    glm::mat4 mCeiling = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.05f, 0.0f)), glm::vec3(20.0f, 0.1f, 20.0f));
    drawObject(mCeiling, texCeiling, spLambert);

    for (int i = 0; i < 4; i++) {
        glm::mat4 mLamp = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(lightPos[i].x, 3.98f, lightPos[i].z)), glm::vec3(1.2f, 0.05f, 1.2f));
        int isLampOn = (currentBrightness > 0.5f) ? 1 : 0;
        drawObject(mLamp, texLamp, spLambert, isLampOn);
    }

    glm::mat4 mOuterN = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, -10.0f)), glm::vec3(20.0f, 4.0f, 0.5f));
    drawObject(mOuterN, texWall, spLambert);
    glm::mat4 mOuterS = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 10.0f)), glm::vec3(20.0f, 4.0f, 0.5f));
    drawObject(mOuterS, texWall, spLambert);
    glm::mat4 mOuterW = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-10.0f, 2.0f, 0.0f)), glm::vec3(0.5f, 4.0f, 20.0f));
    drawObject(mOuterW, texWall, spLambert);
    glm::mat4 mOuterE = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 2.0f, 0.0f)), glm::vec3(0.5f, 4.0f, 20.0f));
    drawObject(mOuterE, texWall, spLambert);

    glm::mat4 mCrossX = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(8.0f, 4.0f, 0.5f));
    drawObject(mCrossX, texBricks, spLambert);
    glm::mat4 mCrossZ = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(0.5f, 4.0f, 8.0f));
    drawObject(mCrossZ, texBricks, spLambert);

    glm::mat4 mDoorX1 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-8.0f, 2.0f, 0.0f)), glm::vec3(4.0f, 4.0f, 0.5f));
    drawObject(mDoorX1, texBricks, spLambert);
    glm::mat4 mDoorX2 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(8.0f, 2.0f, 0.0f)), glm::vec3(4.0f, 4.0f, 0.5f));
    drawObject(mDoorX2, texBricks, spLambert);
    glm::mat4 mDoorZ1 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, -8.0f)), glm::vec3(0.5f, 4.0f, 4.0f));
    drawObject(mDoorZ1, texBricks, spLambert);
    glm::mat4 mDoorZ2 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 8.0f)), glm::vec3(0.5f, 4.0f, 4.0f));
    drawObject(mDoorZ2, texBricks, spLambert);

    for (const auto& sw : lightSwitches) {
        drawLightSwitch(sw.pos, sw.rotY, spLambert);
    }

    drawBench(benchPos, 90.0f, spLambert);

    // Wywołania układające przygotowane modele ekspozycji
    drawUltimateEniac(glm::vec3(-5.0f, 0.0f, -6.0f), spLambert);
    drawOdra1305(glm::vec3(5.0f, 0.0f, -5.0f), spLambert);
    drawRetroRoom(glm::vec3(5.0f, 0.0f, 5.0f), 180.0f, spLambert);
    drawConnectionMachine(glm::vec3(-8.0f, 0.0f, 4.0f), 15.0f, spLambert);
    drawCray1(glm::vec3(-4.5f, 0.0f, 6.5f), -45.0f, spLambert);

    // Rysowanie zinstancjowanych struktur ludzkich i ich zadań wektorowych
    drawHuman(currentPos, currentYaw, 1.0f, isWalking, spLambert, 0);
    drawHuman(t2Pos, t2Yaw, 0.9f, t2Walking, spLambert, 1);
    drawHuman(odraPos, odraYaw, 0.0f, false, spLambert, 2);
    drawHuman(crayWalker, crayYaw, 1.0f, crayWalking, spLambert, 3);
    drawHuman(cmPos, cmYaw, 0.0f, false, spLambert, 4);
    drawHuman(t6Pos, t6Yaw, 0.8f, t6Walking, spLambert, 1);

    drawHuman(benchPos + glm::vec3(0.0f, 0.0f, -0.6f), 90.0f, 0.0f, false, spLambert, 1, true);
    drawHuman(benchPos + glm::vec3(0.0f, 0.0f, 0.6f), 110.0f, 0.0f, false, spLambert, 3, true);

    // Kamery strzeżące monitoringu
    drawCamera(glm::vec3(-0.38f, 3.95f, -0.38f), 225.0f, spLambert);
    drawCamera(glm::vec3(0.38f, 3.95f, -0.38f), 135.0f, spLambert);
    drawCamera(glm::vec3(-0.38f, 3.95f, 0.38f), 315.0f, spLambert);
    drawCamera(glm::vec3(0.38f, 3.95f, 0.38f), 45.0f, spLambert);
}

/**
 * @brief Główna funkcja programu - punkt wejścia.
 * @details Konfiguruje okno menedżera wyświetlania, podłącza zewnętrzne biblioteki (GLEW, GLFW, Miniaudio, ImGui, Vosk),
 * inicjalizuje stan shaderów, a następnie utrzymuje główną pętlę zdarzeniową aż do przerwania działania programu.
 * * @return Kod błędu (EXIT_SUCCESS dla prawidłowego zakończenia lub EXIT_FAILURE przy napotkaniu błędów).
 */
int main(void) {
    GLFWwindow* window;
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "Nie można zainicjować GLFW.\n");
        exit(EXIT_FAILURE);
    }

    window = glfwCreateWindow(800, 600, "Muzeum Maszyn Cyfrowych", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Nie można utworzyć okna.\n");
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    // Podpięcie callbacków obsługujących zdarzenia zewnętrzne
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // Zablokowanie i ukrycie myszki w oknie gry

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Uruchomienie v-sync

    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Nie można zainicjować GLEW.\n");
        exit(EXIT_FAILURE);
    }
    initOpenGLProgram(window);

    // --- INICJALIZACJA IMGUI ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- ŁADOWANIE CZCIONKI Z POLSKIMI ZNAKAMI (ImGui) ---
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Kodowanie ustandaryzowane Basic Latin
        0x0100, 0x017F, // Kodowanie Latin Extended-A (implementujące polskie znaki diakrytyczne)
        0,
    };

    // System wczyta standardową czcionkę Arial zapewniając wysoką jakość rysowanego interfejsu opisowego
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, NULL, ranges);

    // --- INICJALIZACJA VOSK I MINIAUDIO (Komendy Głosowe) ---
    g_model = vosk_model_new("model");
    g_recognizer = vosk_recognizer_new(g_model, 16000.0);

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_s16;
    deviceConfig.capture.channels = 1;
    deviceConfig.sampleRate = 16000;
    deviceConfig.dataCallback = data_callback;

    ma_device device;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Nie znaleziono mikrofonu! Przetwarzanie glosu zablokowane.\n");
    }
    ma_device_start(&device);

    // --- GŁÓWNA PĘTLA RENDERUJĄCA PROGRAMU ---
    while (!glfwWindowShouldClose(window)) {

        // 1. Zbieranie wejścia z urządzeń sterujących
        processInput(window);

        // 2. Rozpoczęcie nowej przestrzeni konstrukcyjnej interfejsu (ImGui)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 3. Rysowanie środowiska 3D
        drawScene(window);

        // 4. Renderowanie warstwy UI
        if (activeExhibit >= 0) {
            // Parametry pozycjonujące ramkę w przestrzeni viewportu okna aplikacji
            ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_FirstUseEver);

            ImGui::Begin("Karta Eksponatu", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

            const Exhibit& ex = exhibits[activeExhibit];

            // Tytuł rysowany zmiennym kolorem w palecie RGBA
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", ex.name);
            ImGui::Text("Rok produkcji: %s", ex.year);
            ImGui::Separator();

            for (int i = 0; i < ex.specCount; i++) {
                ImGui::BulletText("%s", ex.specs[i]);
            }

            ImGui::End();
        }

        // 5. Transfer danych graficznych
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // --- CZYSZCZENIE PAMIĘCI I BEZPIECZNE WYŁĄCZENIE SYSTEMU ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    freeOpenGLProgram(window);
    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}