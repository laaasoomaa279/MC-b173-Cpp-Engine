#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmsystem.h>
#include <GL/gl.h>
#include <immintrin.h>

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <array>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <algorithm>
#include <fstream>
#include <sstream>

// ============================================================================
// OpenGL 3.3 Core Constants & Function Pointers (Zero External Dependencies)
// ============================================================================
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_STATIC_DRAW                    0x88E4
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_TEXTURE0                       0x84C0
#define GL_TEXTURE1                       0x84C1
#define GL_TEXTURE2                       0x84C2
#define GL_TEXTURE_2D_ARRAY               0x8C1A
#define GL_TEXTURE_MAX_LEVEL              0x813D
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_MULTISAMPLE                    0x809D
#endif

#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001
#define WGL_DRAW_TO_WINDOW_ARB            0x2001
#define WGL_SUPPORT_OPENGL_ARB            0x2010
#define WGL_DOUBLE_BUFFER_ARB             0x2011
#define WGL_PIXEL_TYPE_ARB                0x2013
#define WGL_TYPE_RGBA_ARB                 0x202B
#define WGL_COLOR_BITS_ARB                0x2014
#define WGL_DEPTH_BITS_ARB                0x2022
#define WGL_STENCIL_BITS_ARB              0x2023

typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef char      GLchar;

typedef void   (APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void   (APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void   (APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void   (APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void   (APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void   (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void   (APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void   (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void   (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef void   (APIENTRY *PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer);
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void   (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void   (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void   (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void   (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void   (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void   (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint  (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void   (APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void   (APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void   (APIENTRY *PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void   (APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void   (APIENTRY *PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void   (APIENTRY *PFNGLUNIFORM3IPROC)(GLint location, GLint v0, GLint v1, GLint v2);
typedef void   (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void   (APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void   (APIENTRY *PFNGLGENERATEMIPMAPPROC)(GLenum target);
typedef BOOL   (WINAPI   *PFNWGLSWAPINTERVALEXTPROC)(int interval);
typedef HGLRC  (WINAPI   *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int* attribList);
typedef BOOL   (WINAPI   *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats);

inline PFNGLGENBUFFERSPROC              glGenBuffers              = nullptr;
inline PFNGLDELETEBUFFERSPROC           glDeleteBuffers           = nullptr;
inline PFNGLBINDBUFFERPROC              glBindBuffer              = nullptr;
inline PFNGLBUFFERDATAPROC              glBufferData              = nullptr;
inline PFNGLGENVERTEXARRAYSPROC         glGenVertexArrays         = nullptr;
inline PFNGLDELETEVERTEXARRAYSPROC      glDeleteVertexArrays      = nullptr;
inline PFNGLBINDVERTEXARRAYPROC         glBindVertexArray         = nullptr;
inline PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
inline PFNGLVERTEXATTRIBPOINTERPROC     glVertexAttribPointer     = nullptr;
inline PFNGLVERTEXATTRIBIPOINTERPROC    glVertexAttribIPointer    = nullptr;
inline PFNGLCREATESHADERPROC            glCreateShader            = nullptr;
inline PFNGLSHADERSOURCEPROC            glShaderSource            = nullptr;
inline PFNGLCOMPILESHADERPROC           glCompileShader           = nullptr;
inline PFNGLGETSHADERIVPROC             glGetShaderiv             = nullptr;
inline PFNGLGETSHADERINFOLOGPROC        glGetShaderInfoLog        = nullptr;
inline PFNGLDELETESHADERPROC            glDeleteShader            = nullptr;
inline PFNGLCREATEPROGRAMPROC           glCreateProgram           = nullptr;
inline PFNGLATTACHSHADERPROC            glAttachShader            = nullptr;
inline PFNGLLINKPROGRAMPROC             glLinkProgram             = nullptr;
inline PFNGLGETPROGRAMIVPROC            glGetProgramiv            = nullptr;
inline PFNGLGETPROGRAMINFOLOGPROC       glGetProgramInfoLog       = nullptr;
inline PFNGLUSEPROGRAMPROC              glUseProgram              = nullptr;
inline PFNGLDELETEPROGRAMPROC           glDeleteProgram           = nullptr;
inline PFNGLGETUNIFORMLOCATIONPROC      glGetUniformLocation      = nullptr;
inline PFNGLUNIFORM1IPROC               glUniform1i               = nullptr;
inline PFNGLUNIFORM1FPROC               glUniform1f               = nullptr;
inline PFNGLUNIFORM2FPROC               glUniform2f               = nullptr;
inline PFNGLUNIFORM3FPROC               glUniform3f               = nullptr;
inline PFNGLUNIFORM4FPROC               glUniform4f               = nullptr;
inline PFNGLUNIFORM3IPROC               glUniform3i               = nullptr;
inline PFNGLUNIFORMMATRIX4FVPROC        glUniformMatrix4fv        = nullptr;
inline PFNGLACTIVETEXTUREPROC           glActiveTexture           = nullptr;
inline PFNGLGENERATEMIPMAPPROC          glGenerateMipmap          = nullptr;
inline PFNWGLSWAPINTERVALEXTPROC        wglSwapIntervalEXT        = nullptr;

inline bool isValidGLProcPtr(void* p) {
    uintptr_t val = reinterpret_cast<uintptr_t>(p);
    return val > 0x1000 && val != static_cast<uintptr_t>(-1);
}

inline void* loadGLProc(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (!isValidGLProcPtr(p)) {
        static HMODULE oglMod = LoadLibraryA("opengl32.dll");
        if (oglMod) p = reinterpret_cast<void*>(GetProcAddress(oglMod, name));
    }
    return isValidGLProcPtr(p) ? p : nullptr;
}

inline bool loadOpenGL33Functions() {
    glGenBuffers              = (PFNGLGENBUFFERSPROC)loadGLProc("glGenBuffers");
    glDeleteBuffers           = (PFNGLDELETEBUFFERSPROC)loadGLProc("glDeleteBuffers");
    glBindBuffer              = (PFNGLBINDBUFFERPROC)loadGLProc("glBindBuffer");
    glBufferData              = (PFNGLBUFFERDATAPROC)loadGLProc("glBufferData");
    glGenVertexArrays         = (PFNGLGENVERTEXARRAYSPROC)loadGLProc("glGenVertexArrays");
    glDeleteVertexArrays      = (PFNGLDELETEVERTEXARRAYSPROC)loadGLProc("glDeleteVertexArrays");
    glBindVertexArray         = (PFNGLBINDVERTEXARRAYPROC)loadGLProc("glBindVertexArray");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)loadGLProc("glEnableVertexAttribArray");
    glVertexAttribPointer     = (PFNGLVERTEXATTRIBPOINTERPROC)loadGLProc("glVertexAttribPointer");
    glVertexAttribIPointer    = (PFNGLVERTEXATTRIBIPOINTERPROC)loadGLProc("glVertexAttribIPointer");
    glCreateShader            = (PFNGLCREATESHADERPROC)loadGLProc("glCreateShader");
    glShaderSource            = (PFNGLSHADERSOURCEPROC)loadGLProc("glShaderSource");
    glCompileShader           = (PFNGLCOMPILESHADERPROC)loadGLProc("glCompileShader");
    glGetShaderiv             = (PFNGLGETSHADERIVPROC)loadGLProc("glGetShaderiv");
    glGetShaderInfoLog        = (PFNGLGETSHADERINFOLOGPROC)loadGLProc("glGetShaderInfoLog");
    glDeleteShader            = (PFNGLDELETESHADERPROC)loadGLProc("glDeleteShader");
    glCreateProgram           = (PFNGLCREATEPROGRAMPROC)loadGLProc("glCreateProgram");
    glAttachShader            = (PFNGLATTACHSHADERPROC)loadGLProc("glAttachShader");
    glLinkProgram             = (PFNGLLINKPROGRAMPROC)loadGLProc("glLinkProgram");
    glGetProgramiv            = (PFNGLGETPROGRAMIVPROC)loadGLProc("glGetProgramiv");
    glGetProgramInfoLog       = (PFNGLGETPROGRAMINFOLOGPROC)loadGLProc("glGetProgramInfoLog");
    glUseProgram              = (PFNGLUSEPROGRAMPROC)loadGLProc("glUseProgram");
    glDeleteProgram           = (PFNGLDELETEPROGRAMPROC)loadGLProc("glDeleteProgram");
    glGetUniformLocation      = (PFNGLGETUNIFORMLOCATIONPROC)loadGLProc("glGetUniformLocation");
    glUniform1i               = (PFNGLUNIFORM1IPROC)loadGLProc("glUniform1i");
    glUniform1f               = (PFNGLUNIFORM1FPROC)loadGLProc("glUniform1f");
    glUniform2f               = (PFNGLUNIFORM2FPROC)loadGLProc("glUniform2f");
    glUniform3f               = (PFNGLUNIFORM3FPROC)loadGLProc("glUniform3f");
    glUniform4f               = (PFNGLUNIFORM4FPROC)loadGLProc("glUniform4f");
    glUniform3i               = (PFNGLUNIFORM3IPROC)loadGLProc("glUniform3i");
    glUniformMatrix4fv        = (PFNGLUNIFORMMATRIX4FVPROC)loadGLProc("glUniformMatrix4fv");
    glActiveTexture           = (PFNGLACTIVETEXTUREPROC)loadGLProc("glActiveTexture");
    glGenerateMipmap          = (PFNGLGENERATEMIPMAPPROC)loadGLProc("glGenerateMipmap");
    wglSwapIntervalEXT        = (PFNWGLSWAPINTERVALEXTPROC)loadGLProc("wglSwapIntervalEXT");

    return glGenBuffers && glDeleteBuffers && glBindBuffer && glBufferData &&
           glGenVertexArrays && glDeleteVertexArrays && glBindVertexArray &&
           glEnableVertexAttribArray && glVertexAttribPointer && glVertexAttribIPointer &&
           glCreateShader && glShaderSource && glCompileShader && glGetShaderiv &&
           glCreateProgram && glAttachShader && glLinkProgram && glGetProgramiv &&
           glUseProgram && glGetUniformLocation && glUniform1i && glUniform1f &&
           glUniform3f && glUniformMatrix4fv && glActiveTexture && glGenerateMipmap;
}

inline GLuint compileShaderStage(GLenum type, const std::string& source, const char* debugName) {
    if (!glCreateShader || !glShaderSource || !glCompileShader) return 0;
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        if (glGetShaderInfoLog) glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Shader Error] %s:\n%s\n", debugName, log);
        MessageBoxA(nullptr, log, debugName, MB_ICONERROR);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

inline GLuint compileShaderProgramFromFiles(const std::string& vertPath, const std::string& fragPath) {
    auto readText = [](const std::string& path) -> std::string {
        const std::string prefixes[] = { "", "../", "../../" };
        for (const auto& pre : prefixes) {
            std::ifstream f(pre + path, std::ios::binary);
            if (f.is_open()) {
                std::ostringstream ss;
                ss << f.rdbuf();
                return ss.str();
            }
        }
        return {};
    };

    std::string vSrc = readText(vertPath);
    std::string fSrc = readText(fragPath);
    if (vSrc.empty() || fSrc.empty()) {
        std::string err = "Missing shader files:\n" + vertPath + "\n" + fragPath;
        std::fprintf(stderr, "[Shader Error] %s\n", err.c_str());
        MessageBoxA(nullptr, err.c_str(), "Shader Missing Error", MB_ICONERROR);
        return 0;
    }

    GLuint vs = compileShaderStage(GL_VERTEX_SHADER, vSrc, vertPath.c_str());
    GLuint fs = compileShaderStage(GL_FRAGMENT_SHADER, fSrc, fragPath.c_str());
    if (!vs || !fs) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024]{};
        if (glGetProgramInfoLog) glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[Shader Link Error]:\n%s\n", log);
        MessageBoxA(nullptr, log, "Shader Link Error", MB_ICONERROR);
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ============================================================================
// 3D Linear Algebra & Frustum Culling (Vec3, Mat4, Frustum)
// ============================================================================
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s)       const { return { x * s,   y * s,   z * s }; }

    float dot(const Vec3& o)      const { return x * o.x + y * o.y + z * o.z; }
    float lengthSq()              const { return x * x + y * y + z * z; }
    float length()                const { return std::sqrt(lengthSq()); }

    Vec3 cross(const Vec3& o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }

    Vec3 normalized() const {
        float len = length();
        return (len > 1e-6f) ? Vec3{ x / len, y / len, z / len } : Vec3{ 0.0f, 0.0f, 0.0f };
    }
};

struct Mat4 {
    float m[16]{};

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
        Mat4 r{};
        r.m[0]  =  2.0f / (right - left);
        r.m[5]  =  2.0f / (top - bottom);
        r.m[10] = -2.0f / (farZ - nearZ);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(farZ + nearZ) / (farZ - nearZ);
        r.m[15] =  1.0f;
        return r;
    }

    static Mat4 perspective(float fovDegrees, float aspect, float nearZ, float farZ) {
        float rad = fovDegrees * 0.017453292519943295f;
        float tanHalf = std::tan(rad * 0.5f);
        Mat4 r{};
        r.m[0]  = 1.0f / (aspect * tanHalf);
        r.m[5]  = 1.0f / tanHalf;
        r.m[10] = -(farZ + nearZ) / (farZ - nearZ);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);

        Mat4 r = identity();
        r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
        r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] =  f.dot(eye);
        return r;
    }

    static Mat4 translate(float x, float y, float z) {
        Mat4 r = identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }

    static Mat4 scale(float x, float y, float z) {
        Mat4 r{};
        r.m[0] = x; r.m[5] = y; r.m[10] = z; r.m[15] = 1.0f;
        return r;
    }

    static Mat4 rotateX(float deg) {
        float c = std::cos(deg * 0.0174532925f), s = std::sin(deg * 0.0174532925f);
        Mat4 r = identity();
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateY(float deg) {
        float c = std::cos(deg * 0.0174532925f), s = std::sin(deg * 0.0174532925f);
        Mat4 r = identity();
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }

    static Mat4 rotateZ(float deg) {
        float c = std::cos(deg * 0.0174532925f), s = std::sin(deg * 0.0174532925f);
        Mat4 r = identity();
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                r.m[col * 4 + row] =
                    m[0 * 4 + row] * b.m[col * 4 + 0] +
                    m[1 * 4 + row] * b.m[col * 4 + 1] +
                    m[2 * 4 + row] * b.m[col * 4 + 2] +
                    m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return r;
    }
};

struct Frustum {
    float planes[6][4]{};

    void extract(const Mat4& vp) {
        for (int i = 0; i < 4; ++i) planes[0][i] = vp.m[i * 4 + 3] + vp.m[i * 4 + 0];
        for (int i = 0; i < 4; ++i) planes[1][i] = vp.m[i * 4 + 3] - vp.m[i * 4 + 0];
        for (int i = 0; i < 4; ++i) planes[2][i] = vp.m[i * 4 + 3] + vp.m[i * 4 + 1];
        for (int i = 0; i < 4; ++i) planes[3][i] = vp.m[i * 4 + 3] - vp.m[i * 4 + 1];
        for (int i = 0; i < 4; ++i) planes[4][i] = vp.m[i * 4 + 3] + vp.m[i * 4 + 2];
        for (int i = 0; i < 4; ++i) planes[5][i] = vp.m[i * 4 + 3] - vp.m[i * 4 + 2];

        for (int p = 0; p < 6; ++p) {
            float invLen = 1.0f / std::sqrt(planes[p][0] * planes[p][0] +
                                            planes[p][1] * planes[p][1] +
                                            planes[p][2] * planes[p][2]);
            for (int i = 0; i < 4; ++i) planes[p][i] *= invLen;
        }
    }

    bool isBoxVisible(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) const {
        for (int p = 0; p < 6; ++p) {
            float px = (planes[p][0] > 0.0f) ? maxX : minX;
            float py = (planes[p][1] > 0.0f) ? maxY : minY;
            float pz = (planes[p][2] > 0.0f) ? maxZ : minZ;
            if (planes[p][0] * px + planes[p][1] * py + planes[p][2] * pz + planes[p][3] < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

// ============================================================================
// Lock-Free / Low-Contention Ring Queues & Priority WorkerPool
// ============================================================================
template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    T                   buffer[Capacity];
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};

public:
    bool push(T&& item) {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t next = (t + 1) & (Capacity - 1);
        if (next == head.load(std::memory_order_acquire)) return false;
        buffer[t] = std::move(item);
        tail.store(next, std::memory_order_release);
        return true;
    }

    bool push(const T& item) {
        T copy = item;
        return push(std::move(copy));
    }

    bool pop(T& out) {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        out = std::move(buffer[h]);
        head.store((h + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
};

template<typename T, size_t Capacity>
class MPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    T                   buffer[Capacity];
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
    std::atomic_flag    producerLock = ATOMIC_FLAG_INIT;

public:
    bool push(const T& item) {
        while (producerLock.test_and_set(std::memory_order_acquire)) {
            _mm_pause();
        }
        size_t t = tail.load(std::memory_order_relaxed);
        size_t next = (t + 1) & (Capacity - 1);
        bool ok = (next != head.load(std::memory_order_acquire));
        if (ok) {
            buffer[t] = item;
            tail.store(next, std::memory_order_release);
        }
        producerLock.clear(std::memory_order_release);
        return ok;
    }

    bool pop(T& out) {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        out = std::move(buffer[h]);
        head.store((h + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
};

class WorkerPool {
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> highPriorityQueue;
    std::queue<std::function<void()>> lowPriorityQueue;
    std::mutex                        mtx;
    std::condition_variable           cv;
    std::atomic<bool>                 stopFlag{false};
    std::atomic<size_t>               activeTasks{0};

public:
    ~WorkerPool() { shutdown(); }

    void start(unsigned int count) {
        if (!workers.empty()) return;
        stopFlag.store(false, std::memory_order_release);
        unsigned int n = std::max(1u, count);
        for (unsigned int i = 0; i < n; ++i) {
            workers.emplace_back([this]() {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock, [this]() {
                            return stopFlag.load(std::memory_order_acquire) ||
                                   !highPriorityQueue.empty() ||
                                   !lowPriorityQueue.empty();
                        });
                        if (stopFlag.load(std::memory_order_acquire) &&
                            highPriorityQueue.empty() && lowPriorityQueue.empty()) {
                            return;
                        }
                        if (!highPriorityQueue.empty()) {
                            task = std::move(highPriorityQueue.front());
                            highPriorityQueue.pop();
                        } else {
                            task = std::move(lowPriorityQueue.front());
                            lowPriorityQueue.pop();
                        }
                        activeTasks.fetch_add(1, std::memory_order_relaxed);
                    }
                    task();
                    activeTasks.fetch_sub(1, std::memory_order_relaxed);
                }
            });
        }
    }

    void submit(std::function<void()> fn, bool highPriority = false) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (highPriority) highPriorityQueue.push(std::move(fn));
            else              lowPriorityQueue.push(std::move(fn));
        }
        cv.notify_one();
    }

    void clearPending() {
        std::lock_guard<std::mutex> lock(mtx);
        while (!highPriorityQueue.empty()) highPriorityQueue.pop();
        while (!lowPriorityQueue.empty())  lowPriorityQueue.pop();
    }

    size_t pendingCount() {
        std::lock_guard<std::mutex> lock(mtx);
        return highPriorityQueue.size() + lowPriorityQueue.size() + activeTasks.load(std::memory_order_relaxed);
    }

    unsigned int threadCount() const { return (unsigned int)workers.size(); }

    void shutdown() {
        stopFlag.store(true, std::memory_order_release);
        cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
        workers.clear();
    }
};

// ============================================================================
// Dedicated-Thread 44.1 kHz Stereo 3D Audio Engine (WinMM waveOut)
// ============================================================================
struct SoundBuffer {
    std::vector<int16_t> samples;
};

class AudioEngine {
    static constexpr int SAMPLE_RATE   = 44100;
    static constexpr int NUM_BUFFERS   = 4;
    static constexpr int BUFFER_FRAMES = 1024;

    struct ActiveVoice {
        const SoundBuffer* buf       = nullptr;
        double             cursor    = 0.0;
        float              pitch     = 1.0f;
        float              gainLeft  = 1.0f;
        float              gainRight = 1.0f;
    };

    HWAVEOUT                 hWaveOut = nullptr;
    WAVEHDR                  headers[NUM_BUFFERS]{};
    std::vector<int16_t>     pcmBuffers[NUM_BUFFERS];
    std::vector<ActiveVoice> voices;
    std::atomic_flag         voiceLock = ATOMIC_FLAG_INIT;
    std::thread              mixerThread;
    std::atomic<bool>        running{false};

public:
    ~AudioEngine() { shutdown(); }

    bool init() {
        WAVEFORMATEX wfx{};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = 2;
        wfx.nSamplesPerSec  = SAMPLE_RATE;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = 4;
        wfx.nAvgBytesPerSec = SAMPLE_RATE * 4;

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            return false;
        }

        for (int i = 0; i < NUM_BUFFERS; ++i) {
            pcmBuffers[i].resize(BUFFER_FRAMES * 2, 0);
            headers[i].lpData         = (LPSTR)pcmBuffers[i].data();
            headers[i].dwBufferLength = (DWORD)(pcmBuffers[i].size() * sizeof(int16_t));
            waveOutPrepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
            waveOutWrite(hWaveOut, &headers[i], sizeof(WAVEHDR));
        }

        running.store(true, std::memory_order_release);
        mixerThread = std::thread([this]() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            while (running.load(std::memory_order_acquire)) {
                pumpBuffers();
                Sleep(8);
            }
        });
        return true;
    }

    SoundBuffer resampleTo44100Mono(const int16_t* interleaved, int frames, int channels, int srcRate) const {
        SoundBuffer out;
        if (!interleaved || frames <= 0 || channels <= 0 || srcRate <= 0) return out;
        double ratio = (double)srcRate / (double)SAMPLE_RATE;
        size_t dstFrames = (size_t)((double)frames / ratio);
        out.samples.resize(dstFrames);

        for (size_t i = 0; i < dstFrames; ++i) {
            size_t srcIdx = std::min<size_t>((size_t)(i * ratio), (size_t)(frames - 1));
            int sum = 0;
            for (int c = 0; c < channels; ++c) sum += interleaved[srcIdx * channels + c];
            out.samples[i] = (int16_t)(sum / channels);
        }
        return out;
    }

    void play2D(const SoundBuffer& buf, float volume = 1.0f, float pitch = 1.0f) {
        if (!hWaveOut || buf.samples.empty() || volume <= 0.001f) return;
        while (voiceLock.test_and_set(std::memory_order_acquire)) _mm_pause();
        if (voices.size() < 48) {
            voices.push_back({ &buf, 0.0, std::clamp(pitch, 0.5f, 2.0f), volume, volume });
        }
        voiceLock.clear(std::memory_order_release);
    }

    void play3D(const SoundBuffer& buf, float volume, float pitch,
                const Vec3& soundPos, const Vec3& listenerPos, float listenerYawDeg, bool attenuate = true) {
        if (!hWaveOut || buf.samples.empty() || volume <= 0.001f) return;

        float gain = volume;
        float pan  = 0.0f;
        if (attenuate) {
            Vec3 delta = soundPos - listenerPos;
            float dist = delta.length();
            float maxDist = std::max(16.0f, volume * 16.0f);
            if (dist >= maxDist) return;
            gain *= (1.0f - dist / maxDist);

            if (dist > 0.01f) {
                float yawRad = listenerYawDeg * 0.0174532925f;
                Vec3 rightVec{ -std::sin(yawRad), 0.0f, std::cos(yawRad) };
                pan = std::clamp(delta.normalized().dot(rightVec), -1.0f, 1.0f);
            }
        }

        float lGain = gain * std::sqrt(0.5f * (1.0f - pan));
        float rGain = gain * std::sqrt(0.5f * (1.0f + pan));

        while (voiceLock.test_and_set(std::memory_order_acquire)) _mm_pause();
        if (voices.size() < 48) {
            voices.push_back({ &buf, 0.0, std::clamp(pitch, 0.5f, 2.0f), lGain, rGain });
        }
        voiceLock.clear(std::memory_order_release);
    }

    void shutdown() {
        running.store(false, std::memory_order_release);
        if (mixerThread.joinable()) mixerThread.join();
        if (hWaveOut) {
            waveOutReset(hWaveOut);
            for (int i = 0; i < NUM_BUFFERS; ++i) {
                waveOutUnprepareHeader(hWaveOut, &headers[i], sizeof(WAVEHDR));
            }
            waveOutClose(hWaveOut);
            hWaveOut = nullptr;
        }
    }

private:
    void pumpBuffers() {
        if (!hWaveOut) return;
        for (int b = 0; b < NUM_BUFFERS; ++b) {
            if (!(headers[b].dwFlags & WHDR_DONE)) continue;

            std::memset(pcmBuffers[b].data(), 0, pcmBuffers[b].size() * sizeof(int16_t));

            while (voiceLock.test_and_set(std::memory_order_acquire)) _mm_pause();
            for (size_t v = 0; v < voices.size(); ) {
                ActiveVoice& vc = voices[v];
                const auto& smp = vc.buf->samples;
                for (int f = 0; f < BUFFER_FRAMES; ++f) {
                    size_t idx = (size_t)vc.cursor;
                    if (idx >= smp.size()) break;
                    float s = (float)smp[idx];
                    int l = (int)pcmBuffers[b][f * 2 + 0] + (int)(s * vc.gainLeft);
                    int r = (int)pcmBuffers[b][f * 2 + 1] + (int)(s * vc.gainRight);
                    pcmBuffers[b][f * 2 + 0] = (int16_t)std::clamp(l, -32767, 32767);
                    pcmBuffers[b][f * 2 + 1] = (int16_t)std::clamp(r, -32767, 32767);
                    vc.cursor += (double)vc.pitch;
                }
                if ((size_t)vc.cursor >= smp.size()) {
                    voices[v] = voices.back();
                    voices.pop_back();
                } else {
                    ++v;
                }
            }
            voiceLock.clear(std::memory_order_release);

            waveOutWrite(hWaveOut, &headers[b], sizeof(WAVEHDR));
        }
    }
};

// ============================================================================
// Win32 Native Window, OpenGL 3.3 Core Context & Raw Mouse Input
// ============================================================================
class PlatformWindow {
public:
    HWND          hwnd         = nullptr;
    HDC           hdc          = nullptr;
    HGLRC         hglrc        = nullptr;
    int           width        = 1280;
    int           height       = 720;
    bool          running      = false;
    bool          mouseLocked  = false;

    bool          keys[256]{};
    bool          keysPressed[256]{};
    int           mouseX       = 0;
    int           mouseY       = 0;
    float         rawMouseDX   = 0.0f;
    float         rawMouseDY   = 0.0f;
    int           wheelDelta   = 0;
    bool          leftDown     = false;
    bool          leftClicked  = false;
    bool          rightDown    = false;
    bool          rightClicked = false;
    std::string   typedChars;

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastCounter{};

    ~PlatformWindow() {
        if (hglrc) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc);
            hglrc = nullptr;
        }
        if (hwnd && hdc) {
            ReleaseDC(hwnd, hdc);
            hdc = nullptr;
        }
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    bool create(const char* title, int w, int h) {
        width  = w;
        height = h;
        HINSTANCE hInst = GetModuleHandleA(nullptr);

        // 1. Bootstrap WGL extensions via temporary dummy window
        WNDCLASSA dummyWc{};
        dummyWc.style         = CS_OWNDC;
        dummyWc.lpfnWndProc   = DefWindowProcA;
        dummyWc.hInstance     = hInst;
        dummyWc.lpszClassName = "DummyWGL_B173";
        RegisterClassA(&dummyWc);

        HWND dummyWnd = CreateWindowA("DummyWGL_B173", "", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr, hInst, nullptr);
        if (!dummyWnd) {
            MessageBoxA(nullptr, "CreateWindowA failed for dummy WGL window.", "Win32 Error", MB_ICONERROR);
            return false;
        }
        HDC dummyDC = GetDC(dummyWnd);

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize        = sizeof(pfd);
        pfd.nVersion     = 1;
        pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType   = PFD_TYPE_RGBA;
        pfd.cColorBits   = 32;
        pfd.cDepthBits   = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType   = PFD_MAIN_PLANE;

        int pf = ChoosePixelFormat(dummyDC, &pfd);
        SetPixelFormat(dummyDC, pf, &pfd);

        HGLRC dummyRC = wglCreateContext(dummyDC);
        if (!dummyRC) {
            MessageBoxA(nullptr, "wglCreateContext failed for dummy context.", "OpenGL Error", MB_ICONERROR);
            return false;
        }
        wglMakeCurrent(dummyDC, dummyRC);

        auto wglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
        auto wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(dummyRC);
        ReleaseDC(dummyWnd, dummyDC);
        DestroyWindow(dummyWnd);

        // 2. Register and create main Win32 window
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc   = wndProcStatic;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "MinecraftB173WindowClass";
        RegisterClassExA(&wc);

        RECT rect{ 0, 0, width, height };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        hwnd = CreateWindowExA(
            0, "MinecraftB173WindowClass", title,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, hInst, this
        );
        if (!hwnd) {
            MessageBoxA(nullptr, "CreateWindowExA failed for main window.", "Win32 Error", MB_ICONERROR);
            return false;
        }

        hdc = GetDC(hwnd);

        if (isValidGLProcPtr((void*)wglChoosePixelFormatARB) && isValidGLProcPtr((void*)wglCreateContextAttribsARB)) {
            const int pixelAttribs[] = {
                WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
                WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
                WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
                WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
                WGL_COLOR_BITS_ARB,     32,
                WGL_DEPTH_BITS_ARB,     24,
                WGL_STENCIL_BITS_ARB,   8,
                0
            };
            int format = 0;
            UINT numFormats = 0;
            if (wglChoosePixelFormatARB(hdc, pixelAttribs, nullptr, 1, &format, &numFormats) && numFormats > 0) {
                PIXELFORMATDESCRIPTOR chosenPfd = pfd;
                DescribePixelFormat(hdc, format, sizeof(chosenPfd), &chosenPfd);
                SetPixelFormat(hdc, format, &chosenPfd);
            } else {
                int fallbackPf = ChoosePixelFormat(hdc, &pfd);
                SetPixelFormat(hdc, fallbackPf, &pfd);
            }

            const int ctxAttribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            hglrc = wglCreateContextAttribsARB(hdc, nullptr, ctxAttribs);
        }

        if (!hglrc) {
            int fallbackPf = ChoosePixelFormat(hdc, &pfd);
            SetPixelFormat(hdc, fallbackPf, &pfd);
            hglrc = wglCreateContext(hdc);
        }

        if (!hglrc || !wglMakeCurrent(hdc, hglrc)) {
            MessageBoxA(nullptr, "Failed to create or activate OpenGL 3.3 Core context.", "OpenGL Error", MB_ICONERROR);
            return false;
        }

        if (!loadOpenGL33Functions()) {
            const char* ver = (const char*)glGetString(GL_VERSION);
            char err[256];
            std::snprintf(err, sizeof(err), "OpenGL 3.3 functions missing! Detected GPU OpenGL: %s", ver ? ver : "Unknown");
            MessageBoxA(nullptr, err, "OpenGL Version Error", MB_ICONERROR);
            return false;
        }

        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);

        // Register Raw Mouse Input (WM_INPUT)
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x02;
        rid.hwndTarget  = hwnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));

        QueryPerformanceFrequency(&qpcFreq);
        QueryPerformanceCounter(&lastCounter);
        running = true;
        return true;
    }

    float pumpEvents() {
        rawMouseDX   = 0.0f;
        rawMouseDY   = 0.0f;
        wheelDelta   = 0;
        leftClicked  = false;
        rightClicked = false;
        typedChars.clear();
        std::memset(keysPressed, 0, sizeof(keysPressed));

        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (mouseLocked && GetForegroundWindow() == hwnd) {
            RECT r;
            GetClientRect(hwnd, &r);
            POINT center{ (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - lastCounter.QuadPart) / (float)qpcFreq.QuadPart;
        lastCounter = now;
        return std::clamp(dt, 0.0001f, 0.1f);
    }

    void setMouseLock(bool lock) {
        if (mouseLocked == lock) return;
        mouseLocked = lock;
        if (lock) {
            while (ShowCursor(FALSE) >= 0) {}
        } else {
            while (ShowCursor(TRUE) < 0) {}
        }
    }

    void swapBuffers() {
        if (hdc) SwapBuffers(hdc);
    }

    void setTitle(const char* title) {
        if (hwnd) SetWindowTextA(hwnd, title);
    }

private:
    static LRESULT CALLBACK wndProcStatic(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        PlatformWindow* self = nullptr;
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
            self = (PlatformWindow*)cs->lpCreateParams;
            SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = (PlatformWindow*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
        }
        if (self) return self->handleMsg(hWnd, uMsg, wParam, lParam);
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }

    LRESULT handleMsg(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_CLOSE:
            case WM_DESTROY:
                running = false;
                PostQuitMessage(0);
                return 0;

            case WM_SIZE:
                width  = std::max(1, (int)LOWORD(lParam));
                height = std::max(1, (int)HIWORD(lParam));
                glViewport(0, 0, width, height);
                return 0;

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                uint8_t vk = (uint8_t)(wParam & 0xFF);
                if (!keys[vk]) keysPressed[vk] = true;
                keys[vk] = true;
                return 0;
            }

            case WM_KEYUP:
            case WM_SYSKEYUP: {
                uint8_t vk = (uint8_t)(wParam & 0xFF);
                keys[vk] = false;
                return 0;
            }

            case WM_CHAR:
                if (wParam >= 32 && wParam < 127) {
                    typedChars.push_back((char)wParam);
                }
                return 0;

            case WM_MOUSEMOVE:
                mouseX = (int)(short)LOWORD(lParam);
                mouseY = (int)(short)HIWORD(lParam);
                return 0;

            case WM_INPUT: {
                UINT dwSize = sizeof(RAWINPUT);
                RAWINPUT raw{};
                if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER)) == sizeof(RAWINPUT)) {
                    if (raw.header.dwType == RIM_TYPEMOUSE && mouseLocked) {
                        rawMouseDX += (float)raw.data.mouse.lLastX;
                        rawMouseDY += (float)raw.data.mouse.lLastY;
                    }
                }
                return 0;
            }

            case WM_LBUTTONDOWN:
                if (!leftDown) leftClicked = true;
                leftDown = true;
                return 0;
            case WM_LBUTTONUP:
                leftDown = false;
                return 0;
            case WM_RBUTTONDOWN:
                if (!rightDown) rightClicked = true;
                rightDown = true;
                return 0;
            case WM_RBUTTONUP:
                rightDown = false;
                return 0;
            case WM_MOUSEWHEEL:
                wheelDelta += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                return 0;
        }
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }
};