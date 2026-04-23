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

GLuint texWall, texFloor, texCeiling, texLamp, texBricks;
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
    createBox(0.0f,  8.0f, 0.5f, 4.0f)
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

    texWall = readTexture("wall.png");
    texFloor = readTexture("floor.png");
    texBricks = readTexture("bricks.png");
    texCeiling = createSolidColorTexture(255, 255, 255);
    texLamp = createSolidColorTexture(255, 255, 220);
}

void freeOpenGLProgram(GLFWwindow* window) {
    freeShaders();
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
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

void drawScene(GLFWwindow* window) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 1. Najpierw obliczamy nową jasność
    currentBrightness += (targetBrightness - currentBrightness) * 0.02f;

    // 2. Aktywujemy program (SHADER)
    spLambert->use();

    // 3. DOPIERO TERAZ wysyłamy jasność do aktywnego programu
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

    // Zabezpieczenie przed dzieleniem przez zero (gdy okno jest zminimalizowane)
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