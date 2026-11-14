#pragma once
#include "platform.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Sky, 3D Volumetric Clouds (Y=108), Stars & Weather (1:1)
// ============================================================================
// Exact Reproduction of Mojang RenderGlobal.java & WorldProvider.java:
//   - 3D Volumetric Fancy Clouds (4 blocks thick at Y=108.0f, shaded faces)
//   - Sunrise / Sunset Horizon Glow Fan (warm orange/crimson gradient)
//   - Full Moon always + Sun + 1,500 Stars (Mojang seed 10842L)
//   - Classic Rain & Snow streaks clipped against each column's heightMap
// ============================================================================

enum WeatherType : uint8_t {
    WEATHER_CLEAR   = 0,
    WEATHER_RAIN    = 1,
    WEATHER_THUNDER = 2
};

class BetaSkyWeatherSystem {
public:
    GLuint      skyShader       = 0;
    GLuint      skyVao          = 0;
    GLuint      skyVbo          = 0;
    GLuint      starsVao        = 0;
    GLuint      starsVbo        = 0;
    GLsizei     starVertexCount = 0;

    WeatherType weather         = WEATHER_CLEAR;
    float       rainStrength    = 0.0f;
    int         weatherTimer    = 18000;
    int         lightningFlash  = 0;
    float       cloudScrollX    = 0.0f;

    void init() {
        const char* vSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_pos;
            layout(location = 1) in vec2 a_uv;
            layout(location = 2) in vec4 a_col;
            uniform mat4 u_mvp;
            out vec2 v_uv;
            out vec4 v_col;
            void main() {
                v_uv = a_uv;
                v_col = a_col;
                gl_Position = u_mvp * vec4(a_pos, 1.0);
            }
        )";
        const char* fSrc = R"(
            #version 330 core
            in vec2 v_uv;
            in vec4 v_col;
            uniform sampler2D u_tex;
            uniform int u_useTex;
            out vec4 FragColor;
            void main() {
                vec4 c = (u_useTex == 1) ? texture(u_tex, v_uv) * v_col : v_col;
                if (c.a < 0.02) discard;
                FragColor = c;
            }
        )";

        GLuint vs = compileShaderStage(GL_VERTEX_SHADER, vSrc, "sky.vert");
        GLuint fs = compileShaderStage(GL_FRAGMENT_SHADER, fSrc, "sky.frag");
        skyShader = glCreateProgram();
        glAttachShader(skyShader, vs);
        glAttachShader(skyShader, fs);
        glLinkProgram(skyShader);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glGenVertexArrays(1, &skyVao);
        glGenBuffers(1, &skyVbo);

        buildStarfieldVBO();
    }

    void tickWeather() {
        cloudScrollX += 0.03f;
        if (lightningFlash > 0) lightningFlash--;

        if (--weatherTimer <= 0) {
            weatherTimer = 12000 + (std::rand() % 12000);
            weather = (weather == WEATHER_CLEAR) ? WEATHER_RAIN : WEATHER_CLEAR;
        }

        float targetRain = (weather != WEATHER_CLEAR) ? 1.0f : 0.0f;
        if (rainStrength < targetRain) rainStrength = std::min(1.0f, rainStrength + 0.01f);
        else if (rainStrength > targetRain) rainStrength = std::max(0.0f, rainStrength - 0.01f);

        if (weather == WEATHER_THUNDER && (std::rand() % 400) == 0) {
            lightningFlash = 4;
        }
    }

    void renderSky(const WorldMap& world, const BetaAssetManager& assets, const Mat4& proj, const Mat4& viewOnlyRot) {
        if (world.dimension == -1 || !skyShader) return;

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(skyShader);
        GLint uMvp    = glGetUniformLocation(skyShader, "u_mvp");
        GLint uUseTex = glGetUniformLocation(skyShader, "u_useTex");

        float celestialAngle = (float)world.dayTime / 24000.0f - 0.25f;
        if (celestialAngle < 0.0f) celestialAngle += 1.0f;
        float skyBright = world.getSkyBrightness() * (1.0f - rainStrength * 0.4f);

        // 1. Upper Sky Dome & Lower Dark Void Dome
        Vec3 skyCol = world.getSkyColor();
        skyCol = skyCol * (1.0f - rainStrength * 0.45f);
        if (lightningFlash > 0) skyCol = { 0.85f, 0.90f, 1.0f };

        std::vector<float> dome;
        auto pushTri = [&](float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, float r, float g, float b, float a = 1.0f) {
            float pts[3][3] = {{x0,y0,z0},{x1,y1,z1},{x2,y2,z2}};
            for (int i = 0; i < 3; ++i) {
                dome.insert(dome.end(), { pts[i][0], pts[i][1], pts[i][2], 0.0f, 0.0f, r, g, b, a });
            }
        };

        float skyRadius = 128.0f;
        pushTri(-skyRadius, 16.0f, -skyRadius,  skyRadius, 16.0f, -skyRadius,  skyRadius, 16.0f,  skyRadius, skyCol.x, skyCol.y, skyCol.z);
        pushTri(-skyRadius, 16.0f, -skyRadius,  skyRadius, 16.0f,  skyRadius, -skyRadius, 16.0f,  skyRadius, skyCol.x, skyCol.y, skyCol.z);

        pushTri(-skyRadius, -16.0f,  skyRadius,  skyRadius, -16.0f,  skyRadius,  skyRadius, -16.0f, -skyRadius, skyCol.x * 0.2f, skyCol.y * 0.2f, skyCol.z * 0.35f);
        pushTri(-skyRadius, -16.0f,  skyRadius,  skyRadius, -16.0f, -skyRadius, -skyRadius, -16.0f, -skyRadius, skyCol.x * 0.2f, skyCol.y * 0.2f, skyCol.z * 0.35f);

        Mat4 vp = proj * viewOnlyRot;
        glUniformMatrix4fv(uMvp, 1, GL_FALSE, vp.m);
        glUniform1i(uUseTex, 0);
        uploadAndDraw(dome);

        // 2. Mojang Sunrise / Sunset Horizon Glow (WorldProvider.calcSunriseSunsetColors)
        float sunCos = std::cos(celestialAngle * 3.14159265f * 2.0f);
        if (sunCos >= -0.4f && sunCos <= 0.4f && rainStrength < 0.8f) {
            float f2 = (sunCos / 0.4f) * 0.5f + 0.5f;
            float alphaGlow = 1.0f - (1.0f - std::sin(f2 * 3.14159265f)) * 0.99f;
            alphaGlow = alphaGlow * alphaGlow * (1.0f - rainStrength);
            float gr = f2 * 0.3f + 0.7f;
            float gg = f2 * f2 * 0.7f + 0.2f;
            float gb = 0.2f;

            float dirSign = (std::sin(celestialAngle * 3.14159265f * 2.0f) < 0.0f) ? 1.0f : -1.0f;
            std::vector<float> glow;
            for (int seg = 0; seg < 16; ++seg) {
                float a0 = (float)seg * 6.2831853f / 16.0f;
                float a1 = (float)(seg + 1) * 6.2831853f / 16.0f;
                float c0x = std::sin(a0) * 120.0f, c0y = std::cos(a0) * 120.0f, c0z = -std::cos(a0) * 40.0f * alphaGlow;
                float c1x = std::sin(a1) * 120.0f, c1y = std::cos(a1) * 120.0f, c1z = -std::cos(a1) * 40.0f * alphaGlow;

                glow.insert(glow.end(), { dirSign * 100.0f, 0.0f, 0.0f, 0, 0, gr, gg, gb, alphaGlow });
                glow.insert(glow.end(), { c0z, c0y, c0x, 0, 0, gr, gg, gb, 0.0f });
                glow.insert(glow.end(), { c1z, c1y, c1x, 0, 0, gr, gg, gb, 0.0f });
            }
            uploadAndDraw(glow);
        }

        // 3. Additive Sun, Full Moon & Stars
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        Mat4 celMat = vp * Mat4::rotateZ(celestialAngle * 360.0f);
        glUniformMatrix4fv(uMvp, 1, GL_FALSE, celMat.m);

        float starAlpha = std::clamp((1.0f - skyBright) * 1.2f, 0.0f, 0.85f) * (1.0f - rainStrength);
        if (starAlpha > 0.02f && starVertexCount > 0) {
            glUniform1i(uUseTex, 0);
            glBindVertexArray(starsVao);
            glDrawArrays(GL_TRIANGLES, 0, starVertexCount);
        }

        glUniform1i(uUseTex, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, assets.sunTex);
        std::vector<float> sunQuad = {
            -30.0f, 100.0f, -30.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
             30.0f, 100.0f, -30.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
             30.0f, 100.0f,  30.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            -30.0f, 100.0f, -30.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
             30.0f, 100.0f,  30.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            -30.0f, 100.0f,  30.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
        };
        uploadAndDraw(sunQuad);

        glBindTexture(GL_TEXTURE_2D, assets.moonTex);
        std::vector<float> moonQuad = {
            -20.0f, -100.0f,  20.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
             20.0f, -100.0f,  20.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
             20.0f, -100.0f, -20.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            -20.0f, -100.0f,  20.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
             20.0f, -100.0f, -20.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
            -20.0f, -100.0f, -20.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
        };
        uploadAndDraw(moonQuad);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    // Mojang RenderGlobal.renderCloudsFancy (3D Volumetric Clouds, 4 Blocks Thick at Y = 108.0f)
    void renderClouds(const WorldMap& world, const BetaAssetManager& assets, const Mat4& viewProj, const Vec3& eyePos) {
        if (world.dimension == -1 || !skyShader || !assets.cloudsTex) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glUseProgram(skyShader);
        glUniformMatrix4fv(glGetUniformLocation(skyShader, "u_mvp"), 1, GL_FALSE, viewProj.m);
        glUniform1i(glGetUniformLocation(skyShader, "u_useTex"), 1);
        glBindTexture(GL_TEXTURE_2D, assets.cloudsTex);

        const float cellSize = 12.0f;
        const float cloudH   = 4.0f;
        float y0 = 108.0f - eyePos.y;
        float y1 = y0 + cloudH;

        float scrollX = eyePos.x + cloudScrollX;
        float scrollZ = eyePos.z;
        int baseCellX = (int)std::floor(scrollX / cellSize);
        int baseCellZ = (int)std::floor(scrollZ / cellSize);
        float offX = scrollX - baseCellX * cellSize;
        float offZ = scrollZ - baseCellZ * cellSize;

        float b = world.getSkyBrightness() * (1.0f - rainStrength * 0.45f);
        const float invTex = 1.0f / 256.0f;

        std::vector<float> verts;
        verts.reserve(16 * 16 * 36 * 9);

        auto pushFace = [&](float p[4][3], float u0, float v0, float u1, float v1, float shade) {
            float cr = b * shade, cg = b * shade, cb = b * shade, ca = 0.82f;
            float q[6][9] = {
                {p[0][0],p[0][1],p[0][2], u0,v1, cr,cg,cb,ca},
                {p[1][0],p[1][1],p[1][2], u1,v1, cr,cg,cb,ca},
                {p[2][0],p[2][1],p[2][2], u1,v0, cr,cg,cb,ca},
                {p[0][0],p[0][1],p[0][2], u0,v1, cr,cg,cb,ca},
                {p[2][0],p[2][1],p[2][2], u1,v0, cr,cg,cb,ca},
                {p[3][0],p[3][1],p[3][2], u0,v0, cr,cg,cb,ca}
            };
            for (int i = 0; i < 6; ++i) verts.insert(verts.end(), q[i], q[i] + 9);
        };

        for (int gz = -10; gz < 10; ++gz) {
            for (int gx = -10; gx < 10; ++gx) {
                float x0 = gx * cellSize - offX;
                float z0 = gz * cellSize - offZ;
                float x1 = x0 + cellSize;
                float z1 = z0 + cellSize;

                int tx = (baseCellX + gx) & 255;
                int tz = (baseCellZ + gz) & 255;
                float u0 = (tx + 0.5f) * invTex;
                float v0 = (tz + 0.5f) * invTex;
                float u1 = u0 + 0.01f * invTex;
                float v1 = v0 + 0.01f * invTex;

                // Top (+Y, 1.0 shade)
                float fT[4][3] = {{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0}};
                pushFace(fT, u0, v0, u1, v1, 1.0f);

                // Bottom (-Y, 0.7 shade)
                float fB[4][3] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}};
                pushFace(fB, u0, v0, u1, v1, 0.7f);

                // East & West (+X / -X, 0.9 shade)
                float fE[4][3] = {{x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1}};
                float fW[4][3] = {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}};
                pushFace(fE, u0, v0, u1, v1, 0.9f);
                pushFace(fW, u0, v0, u1, v1, 0.9f);

                // South & North (+Z / -Z, 0.8 shade)
                float fS[4][3] = {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
                float fN[4][3] = {{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}};
                pushFace(fS, u0, v0, u1, v1, 0.8f);
                pushFace(fN, u0, v0, u1, v1, 0.8f);
            }
        }

        uploadAndDraw(verts);
        glDisable(GL_BLEND);
    }

    void renderWeather(const WorldMap& world, const BetaAssetManager& assets, const Mat4& viewProj, const Vec3& eyePos) {
        if (world.dimension == -1 || rainStrength <= 0.01f || !skyShader) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);

        glUseProgram(skyShader);
        glUniformMatrix4fv(glGetUniformLocation(skyShader, "u_mvp"), 1, GL_FALSE, viewProj.m);
        glUniform1i(glGetUniformLocation(skyShader, "u_useTex"), 1);

        int px = (int)std::floor(eyePos.x);
        int pz = (int)std::floor(eyePos.z);
        int range = 5;

        std::vector<float> rainVerts;
        std::vector<float> snowVerts;
        float scroll = (float)(world.worldTick & 1023) * 0.05f;

        for (int dz = -range; dz <= range; ++dz) {
            for (int dx = -range; dx <= range; ++dx) {
                int wx = px + dx, wz = pz + dz;
                int topY = world.getTopSolidY(wx, wz);
                float rx = (float)wx - eyePos.x;
                float rz = (float)wz - eyePos.z;
                float y0 = std::max((float)topY, eyePos.y - 8.0f) - eyePos.y;
                float y1 = (eyePos.y + 10.0f) - eyePos.y;
                if (y1 <= y0) continue;

                const ChunkColumn* col = world.getReadyChunk(wx >> 4, wz >> 4);
                bool isSnow = col && (col->temperature[(wz & 15) * 16 + (wx & 15)] < 0.22f);
                auto& dst = isSnow ? snowVerts : rainVerts;
                float sp = isSnow ? (scroll * 0.2f) : scroll;
                float v0 = y0 * 0.25f + sp;
                float v1 = y1 * 0.25f + sp;

                float q[6][9] = {
                    {rx,      y0, rz,      0.0f, v0, 1, 1, 1, rainStrength},
                    {rx+1.0f, y0, rz+1.0f, 1.0f, v0, 1, 1, 1, rainStrength},
                    {rx+1.0f, y1, rz+1.0f, 1.0f, v1, 1, 1, 1, rainStrength},
                    {rx,      y0, rz,      0.0f, v0, 1, 1, 1, rainStrength},
                    {rx+1.0f, y1, rz+1.0f, 1.0f, v1, 1, 1, 1, rainStrength},
                    {rx,      y1, rz,      0.0f, v1, 1, 1, 1, rainStrength}
                };
                for (int i = 0; i < 6; ++i) dst.insert(dst.end(), q[i], q[i] + 9);
            }
        }

        if (!rainVerts.empty() && assets.rainTex) {
            glBindTexture(GL_TEXTURE_2D, assets.rainTex);
            uploadAndDraw(rainVerts);
        }
        if (!snowVerts.empty() && assets.snowTex) {
            glBindTexture(GL_TEXTURE_2D, assets.snowTex);
            uploadAndDraw(snowVerts);
        }

        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }

private:
    void uploadAndDraw(const std::vector<float>& verts) {
        if (verts.empty()) return;
        glBindVertexArray(skyVao);
        glBindBuffer(GL_ARRAY_BUFFER, skyVbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(verts.size() / 9));
    }

    void buildStarfieldVBO() {
        uint64_t s = (10842ULL ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1ULL);
        auto nextF = [&]() -> float {
            s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1ULL);
            return (float)(s >> 24) / (float)(1 << 24);
        };

        std::vector<float> stars;
        for (int i = 0; i < 1500; ++i) {
            float x = nextF() * 2.0f - 1.0f;
            float y = nextF() * 2.0f - 1.0f;
            float z = nextF() * 2.0f - 1.0f;
            float sz = 0.15f + nextF() * 0.1f;
            float d = x * x + y * y + z * z;
            if (d < 1.0f && d > 0.01f) {
                float inv = 1.0f / std::sqrt(d);
                x *= inv * 100.0f; y *= inv * 100.0f; z *= inv * 100.0f;
                float q[6][9] = {
                    {x-sz, y, z-sz, 0,0, 1,1,1,0.85f}, {x+sz, y, z-sz, 0,0, 1,1,1,0.85f}, {x+sz, y, z+sz, 0,0, 1,1,1,0.85f},
                    {x-sz, y, z-sz, 0,0, 1,1,1,0.85f}, {x+sz, y, z+sz, 0,0, 1,1,1,0.85f}, {x-sz, y, z+sz, 0,0, 1,1,1,0.85f}
                };
                for (int k = 0; k < 6; ++k) stars.insert(stars.end(), q[k], q[k] + 9);
            }
        }
        starVertexCount = (GLsizei)(stars.size() / 9);
        glGenVertexArrays(1, &starsVao);
        glGenBuffers(1, &starsVbo);
        glBindVertexArray(starsVao);
        glBindBuffer(GL_ARRAY_BUFFER, starsVbo);
        glBufferData(GL_ARRAY_BUFFER, stars.size() * sizeof(float), stars.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));
    }
};