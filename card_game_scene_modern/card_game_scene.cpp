// ============================================================================
// مشروع لعبة البطاقات - المشهد الكامل (Modern OpenGL 3.3 Core / GLFW / GLEW / GLM)
// الغرفة (30x15x30) + الطاولة المقسمة + الكاميرا بحالتين + 5 بطاقات بالأنيميشن
// + الملصق الممزق (6 رؤوس) + 3 قطع ديكور (جيروسكوب متحرك، تلسكوب، وموديل OBJ)
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #pragma comment(lib, "opengl32.lib")
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// مكتبة stb_image.h لتحميل صور JPG/PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ============================================================================
// نصوص المظللات (Vertex Shader & Fragment Shader) - Modern OpenGL 3.3 Core
// ============================================================================
const char* VERTEX_SHADER_SRC = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * worldPos;
}
)";

const char* FRAGMENT_SHADER_SRC = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uColor;
uniform bool uUseTexture;
uniform bool uUseLighting;
uniform sampler2D uTexture;

uniform vec3 uLightPos;
uniform vec3 uLightAmbient;
uniform vec3 uLightDiffuse;

void main() {
    vec4 baseColor = vec4(uColor, 1.0);
    if (uUseTexture) {
        vec4 texColor = texture(uTexture, vTexCoord);
        baseColor = texColor * vec4(uColor, 1.0);
    }

    if (!uUseLighting) {
        FragColor = baseColor;
        return;
    }

    vec3 norm = normalize(vNormal);
    if (!gl_FrontFacing) {
        norm = -norm;
    }

    vec3 lightDir = normalize(uLightPos - vFragPos);
    float diff = max(dot(norm, lightDir), 0.0);

    vec3 lighting = uLightAmbient + diff * uLightDiffuse;
    FragColor = vec4(baseColor.rgb * lighting, baseColor.a);
}
)";

// ============================================================================
// الثوابت الهندسية للمشهد والبطاقات والملصق
// ============================================================================
const float ROOM_WIDTH  = 30.0f;
const float ROOM_HEIGHT = 15.0f;
const float ROOM_DEPTH  = 30.0f;

const float TABLE_WIDTH     = 10.0f;
const float TABLE_DEPTH     = 7.5f;
const float TABLE_HEIGHT    = 3.0f;
const float TABLE_THICKNESS = 0.35f;
const float LEG_THICKNESS   = 0.45f;

const float POSTER_WIDTH    = 4.2f;
const float POSTER_HEIGHT   = 5.4f;
const float POSTER_CENTER_X = 0.0f;
const float POSTER_BOTTOM_Y = 2.6f;
const float POSTER_Z        = -(ROOM_DEPTH / 2.0f) + 0.05f;

const int   NUM_CARDS         = 5;
const float CARD_WIDTH        = 1.25f;
const float CARD_HEIGHT       = 1.80f;
const float CARD_SPACING      = 1.65f;
const float CARD_SURFACE_Y    = TABLE_HEIGHT + 0.03f;
const float DECK_PIVOT_Z      = 3.15f;
const float PLAYED_PIVOT_Z    = 0.90f;
const float CARD_STAND_ANGLE  = 75.0f;
const float TILT_SPEED        = 7.5f;
const float MOVE_DURATION     = 0.65f;
const float MOVE_ARC_HEIGHT   = 1.10f;
const float PI_F              = 3.14159265f;

// ============================================================================
// بنيات البيانات: الرؤوس والمجسمات (VAO/VBO) والبطاقات وملفات OBJ
// ============================================================================
struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

struct Mesh {
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLsizei vertexCount = 0;
    GLenum drawMode = GL_TRIANGLES;
};

enum CardState {
    CARD_IN_DECK,
    CARD_MOVING,
    CARD_PLAYED
};

struct Card {
    GLuint textureID = 0;
    CardState state = CARD_IN_DECK;

    float x = 0.0f, y = 0.0f, z = 0.0f;
    float angleX = 0.0f;

    float startX = 0.0f, startY = 0.0f, startZ = 0.0f;
    float startAngleX = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
    float moveProgress = 0.0f;
};

struct Vec3 {
    float x, y, z;
};

struct ObjFace {
    int vIdx[3];
    int vnIdx[3];
};

// ============================================================================
// المتغيرات العامة
// ============================================================================
int gWindowWidth  = 1024;
int gWindowHeight = 720;

GLuint gShaderProgram = 0;
GLint uLocModel       = -1;
GLint uLocView        = -1;
GLint uLocProjection  = -1;
GLint uLocColor       = -1;
GLint uLocUseTexture  = -1;
GLint uLocUseLighting = -1;
GLint uLocTexture     = -1;
GLint uLocLightPos    = -1;
GLint uLocLightAmb    = -1;
GLint uLocLightDiff   = -1;

// الخامات (Textures)
GLuint texFloorWood = 0;
GLuint texWallpaper = 0;
GLuint texPoster    = 0;

// المجسمات المخزنة في VAO/VBO
Mesh meshFloor;
Mesh meshWalls;
Mesh meshCeiling;
Mesh meshPoster;
Mesh meshPosterTearEdge;
Mesh meshCube;
Mesh meshUnitQuadXZ;
Mesh meshUnitLoopXZ;
Mesh meshTableDividers;
Mesh meshCardFront;
Mesh meshCardBack;
Mesh meshCardBorder;

// مجسمات قطع الديكور (بديل GLU/GLUT)
Mesh meshSphere;
Mesh meshDisk;
Mesh meshGyroBase;
Mesh meshGyroRod;
Mesh meshTorusOuter;
Mesh meshTorusMiddle;
Mesh meshTorusInner;
Mesh meshTripodLeg;
Mesh meshTelescopeTube;
Mesh meshTelescopeRing;
Mesh meshTelescopeCollar;
Mesh meshTelescopeEyepiece;
Mesh meshPedestal;
Mesh meshFallbackCone;
Mesh meshSpaceship;
bool spaceshipLoaded = false;

// حالة الكاميرا (1 = عمودية فوق الطاولة | 2 = منظور اللاعب الجالس)
int cameraMode = 2;

// البطاقات الخمس وعداد الملعوبة
Card cards[NUM_CARDS];
int playedCardsCount = 0;

// زوايا الأنيميشن المستمر
float gyroAngleOuter  = 0.0f;
float gyroAngleMiddle = 0.0f;
float gyroAngleInner  = 0.0f;
float spaceshipSpin   = 0.0f;

// ============================================================================
// دوال بناء وتحرير VAO/VBO ورسم المجسمات
// ============================================================================
Mesh createMesh(const std::vector<Vertex>& vertices, GLenum drawMode = GL_TRIANGLES) {
    Mesh m;
    m.vertexCount = static_cast<GLsizei>(vertices.size());
    m.drawMode = drawMode;

    if (vertices.empty()) return m;

    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);

    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    // location 0: الموقع (x, y, z)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));

    // location 1: الناظم (nx, ny, nz)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nx));

    // location 2: إحداثيات الخامة (u, v)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));

    glBindVertexArray(0);
    return m;
}

void destroyMesh(Mesh& m) {
    if (m.VBO != 0) {
        glDeleteBuffers(1, &m.VBO);
        m.VBO = 0;
    }
    if (m.VAO != 0) {
        glDeleteVertexArrays(1, &m.VAO);
        m.VAO = 0;
    }
    m.vertexCount = 0;
}

void drawMesh(const Mesh& mesh, const glm::mat4& model,
              const glm::vec3& color, GLuint textureID = 0, bool useLighting = true) {
    if (mesh.VAO == 0 || mesh.vertexCount == 0) return;

    glUniformMatrix4fv(uLocModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(uLocColor, 1, glm::value_ptr(color));
    glUniform1i(uLocUseLighting, useLighting ? 1 : 0);

    if (textureID != 0) {
        glUniform1i(uLocUseTexture, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glUniform1i(uLocTexture, 0);
    } else {
        glUniform1i(uLocUseTexture, 0);
    }

    glBindVertexArray(mesh.VAO);
    glDrawArrays(mesh.drawMode, 0, mesh.vertexCount);
    glBindVertexArray(0);
}

// ============================================================================
// دوال ترجمة وربط الـ Shaders
// ============================================================================
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "[Shader Compile Error] " << infoLog << std::endl;
    }
    return shader;
}

GLuint createShaderProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER_SRC);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint success = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(prog, 512, NULL, infoLog);
        std::cerr << "[Shader Link Error] " << infoLog << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ============================================================================
// دالة مساعدة لإضافة رباعي (Quad) كمثلثين (CCW)
// ============================================================================
void pushQuad(std::vector<Vertex>& out,
              const Vertex& v0, const Vertex& v1,
              const Vertex& v2, const Vertex& v3) {
    out.push_back(v0); out.push_back(v1); out.push_back(v2);
    out.push_back(v0); out.push_back(v2); out.push_back(v3);
}

// ============================================================================
// مولدات المجسمات الهندسية (بديل glutSolidCube, gluCylinder, gluSphere, gluDisk, glutSolidTorus)
// ============================================================================
Mesh buildCubeMesh() {
    std::vector<Vertex> v;
    const float h = 0.5f;

    // الأمامي (+Z)
    pushQuad(v,
        {-h, -h,  h,  0,  0,  1, 0, 0},
        { h, -h,  h,  0,  0,  1, 1, 0},
        { h,  h,  h,  0,  0,  1, 1, 1},
        {-h,  h,  h,  0,  0,  1, 0, 1});
    // الخلفي (-Z)
    pushQuad(v,
        { h, -h, -h,  0,  0, -1, 0, 0},
        {-h, -h, -h,  0,  0, -1, 1, 0},
        {-h,  h, -h,  0,  0, -1, 1, 1},
        { h,  h, -h,  0,  0, -1, 0, 1});
    // الأيسر (-X)
    pushQuad(v,
        {-h, -h, -h, -1,  0,  0, 0, 0},
        {-h, -h,  h, -1,  0,  0, 1, 0},
        {-h,  h,  h, -1,  0,  0, 1, 1},
        {-h,  h, -h, -1,  0,  0, 0, 1});
    // الأيمن (+X)
    pushQuad(v,
        { h, -h,  h,  1,  0,  0, 0, 0},
        { h, -h, -h,  1,  0,  0, 1, 0},
        { h,  h, -h,  1,  0,  0, 1, 1},
        { h,  h,  h,  1,  0,  0, 0, 1});
    // العلوي (+Y)
    pushQuad(v,
        {-h,  h,  h,  0,  1,  0, 0, 0},
        { h,  h,  h,  0,  1,  0, 1, 0},
        { h,  h, -h,  0,  1,  0, 1, 1},
        {-h,  h, -h,  0,  1,  0, 0, 1});
    // السفلي (-Y)
    pushQuad(v,
        {-h, -h, -h,  0, -1,  0, 0, 0},
        { h, -h, -h,  0, -1,  0, 1, 0},
        { h, -h,  h,  0, -1,  0, 1, 1},
        {-h, -h,  h,  0, -1,  0, 0, 1});

    return createMesh(v, GL_TRIANGLES);
}

// بديل gluCylinder / glutSolidCone: يمتد على +Z من z=0 إلى z=height
Mesh buildCylinderMesh(float baseRadius, float topRadius, float height, int slices, int stacks) {
    std::vector<Vertex> v;
    float dr = baseRadius - topRadius;
    float slopeLen = std::sqrt(height * height + dr * dr);
    float nzSlope = (slopeLen > 0.0001f) ? (dr / slopeLen) : 0.0f;
    float nrSlope = (slopeLen > 0.0001f) ? (height / slopeLen) : 1.0f;

    for (int st = 0; st < stacks; ++st) {
        float t0 = static_cast<float>(st) / stacks;
        float t1 = static_cast<float>(st + 1) / stacks;
        float z0 = t0 * height;
        float z1 = t1 * height;
        float r0 = baseRadius + (topRadius - baseRadius) * t0;
        float r1 = baseRadius + (topRadius - baseRadius) * t1;

        for (int sl = 0; sl < slices; ++sl) {
            float a0 = (2.0f * PI_F * sl) / slices;
            float a1 = (2.0f * PI_F * (sl + 1)) / slices;
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);

            Vertex p00 = { r0 * c0, r0 * s0, z0, nrSlope * c0, nrSlope * s0, nzSlope, static_cast<float>(sl) / slices, t0 };
            Vertex p10 = { r0 * c1, r0 * s1, z0, nrSlope * c1, nrSlope * s1, nzSlope, static_cast<float>(sl + 1) / slices, t0 };
            Vertex p11 = { r1 * c1, r1 * s1, z1, nrSlope * c1, nrSlope * s1, nzSlope, static_cast<float>(sl + 1) / slices, t1 };
            Vertex p01 = { r1 * c0, r1 * s0, z1, nrSlope * c0, nrSlope * s0, nzSlope, static_cast<float>(sl) / slices, t1 };

            pushQuad(v, p00, p10, p11, p01);
        }
    }
    return createMesh(v, GL_TRIANGLES);
}

// بديل gluDisk: يقع في المستوي XY عند z=0 وناظمه +Z
Mesh buildDiskMesh(float radius, int slices) {
    std::vector<Vertex> v;
    Vertex center = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f };

    for (int i = 0; i < slices; ++i) {
        float a0 = (2.0f * PI_F * i) / slices;
        float a1 = (2.0f * PI_F * (i + 1)) / slices;

        Vertex p0 = { radius * std::cos(a0), radius * std::sin(a0), 0.0f, 0.0f, 0.0f, 1.0f,
                      0.5f + 0.5f * std::cos(a0), 0.5f + 0.5f * std::sin(a0) };
        Vertex p1 = { radius * std::cos(a1), radius * std::sin(a1), 0.0f, 0.0f, 0.0f, 1.0f,
                      0.5f + 0.5f * std::cos(a1), 0.5f + 0.5f * std::sin(a1) };

        v.push_back(center);
        v.push_back(p0);
        v.push_back(p1);
    }
    return createMesh(v, GL_TRIANGLES);
}

// بديل gluSphere
Mesh buildSphereMesh(float radius, int slices, int stacks) {
    std::vector<Vertex> v;
    for (int st = 0; st < stacks; ++st) {
        float phi0 = PI_F * static_cast<float>(st) / stacks;
        float phi1 = PI_F * static_cast<float>(st + 1) / stacks;

        for (int sl = 0; sl < slices; ++sl) {
            float th0 = 2.0f * PI_F * static_cast<float>(sl) / slices;
            float th1 = 2.0f * PI_F * static_cast<float>(sl + 1) / slices;

            auto makePt = [&](float phi, float th, float uCoord, float vCoord) -> Vertex {
                float nx = std::sin(phi) * std::cos(th);
                float ny = std::cos(phi);
                float nz = std::sin(phi) * std::sin(th);
                return { radius * nx, radius * ny, radius * nz, nx, ny, nz, uCoord, vCoord };
            };

            Vertex p00 = makePt(phi0, th0, static_cast<float>(sl) / slices, static_cast<float>(st) / stacks);
            Vertex p10 = makePt(phi1, th0, static_cast<float>(sl) / slices, static_cast<float>(st + 1) / stacks);
            Vertex p11 = makePt(phi1, th1, static_cast<float>(sl + 1) / slices, static_cast<float>(st + 1) / stacks);
            Vertex p01 = makePt(phi0, th1, static_cast<float>(sl + 1) / slices, static_cast<float>(st) / stacks);

            // ترتيب CCW نحو الخارج
            pushQuad(v, p00, p01, p11, p10);
        }
    }
    return createMesh(v, GL_TRIANGLES);
}

// بديل glutSolidTorus: يقع في المستوي XY (المحور حول Z)
Mesh buildTorusMesh(float innerRadius, float outerRadius, int sides, int rings) {
    std::vector<Vertex> v;
    for (int r = 0; r < rings; ++r) {
        float u0 = 2.0f * PI_F * static_cast<float>(r) / rings;
        float u1 = 2.0f * PI_F * static_cast<float>(r + 1) / rings;

        for (int s = 0; s < sides; ++s) {
            float v0 = 2.0f * PI_F * static_cast<float>(s) / sides;
            float v1 = 2.0f * PI_F * static_cast<float>(s + 1) / sides;

            auto makePt = [&](float ua, float va, float tu, float tv) -> Vertex {
                float cu = std::cos(ua), su = std::sin(ua);
                float cv = std::cos(va), sv = std::sin(va);
                float px = (outerRadius + innerRadius * cv) * cu;
                float py = (outerRadius + innerRadius * cv) * su;
                float pz = innerRadius * sv;
                float nx = cv * cu;
                float ny = cv * su;
                float nz = sv;
                return { px, py, pz, nx, ny, nz, tu, tv };
            };

            Vertex p00 = makePt(u0, v0, static_cast<float>(r) / rings, static_cast<float>(s) / sides);
            Vertex p10 = makePt(u1, v0, static_cast<float>(r + 1) / rings, static_cast<float>(s) / sides);
            Vertex p11 = makePt(u1, v1, static_cast<float>(r + 1) / rings, static_cast<float>(s + 1) / sides);
            Vertex p01 = makePt(u0, v1, static_cast<float>(r) / rings, static_cast<float>(s + 1) / sides);

            pushQuad(v, p00, p10, p11, p01);
        }
    }
    return createMesh(v, GL_TRIANGLES);
}

// ============================================================================
// بناء مجسمات الغرفة والملصق الممزق والطاولة والبطاقات في VAO/VBO
// ============================================================================
void buildSceneMeshes() {
    float halfW = ROOM_WIDTH / 2.0f;
    float halfD = ROOM_DEPTH / 2.0f;
    float h     = ROOM_HEIGHT;

    float floorTileX = 6.0f, floorTileZ = 6.0f;
    float wallTileU  = 6.0f, wallTileV  = 3.0f;

    // 1. الأرضية الخشبية
    std::vector<Vertex> floorVerts;
    pushQuad(floorVerts,
        {-halfW, 0.0f,  halfD, 0.0f, 1.0f, 0.0f, 0.0f,       0.0f},
        { halfW, 0.0f,  halfD, 0.0f, 1.0f, 0.0f, floorTileX, 0.0f},
        { halfW, 0.0f, -halfD, 0.0f, 1.0f, 0.0f, floorTileX, floorTileZ},
        {-halfW, 0.0f, -halfD, 0.0f, 1.0f, 0.0f, 0.0f,       floorTileZ});
    meshFloor = createMesh(floorVerts, GL_TRIANGLES);

    // 2. الجدران الأربعة
    std::vector<Vertex> wallVerts;
    // الأمامي (Z = -15)
    pushQuad(wallVerts,
        {-halfW, 0.0f, -halfD, 0.0f, 0.0f, 1.0f, 0.0f,      0.0f},
        { halfW, 0.0f, -halfD, 0.0f, 0.0f, 1.0f, wallTileU, 0.0f},
        { halfW, h,    -halfD, 0.0f, 0.0f, 1.0f, wallTileU, wallTileV},
        {-halfW, h,    -halfD, 0.0f, 0.0f, 1.0f, 0.0f,      wallTileV});
    // الخلفي (Z = +15)
    pushQuad(wallVerts,
        { halfW, 0.0f,  halfD, 0.0f, 0.0f, -1.0f, 0.0f,      0.0f},
        {-halfW, 0.0f,  halfD, 0.0f, 0.0f, -1.0f, wallTileU, 0.0f},
        {-halfW, h,     halfD, 0.0f, 0.0f, -1.0f, wallTileU, wallTileV},
        { halfW, h,     halfD, 0.0f, 0.0f, -1.0f, 0.0f,      wallTileV});
    // الأيسر (X = -15)
    pushQuad(wallVerts,
        {-halfW, 0.0f,  halfD, 1.0f, 0.0f, 0.0f, 0.0f,      0.0f},
        {-halfW, 0.0f, -halfD, 1.0f, 0.0f, 0.0f, wallTileU, 0.0f},
        {-halfW, h,    -halfD, 1.0f, 0.0f, 0.0f, wallTileU, wallTileV},
        {-halfW, h,     halfD, 1.0f, 0.0f, 0.0f, 0.0f,      wallTileV});
    // الأيمن (X = +15)
    pushQuad(wallVerts,
        { halfW, 0.0f, -halfD, -1.0f, 0.0f, 0.0f, 0.0f,      0.0f},
        { halfW, 0.0f,  halfD, -1.0f, 0.0f, 0.0f, wallTileU, 0.0f},
        { halfW, h,     halfD, -1.0f, 0.0f, 0.0f, wallTileU, wallTileV},
        { halfW, h,    -halfD, -1.0f, 0.0f, 0.0f, 0.0f,      wallTileV});
    meshWalls = createMesh(wallVerts, GL_TRIANGLES);

    // 3. السقف (Y = 15)
    std::vector<Vertex> ceilVerts;
    pushQuad(ceilVerts,
        {-halfW, h, -halfD, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        { halfW, h, -halfD, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f},
        { halfW, h,  halfD, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        {-halfW, h,  halfD, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f});
    meshCeiling = createMesh(ceilVerts, GL_TRIANGLES);

    // 4. الملصق الممزق (6 رؤوس مقسّمة لمثلثات بطريقة Triangle Fan)
    float xMin = POSTER_CENTER_X - (POSTER_WIDTH / 2.0f);
    float yMin = POSTER_BOTTOM_Y;
    const float tornVertices[6][2] = {
        { 0.00f, 0.00f }, // V0: أسفل يسار
        { 1.00f, 0.00f }, // V1: أسفل يمين
        { 1.00f, 0.65f }, // V2: بداية التمزق على الحافة اليمنى
        { 0.85f, 0.85f }, // V3: رأس التمزق الأوسط
        { 0.60f, 1.00f }, // V4: نهاية التمزق على الحافة العلوية
        { 0.00f, 1.00f }  // V5: أعلى يسار
    };

    Vertex posterV[6];
    for (int i = 0; i < 6; ++i) {
        float u = tornVertices[i][0];
        float v = tornVertices[i][1];
        posterV[i] = { xMin + u * POSTER_WIDTH, yMin + v * POSTER_HEIGHT, POSTER_Z,
                       0.0f, 0.0f, 1.0f, u, v };
    }
    std::vector<Vertex> posterTris;
    for (int i = 1; i + 1 < 6; ++i) {
        posterTris.push_back(posterV[0]);
        posterTris.push_back(posterV[i]);
        posterTris.push_back(posterV[i + 1]);
    }
    meshPoster = createMesh(posterTris, GL_TRIANGLES);

    // حافة التمزق الورقية (V2 -> V3 -> V4)
    std::vector<Vertex> tearEdge;
    for (int i = 2; i <= 4; ++i) {
        Vertex e = posterV[i];
        e.pz = POSTER_Z + 0.008f;
        tearEdge.push_back(e);
    }
    meshPosterTearEdge = createMesh(tearEdge, GL_LINE_STRIP);

    // 5. مجسمات الطاولة وأقسامها
    meshCube = buildCubeMesh();

    std::vector<Vertex> unitQuad;
    pushQuad(unitQuad,
        {-0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        { 0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        { 0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f});
    meshUnitQuadXZ = createMesh(unitQuad, GL_TRIANGLES);

    std::vector<Vertex> unitLoop = {
        {-0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        { 0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        { 0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f}
    };
    meshUnitLoopXZ = createMesh(unitLoop, GL_LINE_LOOP);

    float yZone  = TABLE_HEIGHT + 0.015f;
    float xLeft  = -(TABLE_WIDTH / 2.0f) + 0.55f;
    float xRight =  (TABLE_WIDTH / 2.0f) - 0.55f;
    std::vector<Vertex> divLines = {
        { xLeft,  yZone + 0.01f, -1.15f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
        { xRight, yZone + 0.01f, -1.15f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
        { xLeft,  yZone + 0.01f,  1.15f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
        { xRight, yZone + 0.01f,  1.15f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f }
    };
    meshTableDividers = createMesh(divLines, GL_LINES);

    // 6. مجسمات البطاقة الواحدة (حافتها السفلية عند z=0 لتدور حولها)
    float cardHalfW = CARD_WIDTH / 2.0f;
    std::vector<Vertex> cFront;
    pushQuad(cFront,
        {-cardHalfW, 0.0f,  0.0f,        0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        { cardHalfW, 0.0f,  0.0f,        0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        { cardHalfW, 0.0f, -CARD_HEIGHT, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-cardHalfW, 0.0f, -CARD_HEIGHT, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f});
    meshCardFront = createMesh(cFront, GL_TRIANGLES);

    std::vector<Vertex> cBack;
    pushQuad(cBack,
        {-cardHalfW, -0.004f, -CARD_HEIGHT, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f},
        { cardHalfW, -0.004f, -CARD_HEIGHT, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f},
        { cardHalfW, -0.004f,  0.0f,        0.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        {-cardHalfW, -0.004f,  0.0f,        0.0f, -1.0f, 0.0f, 0.0f, 1.0f});
    meshCardBack = createMesh(cBack, GL_TRIANGLES);

    std::vector<Vertex> cBorder = {
        {-cardHalfW, 0.004f,  0.0f,        0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        { cardHalfW, 0.004f,  0.0f,        0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        { cardHalfW, 0.004f, -CARD_HEIGHT, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-cardHalfW, 0.004f, -CARD_HEIGHT, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f}
    };
    meshCardBorder = createMesh(cBorder, GL_LINE_LOOP);

    // 7. مجسمات قطع الديكور الثلاث
    meshSphere            = buildSphereMesh(1.0f, 24, 24);
    meshDisk              = buildDiskMesh(1.0f, 28);
    meshGyroBase          = buildCylinderMesh(1.25f, 0.95f, 2.4f, 28, 4);
    meshGyroRod           = buildCylinderMesh(0.14f, 0.10f, 0.9f, 18, 2);
    meshTorusOuter        = buildTorusMesh(0.075f, 1.65f, 18, 42);
    meshTorusMiddle       = buildTorusMesh(0.065f, 1.25f, 18, 38);
    meshTorusInner        = buildTorusMesh(0.055f, 0.88f, 16, 32);
    meshTripodLeg         = buildCylinderMesh(0.09f, 0.055f, 3.80f, 14, 2);
    meshTelescopeTube     = buildCylinderMesh(0.28f, 0.42f, 3.6f, 26, 4);
    meshTelescopeRing     = buildCylinderMesh(0.355f, 0.365f, 0.25f, 26, 2);
    meshTelescopeCollar   = buildCylinderMesh(0.43f, 0.45f, 0.32f, 26, 2);
    meshTelescopeEyepiece = buildCylinderMesh(0.14f, 0.18f, 0.68f, 18, 2);
    meshPedestal          = buildCylinderMesh(1.05f, 0.90f, 2.5f, 24, 2);
    meshFallbackCone      = buildCylinderMesh(0.45f, 0.0f, 1.5f, 16, 4);
}

// ============================================================================
// دالة تحميل الخامات (Modern OpenGL 3.3 Core: GL_CLAMP_TO_EDGE + glGenerateMipmap)
// ============================================================================
GLuint loadTexture(const char* filename, bool repeatWrap = true,
                   unsigned char fbR = 180, unsigned char fbG = 180, unsigned char fbB = 180) {
    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    GLint wrapMode = repeatWrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_set_flip_vertically_on_load(true);

    int width = 0, height = 0, nrChannels = 0;
    unsigned char* data = stbi_load(filename, &width, &height, &nrChannels, 4);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (data) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    } else {
        std::cerr << "[Warning] Failed to load texture: " << filename << std::endl;
        unsigned char darkR = static_cast<unsigned char>(fbR * 0.65f);
        unsigned char darkG = static_cast<unsigned char>(fbG * 0.65f);
        unsigned char darkB = static_cast<unsigned char>(fbB * 0.65f);
        unsigned char fallback[2][2][3] = {
            { {fbR, fbG, fbB},       {darkR, darkG, darkB} },
            { {darkR, darkG, darkB}, {fbR, fbG, fbB}       }
        };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, fallback);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}

// ============================================================================
// تحميل ملف OBJ وبناء VAO/VBO موحّد القياس ومتمركز حول الأصل تلقائياً
// ============================================================================
bool loadOBJToMesh(const char* filename, Mesh& outMesh) {
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<ObjFace> faces;

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[Warning] Failed to open OBJ model: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            Vec3 v = { 0.0f, 0.0f, 0.0f };
            iss >> v.x >> v.y >> v.z;
            vertices.push_back(v);
        }
        else if (prefix == "vn") {
            Vec3 n = { 0.0f, 1.0f, 0.0f };
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (prefix == "f") {
            std::vector<int> faceV;
            std::vector<int> faceVN;
            std::string token;

            while (iss >> token) {
                int vIndex = -1;
                int vnIndex = -1;

                size_t firstSlash = token.find('/');
                if (firstSlash == std::string::npos) {
                    vIndex = std::atoi(token.c_str()) - 1;
                } else {
                    vIndex = std::atoi(token.substr(0, firstSlash).c_str()) - 1;
                    size_t secondSlash = token.find('/', firstSlash + 1);
                    if (secondSlash != std::string::npos && secondSlash + 1 < token.size()) {
                        vnIndex = std::atoi(token.substr(secondSlash + 1).c_str()) - 1;
                    }
                }

                if (vIndex < 0 && !vertices.empty()) {
                    vIndex = static_cast<int>(vertices.size()) + vIndex + 1;
                }
                if (vnIndex < -1 && !normals.empty()) {
                    vnIndex = static_cast<int>(normals.size()) + vnIndex + 1;
                }

                faceV.push_back(vIndex);
                faceVN.push_back(vnIndex);
            }

            for (size_t i = 1; i + 1 < faceV.size(); ++i) {
                ObjFace tri;
                tri.vIdx[0]  = faceV[0];  tri.vIdx[1]  = faceV[i];  tri.vIdx[2]  = faceV[i + 1];
                tri.vnIdx[0] = faceVN[0]; tri.vnIdx[1] = faceVN[i]; tri.vnIdx[2] = faceVN[i + 1];
                faces.push_back(tri);
            }
        }
    }

    if (vertices.empty() || faces.empty()) {
        return false;
    }

    Vec3 minB = vertices[0];
    Vec3 maxB = vertices[0];
    for (size_t i = 1; i < vertices.size(); ++i) {
        const Vec3& p = vertices[i];
        minB.x = std::min(minB.x, p.x); maxB.x = std::max(maxB.x, p.x);
        minB.y = std::min(minB.y, p.y); maxB.y = std::max(maxB.y, p.y);
        minB.z = std::min(minB.z, p.z); maxB.z = std::max(maxB.z, p.z);
    }

    Vec3 center = {
        (minB.x + maxB.x) * 0.5f,
        (minB.y + maxB.y) * 0.5f,
        (minB.z + maxB.z) * 0.5f
    };
    float maxExtent = std::max(maxB.x - minB.x, std::max(maxB.y - minB.y, maxB.z - minB.z));
    float scale = (maxExtent > 0.0001f) ? (1.0f / maxExtent) : 1.0f;

    std::vector<Vertex> meshVerts;
    meshVerts.reserve(faces.size() * 3);

    for (size_t i = 0; i < faces.size(); ++i) {
        const ObjFace& f = faces[i];
        if (f.vIdx[0] < 0 || f.vIdx[1] < 0 || f.vIdx[2] < 0 ||
            f.vIdx[0] >= static_cast<int>(vertices.size()) ||
            f.vIdx[1] >= static_cast<int>(vertices.size()) ||
            f.vIdx[2] >= static_cast<int>(vertices.size())) {
            continue;
        }

        bool hasNormals = (f.vnIdx[0] >= 0 && f.vnIdx[0] < static_cast<int>(normals.size()) &&
                           f.vnIdx[1] >= 0 && f.vnIdx[1] < static_cast<int>(normals.size()) &&
                           f.vnIdx[2] >= 0 && f.vnIdx[2] < static_cast<int>(normals.size()));

        Vec3 faceN = { 0.0f, 1.0f, 0.0f };
        if (!hasNormals) {
            const Vec3& a = vertices[f.vIdx[0]];
            const Vec3& b = vertices[f.vIdx[1]];
            const Vec3& c = vertices[f.vIdx[2]];
            float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
            float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            float nx = uy * vz - uz * vy;
            float ny = uz * vx - ux * vz;
            float nz = ux * vy - uy * vx;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 0.0001f) {
                faceN = { nx / len, ny / len, nz / len };
            }
        }

        for (int k = 0; k < 3; ++k) {
            const Vec3& pos = vertices[f.vIdx[k]];
            const Vec3& nrm = hasNormals ? normals[f.vnIdx[k]] : faceN;
            Vertex vert = {
                (pos.x - center.x) * scale,
                (pos.y - center.y) * scale,
                (pos.z - center.z) * scale,
                nrm.x, nrm.y, nrm.z,
                0.0f, 0.0f
            };
            meshVerts.push_back(vert);
        }
    }

    if (meshVerts.empty()) return false;
    outMesh = createMesh(meshVerts, GL_TRIANGLES);
    return true;
}

// ============================================================================
// تهيئة مصفوفة البطاقات الخمس في مخزون اللاعب
// ============================================================================
void initCards() {
    const char* cardFiles[NUM_CARDS] = {
        "card1.jpg", "card2.jpg", "card3.jpg", "card4.jpg", "card5.jpg"
    };

    const unsigned char fallbackColors[NUM_CARDS][3] = {
        { 215,  75,  75 },
        {  70, 140, 220 },
        {  75, 185, 110 },
        { 225, 175,  60 },
        { 165,  90, 210 }
    };

    playedCardsCount = 0;

    for (int i = 0; i < NUM_CARDS; ++i) {
        cards[i].textureID = loadTexture(
            cardFiles[i], false,
            fallbackColors[i][0], fallbackColors[i][1], fallbackColors[i][2]
        );
        cards[i].state = CARD_IN_DECK;

        cards[i].x = (i - 2) * CARD_SPACING;
        cards[i].y = CARD_SURFACE_Y;
        cards[i].z = DECK_PIVOT_Z;
        cards[i].angleX = (cameraMode == 2) ? CARD_STAND_ANGLE : 0.0f;

        cards[i].startX = cards[i].x;
        cards[i].startY = cards[i].y;
        cards[i].startZ = cards[i].z;
        cards[i].startAngleX = cards[i].angleX;
        cards[i].targetX = cards[i].x;
        cards[i].targetY = cards[i].y;
        cards[i].targetZ = cards[i].z;
        cards[i].moveProgress = 0.0f;
    }
}

// ============================================================================
// 1. دالة إعداد مصفوفة الكاميرا (GLM)
// ============================================================================
glm::mat4 getViewMatrix() {
    if (cameraMode == 1) {
        // الحالة 1: كاميرا عمودية تماماً فوق منتصف الطاولة تنظر للأسفل
        return glm::lookAt(
            glm::vec3(0.0f, 13.5f, 0.0f),
            glm::vec3(0.0f, TABLE_HEIGHT, 0.0f),
            glm::vec3(0.0f, 0.0f, -1.0f)
        );
    } else {
        // الحالة 2: منظور اللاعب الجالس أمام الطاولة
        return glm::lookAt(
            glm::vec3(0.0f, 6.8f, 9.2f),
            glm::vec3(0.0f, 2.8f, -1.5f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
    }
}

// ============================================================================
// 2. دوال رسم الغرفة والملصق الممزق والطاولة
// ============================================================================
void drawRoom() {
    glm::mat4 identity(1.0f);
    drawMesh(meshFloor,   identity, glm::vec3(1.0f, 1.0f, 1.0f), texFloorWood, true);
    drawMesh(meshWalls,   identity, glm::vec3(1.0f, 1.0f, 1.0f), texWallpaper, true);
    drawMesh(meshCeiling, identity, glm::vec3(0.88f, 0.86f, 0.82f), 0, true);
}

void drawPoster() {
    glm::mat4 identity(1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawMesh(meshPoster, identity, glm::vec3(1.0f, 1.0f, 1.0f), texPoster, true);
    glDisable(GL_BLEND);

    drawMesh(meshPosterTearEdge, identity, glm::vec3(0.94f, 0.92f, 0.86f), 0, false);
}

void drawTableZone(float xMin, float xMax, float zMin, float zMax,
                   float ySurface, const glm::vec3& color) {
    float cx = (xMin + xMax) * 0.5f;
    float cz = (zMin + zMax) * 0.5f;
    float sx = (xMax - xMin);
    float sz = (zMax - zMin);

    glm::mat4 fillMat = glm::translate(glm::mat4(1.0f), glm::vec3(cx, ySurface, cz));
    fillMat = glm::scale(fillMat, glm::vec3(sx, 1.0f, sz));
    drawMesh(meshUnitQuadXZ, fillMat, color, 0, true);

    glm::mat4 borderMat = glm::translate(glm::mat4(1.0f), glm::vec3(cx, ySurface + 0.008f, cz));
    borderMat = glm::scale(borderMat, glm::vec3(sx, 1.0f, sz));
    drawMesh(meshUnitLoopXZ, borderMat, glm::vec3(0.92f, 0.78f, 0.42f), 0, false);
}

void drawTable() {
    // لوح الطاولة العلوي
    glm::mat4 topMat = glm::translate(glm::mat4(1.0f),
                                      glm::vec3(0.0f, TABLE_HEIGHT - TABLE_THICKNESS / 2.0f, 0.0f));
    topMat = glm::scale(topMat, glm::vec3(TABLE_WIDTH, TABLE_THICKNESS, TABLE_DEPTH));
    drawMesh(meshCube, topMat, glm::vec3(0.36f, 0.22f, 0.12f), 0, true);

    // أرجل الطاولة الأربع
    float legHeight  = TABLE_HEIGHT - TABLE_THICKNESS;
    float legOffsetX = (TABLE_WIDTH / 2.0f) - 0.7f;
    float legOffsetZ = (TABLE_DEPTH / 2.0f) - 0.7f;
    float legPositions[4][2] = {
        { -legOffsetX, -legOffsetZ },
        {  legOffsetX, -legOffsetZ },
        { -legOffsetX,  legOffsetZ },
        {  legOffsetX,  legOffsetZ }
    };

    for (int i = 0; i < 4; ++i) {
        glm::mat4 legMat = glm::translate(glm::mat4(1.0f),
                                          glm::vec3(legPositions[i][0], legHeight / 2.0f, legPositions[i][1]));
        legMat = glm::scale(legMat, glm::vec3(LEG_THICKNESS, legHeight, LEG_THICKNESS));
        drawMesh(meshCube, legMat, glm::vec3(0.28f, 0.16f, 0.08f), 0, true);
    }

    // تقسيم سطح الطاولة إلى 3 أقسام
    float yZone  = TABLE_HEIGHT + 0.015f;
    float xLeft  = -(TABLE_WIDTH / 2.0f) + 0.55f;
    float xRight =  (TABLE_WIDTH / 2.0f) - 0.55f;

    drawTableZone(xLeft, xRight, -3.30f, -1.25f, yZone, glm::vec3(0.14f, 0.28f, 0.22f));
    drawTableZone(xLeft, xRight, -1.05f,  1.05f, yZone, glm::vec3(0.10f, 0.36f, 0.24f));
    drawTableZone(xLeft, xRight,  1.25f,  3.30f, yZone, glm::vec3(0.14f, 0.28f, 0.22f));

    drawMesh(meshTableDividers, glm::mat4(1.0f), glm::vec3(0.95f, 0.85f, 0.55f), 0, false);
}

// ============================================================================
// 3. قطع الديكور الثلاث (الجيروسكوب، التلسكوب، ومنصة المركبة الفضائية)
// ============================================================================
void drawGyroscope() {
    glm::mat4 root = glm::translate(glm::mat4(1.0f), glm::vec3(-9.5f, 0.0f, -10.5f));

    // أ) القاعدة الأسطوانية + الغطاء + العمود النحاسي
    glm::mat4 baseMat = glm::rotate(root, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    drawMesh(meshGyroBase, baseMat, glm::vec3(0.24f, 0.16f, 0.12f), 0, true);

    glm::mat4 capMat = glm::translate(baseMat, glm::vec3(0.0f, 0.0f, 2.4f));
    drawMesh(meshDisk, glm::scale(capMat, glm::vec3(0.95f, 0.95f, 1.0f)),
             glm::vec3(0.24f, 0.16f, 0.12f), 0, true);
    drawMesh(meshGyroRod, capMat, glm::vec3(0.72f, 0.56f, 0.24f), 0, true);

    // ب) مركز الجيروسكوب (الكرة المركزية + 3 حلقات دوّارة)
    glm::mat4 centerMat = glm::translate(root, glm::vec3(0.0f, 4.9f, 0.0f));

    drawMesh(meshSphere, glm::scale(centerMat, glm::vec3(0.52f)),
             glm::vec3(0.95f, 0.72f, 0.22f), 0, true);

    glm::mat4 outerMat = glm::rotate(centerMat, glm::radians(gyroAngleOuter), glm::vec3(0.0f, 1.0f, 0.0f));
    outerMat = glm::rotate(outerMat, glm::radians(22.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    drawMesh(meshTorusOuter, outerMat, glm::vec3(0.82f, 0.65f, 0.28f), 0, true);

    glm::mat4 midMat = glm::rotate(centerMat, glm::radians(gyroAngleMiddle),
                                   glm::normalize(glm::vec3(1.0f, 0.45f, 0.0f)));
    drawMesh(meshTorusMiddle, midMat, glm::vec3(0.75f, 0.78f, 0.84f), 0, true);

    glm::mat4 innerMat = glm::rotate(centerMat, glm::radians(gyroAngleInner),
                                     glm::normalize(glm::vec3(0.35f, 0.25f, 1.0f)));
    drawMesh(meshTorusInner, innerMat, glm::vec3(0.78f, 0.42f, 0.22f), 0, true);
}

void drawTelescope() {
    glm::mat4 root = glm::translate(glm::mat4(1.0f), glm::vec3(9.5f, 0.0f, -10.5f));
    root = glm::rotate(root, glm::radians(-35.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    const float hubY = 3.5f;

    // أ) الحامل ثلاثي القوائم
    for (int i = 0; i < 3; ++i) {
        glm::mat4 legMat = glm::translate(root, glm::vec3(0.0f, hubY, 0.0f));
        legMat = glm::rotate(legMat, glm::radians(i * 120.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        legMat = glm::rotate(legMat, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        legMat = glm::rotate(legMat, glm::radians(22.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        drawMesh(meshTripodLeg, legMat, glm::vec3(0.22f, 0.16f, 0.12f), 0, true);
    }

    // مفصل التثبيت العلوي
    glm::mat4 hubMat = glm::translate(root, glm::vec3(0.0f, hubY, 0.0f));
    drawMesh(meshSphere, glm::scale(hubMat, glm::vec3(0.26f)),
             glm::vec3(0.75f, 0.58f, 0.24f), 0, true);

    // ب) جسم التلسكوب المائل للأعلى مع العدستين
    glm::mat4 scopeMat = glm::translate(root, glm::vec3(0.0f, hubY + 0.32f, 0.0f));
    scopeMat = glm::rotate(scopeMat, glm::radians(-28.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    // الأنبوب الرئيسي والحلقة النحاسية
    glm::mat4 tubeMat = glm::translate(scopeMat, glm::vec3(0.0f, 0.0f, -1.4f));
    drawMesh(meshTelescopeTube, tubeMat, glm::vec3(0.86f, 0.84f, 0.78f), 0, true);

    glm::mat4 ringMat = glm::translate(tubeMat, glm::vec3(0.0f, 0.0f, 1.4f));
    drawMesh(meshTelescopeRing, ringMat, glm::vec3(0.78f, 0.60f, 0.24f), 0, true);

    // العدسة الأمامية وطوقها
    glm::mat4 objMat = glm::translate(scopeMat, glm::vec3(0.0f, 0.0f, 2.2f));
    drawMesh(meshTelescopeCollar, objMat, glm::vec3(0.78f, 0.60f, 0.24f), 0, true);

    glm::mat4 lensMat = glm::translate(objMat, glm::vec3(0.0f, 0.0f, 0.26f));
    drawMesh(meshDisk, glm::scale(lensMat, glm::vec3(0.42f, 0.42f, 1.0f)),
             glm::vec3(0.28f, 0.58f, 0.85f), 0, true);

    // العدسة العينية الخلفية
    glm::mat4 eyeMat = glm::translate(scopeMat, glm::vec3(0.0f, 0.0f, -2.05f));
    drawMesh(meshTelescopeEyepiece, eyeMat, glm::vec3(0.22f, 0.22f, 0.25f), 0, true);
    drawMesh(meshDisk, glm::scale(eyeMat, glm::vec3(0.14f, 0.14f, 1.0f)),
             glm::vec3(0.22f, 0.22f, 0.25f), 0, true);
}

void drawSpaceshipDecor() {
    glm::mat4 root = glm::translate(glm::mat4(1.0f), glm::vec3(-5.2f, 0.0f, -12.6f));

    // أ) منصة العرض الأسطوانية
    glm::mat4 pedMat = glm::rotate(root, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    drawMesh(meshPedestal, pedMat, glm::vec3(0.22f, 0.22f, 0.26f), 0, true);

    glm::mat4 pedTop = glm::translate(pedMat, glm::vec3(0.0f, 0.0f, 2.5f));
    drawMesh(meshDisk, glm::scale(pedTop, glm::vec3(0.90f, 0.90f, 1.0f)),
             glm::vec3(0.75f, 0.62f, 0.30f), 0, true);

    // ب) الموديل ثلاثي الأبعاد (spaceship.obj) مع دوران عرض بطيء
    glm::mat4 shipMat = glm::translate(root, glm::vec3(0.0f, 3.85f, 0.0f));
    shipMat = glm::rotate(shipMat, glm::radians(spaceshipSpin), glm::vec3(0.0f, 1.0f, 0.0f));
    shipMat = glm::rotate(shipMat, glm::radians(12.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    if (spaceshipLoaded) {
        shipMat = glm::scale(shipMat, glm::vec3(2.6f));
        drawMesh(meshSpaceship, shipMat, glm::vec3(0.76f, 0.80f, 0.86f), 0, true);
    } else {
        shipMat = glm::rotate(shipMat, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        drawMesh(meshFallbackCone, shipMat, glm::vec3(0.60f, 0.72f, 0.88f), 0, true);
    }
}

// ============================================================================
// 4. رسم البطاقات الخمس حول حافتها السفلية باستخدام GLM
// ============================================================================
void drawSingleCard(const Card& card) {
    glm::mat4 cardMat = glm::translate(glm::mat4(1.0f), glm::vec3(card.x, card.y, card.z));
    cardMat = glm::rotate(cardMat, glm::radians(card.angleX), glm::vec3(1.0f, 0.0f, 0.0f));

    drawMesh(meshCardFront,  cardMat, glm::vec3(1.0f, 1.0f, 1.0f), card.textureID, true);
    drawMesh(meshCardBack,   cardMat, glm::vec3(0.18f, 0.14f, 0.26f), 0, true);
    drawMesh(meshCardBorder, cardMat, glm::vec3(0.95f, 0.90f, 0.78f), 0, false);
}

void drawCards() {
    for (int i = 0; i < NUM_CARDS; ++i) {
        drawSingleCard(cards[i]);
    }
}

// ============================================================================
// 5. ميكانيكية اختيار ولعب بطاقة عشوائية عند الضغط على زر G
// ============================================================================
void playRandomCard() {
    std::vector<int> availableIndices;
    for (int i = 0; i < NUM_CARDS; ++i) {
        if (cards[i].state == CARD_IN_DECK) {
            availableIndices.push_back(i);
        }
    }

    if (availableIndices.empty()) {
        return; // بعد لعب البطاقات الخمس لا يفعل الضغط على G شيئاً
    }

    int randomPick = availableIndices[std::rand() % availableIndices.size()];
    Card& chosen = cards[randomPick];

    chosen.startX      = chosen.x;
    chosen.startY      = chosen.y;
    chosen.startZ      = chosen.z;
    chosen.startAngleX = chosen.angleX;

    int slotIndex = playedCardsCount;
    playedCardsCount++;

    chosen.targetX = (slotIndex - 2) * CARD_SPACING;
    chosen.targetY = CARD_SURFACE_Y;
    chosen.targetZ = PLAYED_PIVOT_Z;

    chosen.moveProgress = 0.0f;
    chosen.state        = CARD_MOVING;
}

// ============================================================================
// 6. تحديث الأنيميشن بسلاسة اعتماداً على deltaTime
// ============================================================================
void updateAnimation(float dt) {
    gyroAngleOuter  = std::fmod(gyroAngleOuter  + 28.0f * dt, 360.0f);
    gyroAngleMiddle = std::fmod(gyroAngleMiddle + 52.0f * dt, 360.0f);
    gyroAngleInner  = std::fmod(gyroAngleInner  + 85.0f * dt, 360.0f);
    spaceshipSpin   = std::fmod(spaceshipSpin   + 18.0f * dt, 360.0f);

    float deckTargetAngle = (cameraMode == 2) ? CARD_STAND_ANGLE : 0.0f;

    for (int i = 0; i < NUM_CARDS; ++i) {
        Card& c = cards[i];

        if (c.state == CARD_IN_DECK) {
            float diff = deckTargetAngle - c.angleX;
            if (std::fabs(diff) < 0.15f) {
                c.angleX = deckTargetAngle;
            } else {
                c.angleX += diff * std::min(1.0f, TILT_SPEED * dt);
            }
        }
        else if (c.state == CARD_MOVING) {
            c.moveProgress += dt / MOVE_DURATION;
            if (c.moveProgress >= 1.0f) {
                c.moveProgress = 1.0f;
                c.x      = c.targetX;
                c.y      = c.targetY;
                c.z      = c.targetZ;
                c.angleX = 0.0f;
                c.state  = CARD_PLAYED;
            } else {
                float t = c.moveProgress;
                float s = t * t * (3.0f - 2.0f * t);

                c.x = c.startX + (c.targetX - c.startX) * s;
                c.z = c.startZ + (c.targetZ - c.startZ) * s;

                float baseY = c.startY + (c.targetY - c.startY) * s;
                c.y = baseY + std::sin(t * PI_F) * MOVE_ARC_HEIGHT;

                c.angleX = c.startAngleX * (1.0f - s);
            }
        }
    }
}

// ============================================================================
// دالة الرسم الرئيسية
// ============================================================================
void renderScene() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(gShaderProgram);

    // 1. مصفوفات الكاميرا والإسقاط (GLM)
    float aspect = (gWindowHeight > 0)
        ? (static_cast<float>(gWindowWidth) / static_cast<float>(gWindowHeight))
        : 1.0f;
    glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspect, 0.5f, 120.0f);
    glm::mat4 view       = getViewMatrix();

    glUniformMatrix4fv(uLocProjection, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(uLocView,       1, GL_FALSE, glm::value_ptr(view));

    // إعداد الإضاءة الثابتة في فضاء العالم (سقف الغرفة)
    glUniform3f(uLocLightPos,  0.0f, 13.0f, 2.0f);
    glUniform3f(uLocLightAmb,  0.40f, 0.40f, 0.40f);
    glUniform3f(uLocLightDiff, 0.85f, 0.83f, 0.78f);

    // 2. رسم الغرفة والملصق الممزق
    drawRoom();
    drawPoster();

    // 3. رسم قطع الديكور الثلاث
    drawGyroscope();
    drawTelescope();
    drawSpaceshipDecor();

    // 4. رسم الطاولة المقسمة والبطاقات الخمس
    drawTable();
    drawCards();
}

// ============================================================================
// دوال استدعاء GLFW (تغيير الحجم ولوحة المفاتيح)
// ============================================================================
void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    gWindowWidth  = width;
    gWindowHeight = (height > 0) ? height : 1;
    glViewport(0, 0, gWindowWidth, gWindowHeight);
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;

    switch (key) {
        case GLFW_KEY_1:
        case GLFW_KEY_KP_1:
            cameraMode = 1;
            break;
        case GLFW_KEY_2:
        case GLFW_KEY_KP_2:
            cameraMode = 2;
            break;
        case GLFW_KEY_G:
            playRandomCard();
            break;
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GL_TRUE);
            break;
    }
}

// ============================================================================
// التهيئة والتحرير
// ============================================================================
void initScene() {
    std::srand(static_cast<unsigned int>(std::time(NULL)));

    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    // ترجمة وربط المظللات وحفظ مواقع المتغيرات الموحدة (Uniforms)
    gShaderProgram  = createShaderProgram();
    uLocModel       = glGetUniformLocation(gShaderProgram, "uModel");
    uLocView        = glGetUniformLocation(gShaderProgram, "uView");
    uLocProjection  = glGetUniformLocation(gShaderProgram, "uProjection");
    uLocColor       = glGetUniformLocation(gShaderProgram, "uColor");
    uLocUseTexture  = glGetUniformLocation(gShaderProgram, "uUseTexture");
    uLocUseLighting = glGetUniformLocation(gShaderProgram, "uUseLighting");
    uLocTexture     = glGetUniformLocation(gShaderProgram, "uTexture");
    uLocLightPos    = glGetUniformLocation(gShaderProgram, "uLightPos");
    uLocLightAmb    = glGetUniformLocation(gShaderProgram, "uLightAmbient");
    uLocLightDiff   = glGetUniformLocation(gShaderProgram, "uLightDiffuse");

    // بناء مجسمات المشهد في VAO/VBO
    buildSceneMeshes();

    // تحميل خامات الغرفة والملصق
    texFloorWood = loadTexture("floor_wood.jpg", true,  160, 115,  75);
    texWallpaper = loadTexture("wallpaper.jpg",  true,  175, 170, 155);
    texPoster    = loadTexture("poster.png",     false, 200, 135,  90);

    // تهيئة البطاقات الخمس
    initCards();

    // تحميل الموديل الخارجي (spaceship.obj) إلى VAO/VBO
    spaceshipLoaded = loadOBJToMesh("spaceship.obj", meshSpaceship);
}

void cleanupScene() {
    destroyMesh(meshFloor);
    destroyMesh(meshWalls);
    destroyMesh(meshCeiling);
    destroyMesh(meshPoster);
    destroyMesh(meshPosterTearEdge);
    destroyMesh(meshCube);
    destroyMesh(meshUnitQuadXZ);
    destroyMesh(meshUnitLoopXZ);
    destroyMesh(meshTableDividers);
    destroyMesh(meshCardFront);
    destroyMesh(meshCardBack);
    destroyMesh(meshCardBorder);
    destroyMesh(meshSphere);
    destroyMesh(meshDisk);
    destroyMesh(meshGyroBase);
    destroyMesh(meshGyroRod);
    destroyMesh(meshTorusOuter);
    destroyMesh(meshTorusMiddle);
    destroyMesh(meshTorusInner);
    destroyMesh(meshTripodLeg);
    destroyMesh(meshTelescopeTube);
    destroyMesh(meshTelescopeRing);
    destroyMesh(meshTelescopeCollar);
    destroyMesh(meshTelescopeEyepiece);
    destroyMesh(meshPedestal);
    destroyMesh(meshFallbackCone);
    destroyMesh(meshSpaceship);

    if (texFloorWood) glDeleteTextures(1, &texFloorWood);
    if (texWallpaper) glDeleteTextures(1, &texWallpaper);
    if (texPoster)    glDeleteTextures(1, &texPoster);
    for (int i = 0; i < NUM_CARDS; ++i) {
        if (cards[i].textureID) glDeleteTextures(1, &cards[i].textureID);
    }

    if (gShaderProgram) glDeleteProgram(gShaderProgram);
}

// ============================================================================
// نقطة البداية (main) - إدارة النافذة وحلقة الرسم عبر GLFW
// ============================================================================
int main() {
    if (!glfwInit()) {
        std::cerr << "[Error] Failed to initialize GLFW." << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(
        gWindowWidth, gWindowHeight,
        "Card Game Scene (Modern OpenGL 3.3) - Keys: 1 Top-Down | 2 Player | G Play Card",
        NULL, NULL
    );
    if (!window) {
        std::cerr << "[Error] Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // تفعيل V-Sync لسلاسة الحركة

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);

    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        std::cerr << "[Error] Failed to initialize GLEW: "
                  << glewGetErrorString(glewErr) << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    framebufferSizeCallback(window, fbW, fbH);

    initScene();

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float dt = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;
        if (dt > 0.05f) dt = 0.05f;

        updateAnimation(dt);
        renderScene();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    cleanupScene();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
