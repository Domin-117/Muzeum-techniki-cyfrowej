#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

struct Cylinder {
    GLuint VAO, VBO;
    int vertexCount;

    void init(int segments = 32) {
        std::vector<float> verts;

        float angleStep = 2.0f * M_PI / segments;

        // Górna pokrywa (y = +0.5)
        for (int i = 0; i < segments; i++) {
            float a1 = i * angleStep;
            float a2 = (i + 1) * angleStep;

            // Środek
            verts.insert(verts.end(), {
                0.0f, 0.5f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.5f, 0.5f
            });
            // Punkt 1
            verts.insert(verts.end(), {
                cos(a1) * 0.5f, 0.5f, sin(a1) * 0.5f,
                0.0f, 1.0f, 0.0f,
                cos(a1) * 0.5f + 0.5f, sin(a1) * 0.5f + 0.5f
            });
            // Punkt 2
            verts.insert(verts.end(), {
                cos(a2) * 0.5f, 0.5f, sin(a2) * 0.5f,
                0.0f, 1.0f, 0.0f,
                cos(a2) * 0.5f + 0.5f, sin(a2) * 0.5f + 0.5f
            });
        }

        // Dolna pokrywa (y = -0.5)
        for (int i = 0; i < segments; i++) {
            float a1 = i * angleStep;
            float a2 = (i + 1) * angleStep;

            verts.insert(verts.end(), {
                0.0f, -0.5f, 0.0f,
                0.0f, -1.0f, 0.0f,
                0.5f, 0.5f
            });
            verts.insert(verts.end(), {
                cos(a2) * 0.5f, -0.5f, sin(a2) * 0.5f,
                0.0f, -1.0f, 0.0f,
                cos(a2) * 0.5f + 0.5f, sin(a2) * 0.5f + 0.5f
            });
            verts.insert(verts.end(), {
                cos(a1) * 0.5f, -0.5f, sin(a1) * 0.5f,
                0.0f, -1.0f, 0.0f,
                cos(a1) * 0.5f + 0.5f, sin(a1) * 0.5f + 0.5f
            });
        }

        // Bok (płaszcz)
        for (int i = 0; i < segments; i++) {
            float a1 = i * angleStep;
            float a2 = (i + 1) * angleStep;
            float u1 = (float)i / segments;
            float u2 = (float)(i + 1) / segments;

            // Trójkąt 1
            verts.insert(verts.end(), {
                cos(a1) * 0.5f, -0.5f, sin(a1) * 0.5f,
                cos(a1), 0.0f, sin(a1),
                u1, 0.0f
            });
            verts.insert(verts.end(), {
                cos(a2) * 0.5f, -0.5f, sin(a2) * 0.5f,
                cos(a2), 0.0f, sin(a2),
                u2, 0.0f
            });
            verts.insert(verts.end(), {
                cos(a1) * 0.5f,  0.5f, sin(a1) * 0.5f,
                cos(a1), 0.0f, sin(a1),
                u1, 1.0f
            });
            // Trójkąt 2
            verts.insert(verts.end(), {
                cos(a2) * 0.5f, -0.5f, sin(a2) * 0.5f,
                cos(a2), 0.0f, sin(a2),
                u2, 0.0f
            });
            verts.insert(verts.end(), {
                cos(a2) * 0.5f,  0.5f, sin(a2) * 0.5f,
                cos(a2), 0.0f, sin(a2),
                u2, 1.0f
            });
            verts.insert(verts.end(), {
                cos(a1) * 0.5f,  0.5f, sin(a1) * 0.5f,
                cos(a1), 0.0f, sin(a1),
                u1, 1.0f
            });
        }

        vertexCount = verts.size() / 8;

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER,
            verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

        // Pozycja
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        // Normalna
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
            8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        // UV
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
            8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
    }

    void draw() {
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }

    void free() {
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
    }
};