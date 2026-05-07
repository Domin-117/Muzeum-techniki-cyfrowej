#define GLM_FORCE_RADIANS
#define MINIAUDIO_IMPLEMENTATION
#define _USE_MATH_DEFINES

#include <cmath>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include "constants.h"
#include "lodepng.h"
#include "shaderprogram.h"
#include "miniaudio.h"
#include "vosk_api.h"
#pragma comment(lib, "winmm.lib")
#include <thread>
#include <atomic>
#include "cylinder.h"
Cylinder cylinderMesh;

std::atomic<float> targetBrightness(1.0f);
float currentBrightness = 1.0f;

VoskModel* g_model;
VoskRecognizer* g_recognizer;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    // 1. Sprawdzamy, czy Vosk przetworzył porcję dźwięku
    if (vosk_recognizer_accept_waveform(g_recognizer, (const char*)pInput, frameCount * 2)) {
        const char* result = vosk_recognizer_result(g_recognizer);

        if (strstr(result, "jasno")) targetBrightness = 1.8f;
        if (strstr(result, "ciemno")) targetBrightness = 0.2f;
        if (strstr(result, "normalnie")) targetBrightness = 1.0f;

        printf("Wynik końcowy: %s\n", result);
    }
    else {
        // 2. Wynik CZĘŚCIOWY
        const char* partial = vosk_recognizer_partial_result(g_recognizer);

        if (strstr(partial, "jasno")) targetBrightness = 1.8f;
        if (strstr(partial, "ciemno")) targetBrightness = 0.0f;
        if (strstr(partial, "normalnie")) targetBrightness = 1.0f;
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

glm::vec3 cameraPos = glm::vec3(-5.0f, 1.5f, 5.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float yaw = -90.0f, pitch = 0.0f;

// Dodałem texOdraFrame oraz texOdraPanel
GLuint texWall, texFloor, texCeiling, texLamp, texEniacBody, texBricks, texEniac, texRed, texBlack, texCard, texDesk, texGreen, texGauge, texBlue, texYellow, texOdraFrame, texOdraPanel;
GLuint cubeVAO, cubeVBO;

struct AABB {
    float minX, maxX, minZ, maxZ;
};

AABB createBox(float cx, float cz, float width, float depth) {
    return { cx - width / 2.0f, cx + width / 2.0f, cz - depth / 2.0f, cz + depth / 2.0f };
}

std::vector<AABB> walls = {
    createBox(0.0f, -10.0f, 20.0f, 0.5f), // Północ
    createBox(0.0f,  10.0f, 20.0f, 0.5f), // Południe
    createBox(-10.0f,  0.0f,  0.5f, 20.0f), // Zachód
    createBox(10.0f,  0.0f,  0.5f, 20.0f), // Wschód

    createBox(0.0f, 0.0f, 8.0f, 0.5f),
    createBox(0.0f, 0.0f, 0.5f, 8.0f),

    createBox(-8.0f,  0.0f, 4.0f, 0.5f),
    createBox(8.0f,  0.0f, 4.0f, 0.5f),
    createBox(0.0f, -8.0f, 0.5f, 4.0f),
    createBox(0.0f,  8.0f, 0.5f, 4.0f),

    // KOLIZJE DLA ENIACA Pokój 1
    // Północny rząd maszyn + Narożnik
    createBox(-5.8f, -8.8f, 7.2f, 1.3f),

    // Zachodni rząd maszyn
    createBox(-8.8f, -5.8f, 1.3f, 4.8f),

    // --- KOLIZJE DLA ODRY 1305 (Pokój 2) ---
    createBox(5.0f, -8.0f, 2.8f, 1.8f),
    createBox(8.5f, -5.3f, 1.2f, 4.0f),
    createBox(5.0f, -5.0f, 1.8f, 1.2f)
};

bool checkCollision(glm::vec3 pos) {
    float playerRadius = 0.2f;

    for (const auto& wall : walls) {
        bool collisionX = pos.x + playerRadius > wall.minX && pos.x - playerRadius < wall.maxX;
        bool collisionZ = pos.z + playerRadius > wall.minZ && pos.z - playerRadius < wall.maxZ;

        if (collisionX && collisionZ) {
            return true;
        }
    }
    return false;
}

float cubeVertices[] = {
    // Tył
    -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f,
    // Przód
    -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f,
    // Lewo
    -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
    // Prawo
     0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f,
     // Dół
     -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,
      0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 1.0f,
      0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,
      0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f,
     -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 0.0f,
     -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f,
     // Góra
     -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f,
      0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 1.0f,
      0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,
      0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f,
     -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 0.0f,
     -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f
};

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

void error_callback(int error, const char* description) {
    fputs(description, stderr);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mod) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}

void processInput(GLFWwindow* window) {
    float speed = 0.05f;
    float rotSpeed = 1.5f;

    glm::vec3 targetDirection = glm::vec3(0.0f);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) targetDirection += cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) targetDirection -= cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) targetDirection -= glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) targetDirection += glm::normalize(glm::cross(cameraFront, cameraUp));

    glm::vec3 movement = targetDirection * speed;

    // KOLIZJA
    // 1. Sprawdzamy ruch na osi X
    glm::vec3 testPosX = glm::vec3(cameraPos.x + movement.x, cameraPos.y, cameraPos.z);
    if (!checkCollision(testPosX)) {
        cameraPos.x += movement.x;
    }

    // 2. Sprawdzamy ruch na osi Z
    glm::vec3 testPosZ = glm::vec3(cameraPos.x, cameraPos.y, cameraPos.z + movement.z);
    if (!checkCollision(testPosZ)) {
        cameraPos.z += movement.z;
    }

    // Zablokowanie wysokości
    cameraPos.y = 1.5f;

    // Obracanie kamery nie podlega kolizjom
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)  yaw -= rotSpeed;
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) yaw += rotSpeed;
    // Spoglądanie w górę i w dół
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)    pitch += rotSpeed;
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)  pitch -= rotSpeed;

    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

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

    // Pozycja
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normalna
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Tekstura UV
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    texWall = createSolidColorTexture(75, 95, 120);
    texFloor = createSolidColorTexture(80, 80, 85);
    texEniacBody = createSolidColorTexture(35, 38, 35);
    texBricks = readTexture("bricks.png");
    texEniac = readTexture("eniac_panel.png");
    texCeiling = createSolidColorTexture(255, 255, 255);
    texLamp = createSolidColorTexture(255, 255, 220);

    texRed = createSolidColorTexture(255, 50, 50); // Czerwone lampki
    texBlack = createSolidColorTexture(30, 30, 30); // Czarne kable

    texCard = createSolidColorTexture(220, 200, 160); // Vintage beżowy 
    texDesk = createSolidColorTexture(70, 75, 80); // Ciemnoszary, matowy metal

    texGreen = createSolidColorTexture(50, 255, 50); // zielony
    texGauge = createSolidColorTexture(200, 190, 170); // Wyblakły żółtawy

    // NOWE KOLORY
    texBlue = createSolidColorTexture(50, 50, 255);
    texYellow = createSolidColorTexture(255, 255, 50);

    // KULTOWE KOLORY ODRY 1305 (Elwro)
    texOdraFrame = createSolidColorTexture(230, 225, 210); // Jasny, kremowy beż
    texOdraPanel = createSolidColorTexture(210, 70, 20);   // Ikoniczny pomarańczowy panel
    cylinderMesh.init(32);
}

void freeOpenGLProgram(GLFWwindow* window) {
    freeShaders();
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    cylinderMesh.free();
}

void drawCylinder(glm::mat4 M, GLuint tex, ShaderProgram* sp, int isLamp = 0) {
    glUniformMatrix4fv(sp->u("M"), 1, false, glm::value_ptr(M));
    glUniform1i(sp->u("isLamp"), isLamp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(sp->u("tex"), 0);
    cylinderMesh.draw();
}

void drawObject(glm::mat4 M, GLuint tex, ShaderProgram* sp, int isLamp = 0) {
    glUniformMatrix4fv(sp->u("M"), 1, false, glm::value_ptr(M));
    glUniform1i(sp->u("isLamp"), isLamp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(sp->u("tex"), 0);

    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void drawEniacCabinet(glm::vec3 pos, float rotY, ShaderProgram* sp, bool isLast = false) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    // Unikalny losowy numer 
    int seed = (int)(abs(pos.x * 111.0f + pos.z * 43.0f));

    // 1. GŁÓWNA SZAFA
    glm::mat4 mCab = glm::translate(mBase, glm::vec3(0.0f, 1.75f, 0.0f));
    mCab = glm::scale(mCab, glm::vec3(1.1f, 3.5f, 0.8f));
    drawObject(mCab, texEniacBody, sp, 0);

    // 2. DOLNA KRATKA WENTYLACYJNA
    glm::mat4 mVent = glm::translate(mBase, glm::vec3(0.0f, 0.3f, 0.41f));
    mVent = glm::scale(mVent, glm::vec3(0.9f, 0.4f, 0.05f));
    drawObject(mVent, texBlack, sp, 0);

    // 3. STÓŁ / PULPIT 
    glm::mat4 mDesk = glm::translate(mBase, glm::vec3(0.0f, 0.8f, 0.6f));
    mDesk = glm::scale(mDesk, glm::vec3(1.1f, 0.1f, 0.5f));
    drawObject(mDesk, texDesk, sp, 0);

    // Czarna listwa z gniazdami
    glm::mat4 mSockets = glm::translate(mBase, glm::vec3(0.0f, 0.86f, 0.65f));
    mSockets = glm::scale(mSockets, glm::vec3(0.9f, 0.02f, 0.2f));
    drawObject(mSockets, texBlack, sp, 0);

    // Nóżki podtrzymujące pulpit
    glm::mat4 mLegL = glm::translate(mBase, glm::vec3(-0.45f, 0.4f, 0.75f));
    mLegL = glm::scale(mLegL, glm::vec3(0.05f, 0.8f, 0.05f));
    drawObject(mLegL, texDesk, sp, 0);
    glm::mat4 mLegR = glm::translate(mBase, glm::vec3(0.45f, 0.4f, 0.75f));
    mLegR = glm::scale(mLegR, glm::vec3(0.05f, 0.8f, 0.05f));
    drawObject(mLegR, texDesk, sp, 0);

    // 4. ŚRODKOWY PANEL (Wajchy)
    glm::mat4 mPanelLow = glm::translate(mBase, glm::vec3(0.0f, 1.3f, 0.41f));
    mPanelLow = glm::scale(mPanelLow, glm::vec3(0.9f, 0.6f, 0.05f));
    drawObject(mPanelLow, texBlack, sp, 0);

    for (int i = 0; i < 5; i++) {
        float xOffset = -0.35f + (i * 0.17f);
        glm::mat4 mSwitch = glm::translate(mBase, glm::vec3(xOffset, 1.3f, 0.44f));
        mSwitch = glm::scale(mSwitch, glm::vec3(0.03f, 0.15f, 0.06f));
        drawObject(mSwitch, texCeiling, sp, 0);
    }

    // 5. GÓRNY PANEL (Macierz lampek)
    glm::mat4 mPanelUp = glm::translate(mBase, glm::vec3(0.0f, 2.3f, 0.41f));
    mPanelUp = glm::scale(mPanelUp, glm::vec3(0.9f, 1.0f, 0.05f));
    drawObject(mPanelUp, texBlack, sp, 0);

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 6; col++) {
            int randVal = (seed + row * 7 + col * 3) % 10;
            int isLit = (randVal > 3) ? 1 : 0;

            // 20% szans, że lampka będzie ZIELONA, w przeciwnym razie CZERWONA
            GLuint currentTex = (randVal > 7) ? texGreen : texRed;

            glm::mat4 mLight = glm::translate(mBase, glm::vec3(-0.35f + (col * 0.14f), 1.95f + (row * 0.18f), 0.44f));
            mLight = glm::scale(mLight, glm::vec3(0.04f, 0.04f, 0.02f));
            drawObject(mLight, currentTex, sp, isLit);
        }
    }

    // 6. SPLĄTANE KABLE KROSOWE (Do listwy)
    for (int k = 0; k < 8; k++) {
        float startX = -0.35f + ((seed + k) % 8) * 0.1f;
        float endX = -0.35f + ((seed + k * 2) % 8) * 0.1f;
        float tangleAngle = (startX - endX) * 12.0f;

        glm::mat4 mCable = glm::translate(mBase, glm::vec3((startX + endX) / 2.0f, 1.335f, 0.545f));
        mCable = glm::rotate(mCable, glm::radians(-13.0f), glm::vec3(1, 0, 0));
        mCable = glm::rotate(mCable, glm::radians(tangleAngle), glm::vec3(0, 0, 1));
        mCable = glm::scale(mCable, glm::vec3(0.006f, 0.95f, 0.006f));
        drawObject(mCable, texBlack, sp, 0);
    }

    // 7. CZYTNIK KART DZIURKOWANYCH
    if (seed % 3 == 0) {
        glm::mat4 mReader = glm::translate(mBase, glm::vec3(0.2f, 0.95f, 0.65f));
        mReader = glm::scale(mReader, glm::vec3(0.35f, 0.2f, 0.3f));
        drawObject(mReader, texDesk, sp, 0);

        glm::mat4 mPaper = glm::translate(mBase, glm::vec3(0.2f, 0.98f, 0.85f));
        mPaper = glm::rotate(mPaper, glm::radians(25.0f), glm::vec3(1, 0, 0));
        mPaper = glm::scale(mPaper, glm::vec3(0.25f, 0.01f, 0.4f));
        drawObject(mPaper, texCard, sp, 0);
    }

    glm::mat4 mTopPanel = glm::translate(mBase, glm::vec3(0.0f, 3.1f, 0.41f));
    mTopPanel = glm::scale(mTopPanel, glm::vec3(0.85f, 0.4f, 0.05f));
    drawObject(mTopPanel, texDesk, sp, 0);

    // Zegary analogowe
    for (int i = 0; i < 2; i++) {
        glm::mat4 mGauge = glm::translate(mBase, glm::vec3(-0.2f + (i * 0.4f), 3.15f, 0.44f));
        mGauge = glm::scale(mGauge, glm::vec3(0.18f, 0.18f, 0.02f));
        drawObject(mGauge, texGauge, sp, 0);
    }

    // Zielone, podłużne lampki stanu pod zegarami
    for (int i = 0; i < 4; i++) {
        glm::mat4 mStatus = glm::translate(mBase, glm::vec3(-0.3f + (i * 0.2f), 2.95f, 0.44f));
        mStatus = glm::scale(mStatus, glm::vec3(0.12f, 0.04f, 0.02f));
        drawObject(mStatus, texGreen, sp, 1);
    }

    // 9. GRUBE KABLE ŁĄCZĄCE SZAFY ZE SOBĄ
    if (!isLast) {
        for (int c = 0; c < 3; c++) {
            glm::mat4 mLink = glm::translate(mBase, glm::vec3(0.6f, 0.5f + (c * 0.1f), 0.35f));
            mLink = glm::scale(mLink, glm::vec3(1.2f, 0.02f, 0.02f));
            drawObject(mLink, texBlack, sp, 0);
        }
    }
}

void drawTapeDrive(glm::vec3 pos, float rotY, ShaderProgram* sp, int index) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    // 1. Szafa z taśmami
    glm::mat4 mCab = glm::translate(mBase, glm::vec3(0.0f, 1.4f, 0.0f));
    mCab = glm::scale(mCab, glm::vec3(0.9f, 2.8f, 0.7f));
    drawObject(mCab, texOdraFrame, sp, 0);

    // 2. Dolne drzwiczki pod szpulami
    glm::mat4 mDoor = glm::translate(mBase, glm::vec3(0.0f, 0.65f, 0.355f));
    mDoor = glm::scale(mDoor, glm::vec3(0.85f, 1.2f, 0.05f));
    drawObject(mDoor, texOdraPanel, sp, 0);

    // 3. Ciemny panel z tyłu za szpulami
    glm::mat4 mPanel = glm::translate(mBase, glm::vec3(0.0f, 1.8f, 0.36f));
    mPanel = glm::scale(mPanel, glm::vec3(0.8f, 1.0f, 0.05f));
    drawObject(mPanel, texBlack, sp, 0);

    float time = (float)glfwGetTime();

    // 4. Lewa szpula
    glm::mat4 mReel1 = glm::translate(mBase, glm::vec3(-0.22f, 1.9f, 0.39f));
    mReel1 = glm::rotate(mReel1, time * 2.0f, glm::vec3(0, 0, 1));
    mReel1 = glm::rotate(mReel1, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mReel1 = glm::scale(mReel1, glm::vec3(0.28f, 0.04f, 0.28f));
    drawCylinder(mReel1, texCeiling, sp, 0);

    // Ciemny środek szpuli (otwór / piasta)
    glm::mat4 mHub1 = glm::translate(mBase, glm::vec3(-0.22f, 1.9f, 0.395f));
    mHub1 = glm::rotate(mHub1, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mHub1 = glm::scale(mHub1, glm::vec3(0.08f, 0.05f, 0.08f));
    drawCylinder(mHub1, texBlack, sp, 0);

    // 5. Prawa szpula
    glm::mat4 mReel2 = glm::translate(mBase, glm::vec3(0.22f, 1.9f, 0.39f));
    mReel2 = glm::rotate(mReel2, time * 2.0f, glm::vec3(0, 0, 1));
    mReel2 = glm::rotate(mReel2, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mReel2 = glm::scale(mReel2, glm::vec3(0.28f, 0.04f, 0.28f));
    drawCylinder(mReel2, texCeiling, sp, 0);

    // Ciemny środek szpuli
    glm::mat4 mHub2 = glm::translate(mBase, glm::vec3(0.22f, 1.9f, 0.395f));
    mHub2 = glm::rotate(mHub2, glm::radians(90.0f), glm::vec3(1, 0, 0));
    mHub2 = glm::scale(mHub2, glm::vec3(0.08f, 0.05f, 0.08f));
    drawCylinder(mHub2, texBlack, sp, 0);

    // 6. Pasek taśmy magnetycznej łączący szpule
    glm::mat4 mTape = glm::translate(mBase, glm::vec3(0.0f, 1.75f, 0.39f));
    mTape = glm::scale(mTape, glm::vec3(0.44f, 0.02f, 0.01f));
    drawObject(mTape, texBlack, sp, 0);

    if (index == 0) {
        // Mały czarny panel
        glm::mat4 mSubPanel = glm::translate(mBase, glm::vec3(0.0f, 0.9f, 0.36f));
        mSubPanel = glm::scale(mSubPanel, glm::vec3(0.5f, 0.15f, 0.05f));
        drawObject(mSubPanel, texBlack, sp, 0);

        // 5 małych czerwonych diod
        for (int j = 0; j < 5; j++) {
            glm::mat4 mLed = glm::translate(mBase, glm::vec3(-0.15f + (j * 0.075f), 0.9f, 0.39f));
            mLed = glm::scale(mLed, glm::vec3(0.04f, 0.04f, 0.02f));
            drawObject(mLed, texRed, sp, 1);
        }

        // Metalowa tabliczka
        glm::mat4 mPlate = glm::translate(mBase, glm::vec3(0.0f, 0.5f, 0.36f));
        mPlate = glm::scale(mPlate, glm::vec3(0.4f, 0.2f, 0.05f));
        drawObject(mPlate, texGauge, sp, 0);
    }
    else if (index == 1) {
        // 4 kolorowe przyciski + 3 wajchy
        GLuint btnColors[4] = { texRed, texBlue, texGreen, texYellow };
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 2; c++) {
                glm::mat4 mBtn = glm::translate(mBase, glm::vec3(-0.15f + (c * 0.08f), 0.95f - (r * 0.08f), 0.36f));
                mBtn = glm::scale(mBtn, glm::vec3(0.05f, 0.05f, 0.02f));
                drawObject(mBtn, btnColors[r * 2 + c], sp, 1);
            }
        }
        // Wajchy
        for (int sw = 0; sw < 3; sw++) {
            glm::mat4 mSw = glm::translate(mBase, glm::vec3(0.05f + (sw * 0.08f), 0.91f, 0.36f));
            mSw = glm::rotate(mSw, glm::radians(30.0f), glm::vec3(1, 0, 0));
            mSw = glm::scale(mSw, glm::vec3(0.02f, 0.08f, 0.02f));
            drawObject(mSw, texCeiling, sp, 0);
        }
    }
    else if (index == 2) {
        // Mały czarny panel
        glm::mat4 mSubPanel = glm::translate(mBase, glm::vec3(0.0f, 0.9f, 0.36f));
        mSubPanel = glm::scale(mSubPanel, glm::vec3(0.5f, 0.15f, 0.05f));
        drawObject(mSubPanel, texBlack, sp, 0);

        // Pasek postępu (3 zielone bloki)
        for (int j = 0; j < 3; j++) {
            glm::mat4 mBar = glm::translate(mBase, glm::vec3(-0.1f + (j * 0.1f), 0.9f, 0.39f));
            mBar = glm::scale(mBar, glm::vec3(0.08f, 0.08f, 0.02f));
            drawObject(mBar, texGreen, sp, 1);
        }

        // Metalowa tabliczka
        glm::mat4 mPlate = glm::translate(mBase, glm::vec3(0.0f, 0.5f, 0.36f));
        mPlate = glm::scale(mPlate, glm::vec3(0.4f, 0.2f, 0.05f));
        drawObject(mPlate, texGauge, sp, 0);
    }
}

void drawMainframeConsole(glm::vec3 pos, float rotY, ShaderProgram* sp) {
    glm::mat4 mBase = glm::translate(glm::mat4(1.0f), pos);
    mBase = glm::rotate(mBase, glm::radians(rotY), glm::vec3(0, 1, 0));

    // Biurko Operatora
    glm::mat4 mDesk = glm::translate(mBase, glm::vec3(0.0f, 0.7f, 0.0f));
    mDesk = glm::scale(mDesk, glm::vec3(1.4f, 0.05f, 0.8f));
    drawObject(mDesk, texDesk, sp, 0);

    glm::mat4 mLeg1 = glm::translate(mBase, glm::vec3(-0.65f, 0.35f, 0.0f));
    mLeg1 = glm::scale(mLeg1, glm::vec3(0.05f, 0.7f, 0.7f));
    drawObject(mLeg1, texBlack, sp, 0);

    glm::mat4 mLeg2 = glm::translate(mBase, glm::vec3(0.65f, 0.35f, 0.0f));
    mLeg2 = glm::scale(mLeg2, glm::vec3(0.05f, 0.7f, 0.7f));
    drawObject(mLeg2, texBlack, sp, 0);

    // RETRO TERMINAL
    glm::mat4 mMonBase = glm::translate(mBase, glm::vec3(0.0f, 0.75f, -0.1f));
    mMonBase = glm::scale(mMonBase, glm::vec3(0.25f, 0.1f, 0.25f));
    drawObject(mMonBase, texOdraFrame, sp, 0);

    glm::mat4 mMonitor = glm::translate(mBase, glm::vec3(0.0f, 0.98f, -0.05f));
    mMonitor = glm::rotate(mMonitor, glm::radians(5.0f), glm::vec3(1, 0, 0));
    mMonitor = glm::scale(mMonitor, glm::vec3(0.5f, 0.45f, 0.45f));
    drawObject(mMonitor, texOdraFrame, sp, 0);

    // RETRO TERMINAL: Zielony Ekran
    glm::mat4 mScreen = glm::translate(mBase, glm::vec3(0.0f, 0.98f, 0.18f));
    mScreen = glm::rotate(mScreen, glm::radians(5.0f), glm::vec3(1, 0, 0));
    mScreen = glm::scale(mScreen, glm::vec3(0.42f, 0.35f, 0.02f));
    drawObject(mScreen, texGreen, sp, 1);

    // Klawiatura
    glm::mat4 mKeyb = glm::translate(mBase, glm::vec3(0.0f, 0.74f, 0.28f));
    mKeyb = glm::rotate(mKeyb, glm::radians(10.0f), glm::vec3(1, 0, 0));
    mKeyb = glm::scale(mKeyb, glm::vec3(0.6f, 0.04f, 0.2f));
    drawObject(mKeyb, texBlack, sp, 0);

    // Gruby kabel klawiatury
    glm::mat4 mCable = glm::translate(mBase, glm::vec3(0.0f, 0.73f, 0.12f));
    mCable = glm::scale(mCable, glm::vec3(0.02f, 0.02f, 0.2f));
    drawObject(mCable, texBlack, sp, 0);
}

void drawOdra1305(glm::vec3 centerPos, ShaderProgram* sp) {
    // Wielka jednostka centralna
    glm::mat4 mCpuBase = glm::translate(glm::mat4(1.0f), centerPos + glm::vec3(0.0f, 1.0f, -3.0f));
    glm::mat4 mCpu = glm::scale(mCpuBase, glm::vec3(2.5f, 2.0f, 1.5f));
    drawObject(mCpu, texOdraFrame, sp, 0);

    // POMARAŃCZOWY FRONT
    glm::mat4 mCpuFront = glm::translate(mCpuBase, glm::vec3(0.0f, 0.1f, 0.755f));
    mCpuFront = glm::scale(mCpuFront, glm::vec3(2.4f, 1.6f, 0.02f));
    drawObject(mCpuFront, texOdraPanel, sp, 0);

    //DETALE NA CPU
    // Panel sterowania
    glm::mat4 mCpuPanel = glm::translate(mCpuBase, glm::vec3(-0.5f, 0.3f, 0.76f));
    mCpuPanel = glm::scale(mCpuPanel, glm::vec3(0.8f, 0.4f, 0.05f));
    drawObject(mCpuPanel, texBlack, sp, 0);

    // 4 czerwone przyciski (2x2) na panelu
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            glm::mat4 mBtn = glm::translate(mCpuBase, glm::vec3(-0.7f + (c * 0.12f), 0.36f - (r * 0.12f), 0.79f));
            mBtn = glm::scale(mBtn, glm::vec3(0.08f, 0.08f, 0.02f));
            drawObject(mBtn, texRed, sp, 1);
        }
    }

    // Czarne pokrętło obok przycisków
    glm::mat4 mDial = glm::translate(mCpuBase, glm::vec3(-0.35f, 0.3f, 0.79f));
    mDial = glm::scale(mDial, glm::vec3(0.12f, 0.12f, 0.03f));
    drawObject(mDial, texDesk, sp, 0);

    // Szczeliny wentylacyjne (prawa strona CPU)
    for (int v = 0; v < 5; v++) {
        glm::mat4 mVent = glm::translate(mCpuBase, glm::vec3(0.2f + (v * 0.15f), 0.3f, 0.76f));
        mVent = glm::scale(mVent, glm::vec3(0.05f, 0.4f, 0.02f));
        drawObject(mVent, texBlack, sp, 0);
    }

    // 3 Pamięci taśmowe pod wschodnią ścianą pokoju
    for (int i = 0; i < 3; i++) {
        drawTapeDrive(centerPos + glm::vec3(3.5f, 0.0f, -1.5f + (i * 1.2f)), -90.0f, sp, i);
    }

    // Konsola operatora na środku pokoju
    drawMainframeConsole(centerPos + glm::vec3(0.0f, 0.0f, 0.0f), 0.0f, sp);

    // Magistrala: Główny procesor (CPU)
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
        bool isLastInRow = (i == 3);
        drawEniacCabinet(pos, 90.0f, sp, isLastInRow);
    }
}

void drawScene(GLFWwindow* window) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::vec2 playerPos2D = glm::vec2(cameraPos.x, cameraPos.z);
    float distEniac = glm::distance(playerPos2D, glm::vec2(-6.0f, -6.0f));
    float distOdra = glm::distance(playerPos2D, glm::vec2(5.0f, -5.0f));

    if (distEniac < 4.0f) {
        glfwSetWindowTitle(window, "Eksponat: ENIAC (1945) | Waga: 27 ton | 18 000 lamp prozniowych");
    }
    else if (distOdra < 5.0f) {
        glfwSetWindowTitle(window, "Eksponat: ODRA 1305 (1973) | Elwro Wroclaw | RAM: max 256 KB | Legenda PRL");
    }
    else {
        glfwSetWindowTitle(window, "Muzeum Maszyn Cyfrowych");
    }

    // Najpierw obliczamy nową jasność
    currentBrightness += (targetBrightness - currentBrightness) * 0.02f;

    // Aktywujemy program (SHADER)
    spLambert->use();

    // DOPIERO TERAZ wysyłamy jasność do aktywnego programu
    glUniform1f(spLambert->u("soundVolume"), currentBrightness);

    // Aktualizacja rozglądania
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);

    glm::mat4 V = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    // Pobieramy aktualny rozmiar okna
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    // Zabezpieczenie przed dzieleniem przez zero
    if (height == 0) height = 1;
    float aspectRatio = (float)width / (float)height;

    // Używamy dynamicznego aspectRatio
    glm::mat4 P = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, 100.0f);

    spLambert->use();
    glUniformMatrix4fv(spLambert->u("P"), 1, false, glm::value_ptr(P));
    glUniformMatrix4fv(spLambert->u("V"), 1, false, glm::value_ptr(V));

    // Oświetlenie
    glm::vec4 lightPos[4] = {
    glm::vec4(-5.0f, 2.5f, -5.0f, 1.0f), // Pokój Północno-Zachodni
    glm::vec4(5.0f, 2.5f, -5.0f, 1.0f),  // Pokój Północno-Wschodni
    glm::vec4(-5.0f, 2.5f,  5.0f, 1.0f), // Pokój Południowo-Zachodni
    glm::vec4(5.0f, 2.5f,  5.0f, 1.0f)   // Pokój Południowo-Wschodni
    };

    // Wysyłamy całą tablicę 4 wektorów do shadera
    glUniform4fv(spLambert->u("lightPositions"), 4, glm::value_ptr(lightPos[0]));

    // BUDOWA POKOJU

    // 1. PODŁOGA
    glm::mat4 mFloor = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec3(20.0f, 0.1f, 20.0f));
    drawObject(mFloor, texFloor, spLambert);

    // SUFIT
    glm::mat4 mCeiling = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.05f, 0.0f)), glm::vec3(20.0f, 0.1f, 20.0f));
    drawObject(mCeiling, texCeiling, spLambert);

    // LAMPY (Fizyczne modele)
    for (int i = 0; i < 4; i++) {
        glm::mat4 mLamp = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(lightPos[i].x, 3.98f, lightPos[i].z)), glm::vec3(1.2f, 0.05f, 1.2f));

        drawObject(mLamp, texLamp, spLambert, 1);
    }

    // 2. ŚCIANY ZEWNĘTRZNE
    glm::mat4 mOuterN = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, -10.0f)), glm::vec3(20.0f, 4.0f, 0.5f));
    drawObject(mOuterN, texWall, spLambert);
    glm::mat4 mOuterS = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 10.0f)), glm::vec3(20.0f, 4.0f, 0.5f));
    drawObject(mOuterS, texWall, spLambert);
    glm::mat4 mOuterW = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-10.0f, 2.0f, 0.0f)), glm::vec3(0.5f, 4.0f, 20.0f));
    drawObject(mOuterW, texWall, spLambert);
    glm::mat4 mOuterE = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 2.0f, 0.0f)), glm::vec3(0.5f, 4.0f, 20.0f));
    drawObject(mOuterE, texWall, spLambert);

    // 3. ŚCIANY WEWNĘTRZNE
    // A) Centralny filar/krzyż
    glm::mat4 mCrossX = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(8.0f, 4.0f, 0.5f));
    drawObject(mCrossX, texBricks, spLambert);

    glm::mat4 mCrossZ = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(0.5f, 4.0f, 8.0f));
    drawObject(mCrossZ, texBricks, spLambert);

    // B) Kawałki ścian przy drzwiach
    glm::mat4 mDoorX1 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-8.0f, 2.0f, 0.0f)), glm::vec3(4.0f, 4.0f, 0.5f));
    drawObject(mDoorX1, texBricks, spLambert);

    glm::mat4 mDoorX2 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(8.0f, 2.0f, 0.0f)), glm::vec3(4.0f, 4.0f, 0.5f));
    drawObject(mDoorX2, texBricks, spLambert);

    glm::mat4 mDoorZ1 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, -8.0f)), glm::vec3(0.5f, 4.0f, 4.0f));
    drawObject(mDoorZ1, texBricks, spLambert);

    glm::mat4 mDoorZ2 = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 8.0f)), glm::vec3(0.5f, 4.0f, 4.0f));
    drawObject(mDoorZ2, texBricks, spLambert);

    // 4. EKSPONATY
    drawUltimateEniac(glm::vec3(-5.0f, 0.0f, -6.0f), spLambert);
    drawOdra1305(glm::vec3(5.0f, 0.0f, -5.0f), spLambert);

    glfwSwapBuffers(window);
}

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
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Nie można zainicjować GLEW.\n");
        exit(EXIT_FAILURE);
    }
    initOpenGLProgram(window);

    // 1. Ładowanie modelu
    g_model = vosk_model_new("model");
    g_recognizer = vosk_recognizer_new(g_model, 16000.0);

    // 2. Konfiguracja mikrofonu (Miniaudio)
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_s16; // Vosk lubi 16-bit PCM
    deviceConfig.capture.channels = 1;           // Mono
    deviceConfig.sampleRate = 16000;             // 16kHz
    deviceConfig.dataCallback = data_callback;

    ma_device device;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        printf("Nie znaleziono mikrofonu!\n");
    }
    ma_device_start(&device); // Start nasłuchiwania

    while (!glfwWindowShouldClose(window)) {
        processInput(window);
        drawScene(window);
        glfwPollEvents();
    }
    freeOpenGLProgram(window);
    glfwDestroyWindow(window);
    glfwTerminate();
    exit(EXIT_SUCCESS);
}