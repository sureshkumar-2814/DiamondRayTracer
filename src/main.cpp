
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include "spectral.h"


bool saveScreenshot(const char* filename, int width, int height) {
    std::vector<unsigned char> pixels(3 * width * height);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    for (int i = 0; i < height / 2; ++i) {
        int top = 3 * width * i;
        int bot = 3 * width * (height - 1 - i);
        for (int j = 0; j < 3 * width; ++j) {
            std::swap(pixels[top + j], pixels[bot + j]);
        }
    }

    FILE* f = fopen(filename, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(pixels.data(), 1, 3 * width * height, f);
    fclose(f);
    printf("Saved %s\n", filename);
    return true;
}

const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

static uint32_t gFrameIndex = 0;
static float gCenterX = 0.0f;
static float gCenterZ = 0.0f;
static float gRadiusXZ = 1.0f;
static int gCurrentMode = 0;

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

std::string loadTextFile(const char* path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error(std::string("Failed to open file: ") + path);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "Shader compile error:\n" << log << std::endl;
        throw std::runtime_error("Shader compile failed");
    }
    return shader;
}

GLuint compileComputeShader(const char* src) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "Compute shader compile error:\n" << log << std::endl;
        throw std::runtime_error("Compute shader compile failed");
    }
    return shader;
}

GLuint createProgram(const char* vsPath, const char* fsPath) {
    std::string vsSrc = loadTextFile(vsPath);
    std::string fsSrc = loadTextFile(fsPath);
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc.c_str());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc.c_str());
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        std::cerr << "Program link error:\n" << log << std::endl;
        throw std::runtime_error("Program link failed");
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

GLuint createComputeProgram(const char* csPath) {
    std::string csSrc = loadTextFile(csPath);
    GLuint cs = compileComputeShader(csSrc.c_str());
    GLuint program = glCreateProgram();
    glAttachShader(program, cs);
    glLinkProgram(program);
    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(logLen, '\0');
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        std::cerr << "Compute program link error:\n" << log << std::endl;
        throw std::runtime_error("Compute program link failed");
    }
    glDeleteShader(cs);
    return program;
}

struct Vertex {
    float px, py, pz, pw;
    float nx, ny, nz, nw;
};

struct Triangle {
    uint32_t v0, v1, v2, w;
};

std::vector<Vertex> gVertices;
std::vector<Triangle> gTriangles;

bool loadOBJ_Assimp(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals
    );
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
        return false;
    }
    if (scene->mNumMeshes == 0) {
        std::cerr << "No meshes in file: " << path << std::endl;
        return false;
    }

    gVertices.clear();
    gTriangles.clear();
    aiMesh* mesh = scene->mMeshes[0];
    gVertices.reserve(mesh->mNumVertices);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex v{};
        v.px = mesh->mVertices[i].x;
        v.py = mesh->mVertices[i].y;
        v.pz = mesh->mVertices[i].z;
        v.pw = 1.0f;
        if (mesh->HasNormals()) {
            v.nx = mesh->mNormals[i].x;
            v.ny = mesh->mNormals[i].y;
            v.nz = mesh->mNormals[i].z;
        } else {
            v.nx = v.ny = 0.0f;
            v.nz = 1.0f;
        }
        v.nw = 0.0f;
        gVertices.push_back(v);
    }

    gTriangles.reserve(mesh->mNumFaces);
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        if (face.mNumIndices != 3) continue;
        Triangle t{};
        t.v0 = face.mIndices[0];
        t.v1 = face.mIndices[1];
        t.v2 = face.mIndices[2];
        t.w = 0;
        gTriangles.push_back(t);
    }

    float minX = 1e30f, maxX = -1e30f;
    float minZ = 1e30f, maxZ = -1e30f;
    for (const auto& v : gVertices) {
        minX = std::min(minX, v.px);
        maxX = std::max(maxX, v.px);
        minZ = std::min(minZ, v.pz);
        maxZ = std::max(maxZ, v.pz);
    }

    gCenterX = 0.5f * (minX + maxX);
    gCenterZ = 0.5f * (minZ + maxZ);
    float rX = 0.5f * (maxX - minX);
    float rZ = 0.5f * (maxZ - minZ);
    gRadiusXZ = std::max(rX, rZ) * 1.05f;

    std::cout << "Loaded " << gVertices.size() << " vertices, "
              << gTriangles.size() << " triangles from " << path << std::endl;
    std::cout << "XZ bounds: X[" << minX << "," << maxX
              << "], Z[" << minZ << "," << maxZ << "], radius " << gRadiusXZ
              << ", center (" << gCenterX << "," << gCenterZ << ")\n";
    return true;
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        gCurrentMode = (gCurrentMode + 1) % 4;
        gFrameIndex = 0;
        printf("Mode %d ", gCurrentMode);
        if (gCurrentMode == 0) printf("(SPECTRAL BRILLIANCE - White light dispersion)\n"); 
        else if (gCurrentMode == 1) printf("(DIFFUSE DEBUG)\n"); 
        else if (gCurrentMode == 2) printf("(BOUNCE HEATMAP)\n"); 
        else printf("(SPECTRAL FIRE - Sunlight dispersion)\n");

    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        gFrameIndex = 0;
        printf("RESET frames: %d\n", gFrameIndex);
    }
}

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        WINDOW_WIDTH, WINDOW_HEIGHT,
        "Diamond Analyzer - SPACE=Mode R=Reset (Frames: 0/2000)",
        nullptr, nullptr
    );
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(window, key_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    if (!loadOBJ_Assimp("models/diamond7.obj")) {
        std::cerr << "Failed to load models/diamond7.obj\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    int size = std::min(width, height);
    int x = (width - size) / 2;
    int y = (height - size) / 2;
    glViewport(x, y, size, size);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint renderTexture = 0;
    glGenTextures(1, &renderTexture);
    glBindTexture(GL_TEXTURE_2D, renderTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindImageTexture(0, renderTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    GLuint vertexBuffer = 0, triangleBuffer = 0;
    glGenBuffers(1, &vertexBuffer);
    glGenBuffers(1, &triangleBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, vertexBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, gVertices.size() * sizeof(Vertex), gVertices.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vertexBuffer);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, gTriangles.size() * sizeof(Triangle), gTriangles.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, triangleBuffer);

    GLuint computeProgram = 0;
    try {
        computeProgram = createComputeProgram("shaders/raytrace.comp");
    } catch (const std::exception& e) {
        std::cerr << "Compute program error: " << e.what() << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    GLuint displayProgram = 0;
    try {
        displayProgram = createProgram("shaders/fullscreen.vert", "shaders/display.frag");
    } catch (const std::exception& e) {
        std::cerr << "Display program error: " << e.what() << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    GLint uResolutionLoc = glGetUniformLocation(computeProgram, "uResolution");
    GLint uNumVerticesLoc = glGetUniformLocation(computeProgram, "uNumVertices");
    GLint uNumTrianglesLoc = glGetUniformLocation(computeProgram, "uNumTriangles");
    GLint uFrameIndexLoc = glGetUniformLocation(computeProgram, "uFrameIndex");
    GLint uModeLoc = glGetUniformLocation(computeProgram, "uMode");
    GLint uCenterLoc = glGetUniformLocation(computeProgram, "uCenterXZ");
    GLint uRadiusLoc = glGetUniformLocation(computeProgram, "uRadius");
    GLint uDispFrameLoc = glGetUniformLocation(displayProgram, "uFrameIndex");

    std::vector<float> zeroData(WINDOW_WIDTH * WINDOW_HEIGHT * 4, 0.0f);
    glBindTexture(GL_TEXTURE_2D, renderTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, GL_RGBA, GL_FLOAT, zeroData.data());

    printf("CONTROLS: SPACE = cycle modes (0=Brilliance,3=Fire), R = reset, ESC=quit\n");
    printf("Progress shown in title bar: Frames current/max\n");

    const uint32_t MAX_FRAMES = 1000;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        bool accumulating = (gFrameIndex < MAX_FRAMES);
        int mode = gCurrentMode;

        if (accumulating) {
            glUseProgram(computeProgram);

            if (uResolutionLoc >= 0)
                glUniform2f(uResolutionLoc, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT);
            if (uNumVerticesLoc >= 0)
                glUniform1ui(uNumVerticesLoc, static_cast<GLuint>(gVertices.size()));
            if (uNumTrianglesLoc >= 0)
                glUniform1ui(uNumTrianglesLoc, static_cast<GLuint>(gTriangles.size()));
            if (uFrameIndexLoc >= 0)
                glUniform1ui(uFrameIndexLoc, gFrameIndex);
            if (uModeLoc >= 0)
                glUniform1i(uModeLoc, mode);
            if (uCenterLoc >= 0)
                glUniform2f(uCenterLoc, gCenterX, gCenterZ);
            if (uRadiusLoc >= 0)
                glUniform1f(uRadiusLoc, gRadiusXZ);

            uint32_t groupsX = (WINDOW_WIDTH + 7) / 8;
            uint32_t groupsY = (WINDOW_HEIGHT + 7) / 8;
            glDispatchCompute(groupsX, groupsY, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            ++gFrameIndex;
        }

        char title[128];
        float progress = float(gFrameIndex) / float(MAX_FRAMES) * 100.0f;
        snprintf(title, sizeof(title), "Diamond Analyzer - Mode %d - Frame %d/%d (%.1f%%)", 
                 gCurrentMode, gFrameIndex, MAX_FRAMES, progress);
        glfwSetWindowTitle(window, title);

        if (gFrameIndex >= 100 && gFrameIndex % 100 == 0) {
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
            char filename[64];
            const char* modeNames[] = {"brilliance", "diffuse", "bounces", "fire"};
            snprintf(filename, sizeof(filename), "diamond_%s_%04d.ppm", modeNames[gCurrentMode], gFrameIndex);
            saveScreenshot(filename, fbWidth, fbHeight);
        }

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(displayProgram);
        if (uDispFrameLoc >= 0)
            glUniform1ui(uDispFrameLoc, gFrameIndex == 0 ? 1u : gFrameIndex);
        glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderTexture);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glfwSwapBuffers(window);
    }

    glDeleteProgram(computeProgram);
    glDeleteProgram(displayProgram);
    glDeleteTextures(1, &renderTexture);
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteBuffers(1, &triangleBuffer);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
