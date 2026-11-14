#pragma once
#include "platform.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
// Sky, 3D Volumetric Clouds (Y=108), Stars & Weather (1:1)
// ============================================================================
//   - WorldProvider.calculateCelestialAngle(long worldTime, float partialTicks):
//       * Exact non-linear solar/lunar curve:
//           f1 = (worldTime % 24000) / 24000.0F - 0.25F
//           f2 = f1; f1 = 0.5F - cos(f1 * PI) / 2.0F; return f2 + (f1 - f2) / 3.0F
//   - World.getSkyColor() & World.getStarBrightness():
//       * Exact 2*PI cosine brightness curve (noon = 1.0, midnight = 0.0)
//       * Sunrise / Sunset horizon glow fan (WorldProvider.calcSunriseSunsetColors)
//       * 1,500 procedural stars with dynamic night fade
//   - RenderGlobal.renderCloudsFancy():
//       * Reads environment/clouds.png alpha mask (256x256) at Y = 108.0F,
//         4 blocks thick, 12x12 block cells, with neighbor-culled shaded faces
//   - EntityRenderer.renderRainSnow():
//       * Per-column deterministic phase offsets, heightMap clipping, and
//         diagonal snow drift
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

    // Cached 256x256 cloud alpha mask sampled from assets.cloudsTex
    std::vector<uint8_t> cloudMask;
    GLuint               cachedCloudTexId = 0;

    // ExactWorldProvider.calculateCelestialAngle(worldTime, partialTicks)
    static float calculateCelestialAngle(uint64_t worldTime, float partialTicks = 0.0f) {
        int i = static_cast<int>(worldTime % 24000ULL);
        float f1 = (static_cast<float>(i) + partialTicks) / 24000.0f - 0.25f;
        if (f1 < 0.0f) f1 += 1.0f;
        if (f1 > 1.0f) f1 -= 1.0f;
        float f2 = f1;
        f1 = 0.5f - std::cos(f1 * 3.1415927f) / 2.0f;
        return f2 + (f1 - f2) / 3.0f;
    }

    // ExactWorld.getSkyColor brightness multiplier [0.0 at midnight .. 1.0 at noon]
    static float calculateSkyLightFactor(float celestialAngle) {
        float f2 = std::cos(celestialAngle * 3.1415927f * 2.0f) * 2.0f + 0.5f;
        return std::clamp(f2, 0.0f, 1.0f);
    }

    // ExactWorld.getStarBrightness(partialTicks)
    static float calculateStarBrightness(float celestialAngle) {
        float f2 = 1.0f - (std::cos(celestialAngle * 3.1415927f * 2.0f) * 2.0f + 0.75f);
        f2 = std::clamp(f2, 0.0f, 1.0f);
        return f2 * f2 * 0.85f;
    }

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
            uniform vec4 u_colorMod;
            out vec4 FragColor;
            void main() {
                vec4 base = (u_useTex == 1) ? texture(u_tex, v_uv) * v_col : v_col;
                vec4 c = base * u_colorMod;
                if (c.a < 0.01) discard;
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
        GLint uMvp      = glGetUniformLocation(skyShader, "u_mvp");
        GLint uUseTex   = glGetUniformLocation(skyShader, "u_useTex");
        GLint uColorMod = glGetUniformLocation(skyShader, "u_colorMod");
        glUniform4f(uColorMod, 1.0f, 1.0f, 1.0f, 1.0f);

        // Exactcelestial angle & day/night sky color curve
        float celestialAngle = calculateCelestialAngle(world.dayTime, 0.0f);
        float skyFactor      = calculateSkyLightFactor(celestialAngle);

        float skyR = 0.53f * skyFactor;
        float skyG = 0.72f * skyFactor;
        float skyB = 1.00f * skyFactor;

        if (rainStrength > 0.0f) {
            float lum = (skyR * 0.30f + skyG * 0.59f + skyB * 0.11f) * 0.60f;
            float f4  = 1.0f - rainStrength * 0.75f;
            skyR = skyR * f4 + lum * (1.0f - f4);
            skyG = skyG * f4 + lum * (1.0f - f4);
            skyB = skyB * f4 + lum * (1.0f - f4);
        }
        if (lightningFlash > 0) {
            skyR = 0.85f;
            skyG = 0.90f;
            skyB = 1.00f;
        }

        // 1. Upper Sky Dome & Lower Dark Void Hemisphere (RenderGlobal.renderSky)
        std::vector<float> dome;
        dome.reserve(12 * 9);
        auto pushTri = [&](float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2,
                           float r, float g, float b, float a = 1.0f) {
            float pts[3][3] = { {x0,y0,z0}, {x1,y1,z1}, {x2,y2,z2} };
            for (int i = 0; i < 3; ++i) {
                dome.insert(dome.end(), { pts[i][0], pts[i][1], pts[i][2], 0.0f, 0.0f, r, g, b, a });
            }
        };

        const float skyRadius = 128.0f;
        pushTri(-skyRadius, 16.0f, -skyRadius,  skyRadius, 16.0f, -skyRadius,  skyRadius, 16.0f,  skyRadius, skyR, skyG, skyB);
        pushTri(-skyRadius, 16.0f, -skyRadius,  skyRadius, 16.0f,  skyRadius, -skyRadius, 16.0f,  skyRadius, skyR, skyG, skyB);

        // Lower void hemisphere (Mojang darkens void below horizon to 0.2x + subtle night blue)
        float voidR = skyR * 0.2f + 0.04f * skyFactor;
        float voidG = skyG * 0.2f + 0.04f * skyFactor;
        float voidB = skyB * 0.6f + 0.10f * skyFactor;
        pushTri(-skyRadius, -16.0f,  skyRadius,  skyRadius, -16.0f,  skyRadius,  skyRadius, -16.0f, -skyRadius, voidR, voidG, voidB);
        pushTri(-skyRadius, -16.0f,  skyRadius,  skyRadius, -16.0f, -skyRadius, -skyRadius, -16.0f, -skyRadius, voidR, voidG, voidB);

        Mat4 vp = proj * viewOnlyRot;
        glUniformMatrix4fv(uMvp, 1, GL_FALSE, vp.m);
        glUniform1i(uUseTex, 0);
        uploadAndDraw(dome);

        // 2.Sunrise / Sunset Horizon Glow Fan (WorldProvider.calcSunriseSunsetColors)
        float sunCos = std::cos(celestialAngle * 3.1415927f * 2.0f);
        if (sunCos >= -0.4f && sunCos <= 0.4f && rainStrength < 0.8f) {
            float f2 = (sunCos / 0.4f) * 0.5f + 0.5f;
            float alphaGlow = 1.0f - (1.0f - std::sin(f2 * 3.1415927f)) * 0.99f;
            alphaGlow = alphaGlow * alphaGlow * (1.0f - rainStrength);
            float gr = f2 * 0.3f + 0.7f;
            float gg = f2 * f2 * 0.7f + 0.2f;
            float gb = 0.2f;

            // Sunrise at East (+X, sin < 0), Sunset at West (-X, sin > 0)
            float dirSign = (std::sin(celestialAngle * 3.1415927f * 2.0f) < 0.0f) ? 1.0f : -1.0f;
            std::vector<float> glow;
            glow.reserve(16 * 3 * 9);
            for (int seg = 0; seg < 16; ++seg) {
                float a0 = static_cast<float>(seg) * 6.2831853f / 16.0f;
                float a1 = static_cast<float>(seg + 1) * 6.2831853f / 16.0f;
                float c0x = std::sin(a0) * 120.0f, c0y = std::cos(a0) * 120.0f, c0z = -std::cos(a0) * 40.0f * alphaGlow;
                float c1x = std::sin(a1) * 120.0f, c1y = std::cos(a1) * 120.0f, c1z = -std::cos(a1) * 40.0f * alphaGlow;

                glow.insert(glow.end(), { dirSign * 100.0f, 0.0f, 0.0f, 0, 0, gr, gg, gb, alphaGlow });
                glow.insert(glow.end(), { c0z, c0y, c0x, 0, 0, gr, gg, gb, 0.0f });
                glow.insert(glow.end(), { c1z, c1y, c1x, 0, 0, gr, gg, gb, 0.0f });
            }
            uploadAndDraw(glow);
        }

        // 3. Additive Stars, Sun & Full Moon (RenderGlobal.renderSky)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        Mat4 celMat = vp * Mat4::rotateZ(celestialAngle * 360.0f);
        glUniformMatrix4fv(uMvp, 1, GL_FALSE, celMat.m);

        // Stars fade in smoothly at dusk and shine during night
        float starBright = calculateStarBrightness(celestialAngle) * (1.0f - rainStrength);
        if (starBright > 0.01f && starVertexCount > 0) {
            glUniform1i(uUseTex, 0);
            glUniform4f(uColorMod, starBright, starBright, starBright, starBright);
            glBindVertexArray(starsVao);
            glDrawArrays(GL_TRIANGLES, 0, starVertexCount);
        }

        glUniform4f(uColorMod, 1.0f, 1.0f, 1.0f, 1.0f);
        glUniform1i(uUseTex, 1);
        glActiveTexture(GL_TEXTURE0);

        // Sun (+Y = +100 at noon)
        if (assets.sunTex) {
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
        }

        // Full Moon (-Y = -100 at noon -> +Y = +100 at midnight)
        if (assets.moonTex) {
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
        }

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    //RenderGlobal.renderCloudsFancy (3D Volumetric Clouds at Y = 108.0F, 4 Blocks Thick)
    void renderClouds(const WorldMap& world, const BetaAssetManager& assets, const Mat4& viewProj, const Vec3& eyePos) {
        if (world.dimension == -1 || !skyShader || !assets.cloudsTex) return;

        ensureCloudMaskLoaded(assets.cloudsTex);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glUseProgram(skyShader);
        glUniformMatrix4fv(glGetUniformLocation(skyShader, "u_mvp"), 1, GL_FALSE, viewProj.m);
        glUniform1i(glGetUniformLocation(skyShader, "u_useTex"), 0);
        glUniform4f(glGetUniformLocation(skyShader, "u_colorMod"), 1.0f, 1.0f, 1.0f, 1.0f);

        float celestialAngle = calculateCelestialAngle(world.dayTime, 0.0f);
        float skyFactor      = calculateSkyLightFactor(celestialAngle);
        float b = (skyFactor * 0.90f + 0.10f) * (1.0f - rainStrength * 0.45f);
        if (lightningFlash > 0) b = 1.0f;

        const float cellSize = 12.0f;
        const float cloudH   = 4.0f;
        float y0 = 108.0f - eyePos.y;
        float y1 = y0 + cloudH;

        float scrollX = eyePos.x + cloudScrollX;
        float scrollZ = eyePos.z;
        int baseCellX = static_cast<int>(std::floor(scrollX / cellSize));
        int baseCellZ = static_cast<int>(std::floor(scrollZ / cellSize));
        float offX = scrollX - static_cast<float>(baseCellX) * cellSize;
        float offZ = scrollZ - static_cast<float>(baseCellZ) * cellSize;

        auto hasCloudAt = [&](int cx, int cz) -> bool {
            int tx = cx & 255;
            int tz = cz & 255;
            return cloudMask[static_cast<size_t>(tz * 256 + tx)] != 0;
        };

        std::vector<float> verts;
        verts.reserve(16 * 16 * 24 * 9);

        auto pushFace = [&](float p[4][3], float shade) {
            float cr = b * shade, cg = b * shade, cb = b * shade, ca = 0.80f;
            float q[6][9] = {
                {p[0][0],p[0][1],p[0][2], 0,0, cr,cg,cb,ca},
                {p[1][0],p[1][1],p[1][2], 0,0, cr,cg,cb,ca},
                {p[2][0],p[2][1],p[2][2], 0,0, cr,cg,cb,ca},
                {p[0][0],p[0][1],p[0][2], 0,0, cr,cg,cb,ca},
                {p[2][0],p[2][1],p[2][2], 0,0, cr,cg,cb,ca},
                {p[3][0],p[3][1],p[3][2], 0,0, cr,cg,cb,ca}
            };
            for (int i = 0; i < 6; ++i) verts.insert(verts.end(), q[i], q[i] + 9);
        };

        constexpr int RANGE = 14;
        for (int gz = -RANGE; gz < RANGE; ++gz) {
            for (int gx = -RANGE; gx < RANGE; ++gx) {
                int cellX = baseCellX + gx;
                int cellZ = baseCellZ + gz;
                if (!hasCloudAt(cellX, cellZ)) continue;

                float x0 = static_cast<float>(gx) * cellSize - offX;
                float z0 = static_cast<float>(gz) * cellSize - offZ;
                float x1 = x0 + cellSize;
                float z1 = z0 + cellSize;

                // Top (+Y, 1.0 shade)
                float fT[4][3] = { {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0} };
                pushFace(fT, 1.0f);

                // Bottom (-Y, 0.7 shade)
                float fB[4][3] = { {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1} };
                pushFace(fB, 0.7f);

                // Side faces culled against adjacent cloud cells (exact Notch RenderGlobal.renderCloudsFancy)
                if (!hasCloudAt(cellX + 1, cellZ)) {
                    float fE[4][3] = { {x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1} };
                    pushFace(fE, 0.9f);
                }
                if (!hasCloudAt(cellX - 1, cellZ)) {
                    float fW[4][3] = { {x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0} };
                    pushFace(fW, 0.9f);
                }
                if (!hasCloudAt(cellX, cellZ + 1)) {
                    float fS[4][3] = { {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1} };
                    pushFace(fS, 0.8f);
                }
                if (!hasCloudAt(cellX, cellZ - 1)) {
                    float fN[4][3] = { {x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0} };
                    pushFace(fN, 0.8f);
                }
            }
        }

        uploadAndDraw(verts);
        glDisable(GL_BLEND);
    }

    //EntityRenderer.renderRainSnow(float partialTicks)
    void renderWeather(const WorldMap& world, const BetaAssetManager& assets, const Mat4& viewProj, const Vec3& eyePos) {
        if (world.dimension == -1 || rainStrength <= 0.01f || !skyShader) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);

        glUseProgram(skyShader);
        glUniformMatrix4fv(glGetUniformLocation(skyShader, "u_mvp"), 1, GL_FALSE, viewProj.m);
        glUniform1i(glGetUniformLocation(skyShader, "u_useTex"), 1);
        glUniform4f(glGetUniformLocation(skyShader, "u_colorMod"), 1.0f, 1.0f, 1.0f, 1.0f);

        int px = static_cast<int>(std::floor(eyePos.x));
        int pz = static_cast<int>(std::floor(eyePos.z));
        constexpr int RANGE = 8;

        std::vector<float> rainVerts;
        std::vector<float> snowVerts;
        rainVerts.reserve(RANGE * RANGE * 24 * 9);
        snowVerts.reserve(RANGE * RANGE * 24 * 9);

        float tickF = static_cast<float>(world.worldTick & 4095ULL);

        for (int dz = -RANGE; dz <= RANGE; ++dz) {
            for (int dx = -RANGE; dx <= RANGE; ++dx) {
                int wx = px + dx;
                int wz = pz + dz;
                int topY = world.getTopSolidY(wx, wz);

                float yMin = std::max(static_cast<float>(topY), eyePos.y - 8.0f) - eyePos.y;
                float yMax = (eyePos.y + 10.0f) - eyePos.y;
                if (yMax <= yMin) continue;

                // ExactEntityRenderer per-column deterministic hash
                uint32_t colHash = static_cast<uint32_t>(wx * wx * 3121 + wx * 45238971) ^
                                   static_cast<uint32_t>(wz * wz * 418711 + wz * 13761);
                float phase = static_cast<float>(colHash & 31u) / 32.0f;

                const ChunkColumn* col = world.getReadyChunk(wx >> 4, wz >> 4);
                bool isSnow = col && (col->temperature[(wz & 15) * 16 + (wx & 15)] < 0.22f);

                float rx = static_cast<float>(wx) - eyePos.x;
                float rz = static_cast<float>(wz) - eyePos.z;
                float distNorm = std::sqrt(static_cast<float>(dx * dx + dz * dz)) / static_cast<float>(RANGE);
                float alpha = ((1.0f - distNorm * distNorm) * 0.5f + 0.5f) * rainStrength;
                if (alpha <= 0.01f) continue;

                if (isSnow) {
                    float vScroll = tickF * 0.01f + phase;
                    float uDrift  = std::sin(tickF * 0.04f + phase * 6.2831853f) * 0.15f;
                    float v0 = yMin * 0.25f + vScroll;
                    float v1 = yMax * 0.25f + vScroll;

                    float q[6][9] = {
                        {rx,      yMin, rz,      0.0f + uDrift, v0, 1, 1, 1, alpha},
                        {rx+1.0f, yMin, rz+1.0f, 1.0f + uDrift, v0, 1, 1, 1, alpha},
                        {rx+1.0f, yMax, rz+1.0f, 1.0f + uDrift, v1, 1, 1, 1, alpha},
                        {rx,      yMin, rz,      0.0f + uDrift, v0, 1, 1, 1, alpha},
                        {rx+1.0f, yMax, rz+1.0f, 1.0f + uDrift, v1, 1, 1, 1, alpha},
                        {rx,      yMax, rz,      0.0f + uDrift, v1, 1, 1, 1, alpha}
                    };
                    for (int i = 0; i < 6; ++i) snowVerts.insert(snowVerts.end(), q[i], q[i] + 9);
                } else {
                    float vScroll = static_cast<float>((world.worldTick + (colHash & 31u)) & 31u) / 32.0f;
                    float v0 = yMin * 0.25f + vScroll;
                    float v1 = yMax * 0.25f + vScroll;

                    float q[6][9] = {
                        {rx,      yMin, rz,      0.0f, v0, 1, 1, 1, alpha},
                        {rx+1.0f, yMin, rz+1.0f, 1.0f, v0, 1, 1, 1, alpha},
                        {rx+1.0f, yMax, rz+1.0f, 1.0f, v1, 1, 1, 1, alpha},
                        {rx,      yMin, rz,      0.0f, v0, 1, 1, 1, alpha},
                        {rx+1.0f, yMax, rz+1.0f, 1.0f, v1, 1, 1, 1, alpha},
                        {rx,      yMax, rz,      0.0f, v1, 1, 1, 1, alpha}
                    };
                    for (int i = 0; i < 6; ++i) rainVerts.insert(rainVerts.end(), q[i], q[i] + 9);
                }
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
    void ensureCloudMaskLoaded(GLuint texId) {
        if (cachedCloudTexId == texId && cloudMask.size() == 256 * 256) return;
        cachedCloudTexId = texId;
        cloudMask.assign(256 * 256, 0);

        glBindTexture(GL_TEXTURE_2D, texId);
        GLint w = 0, h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

        if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
            std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

            // Check if texture is a genuine clouds.png (has transparent gaps) vs solid fallback
            bool hasTransparency = false;
            for (size_t i = 3; i < rgba.size(); i += 4) {
                if (rgba[i] < 128) {
                    hasTransparency = true;
                    break;
                }
            }

            if (hasTransparency) {
                for (int z = 0; z < 256; ++z) {
                    int sz = (z * h) / 256;
                    for (int x = 0; x < 256; ++x) {
                        int sx = (x * w) / 256;
                        uint8_t alpha = rgba[(static_cast<size_t>(sz) * static_cast<size_t>(w) + static_cast<size_t>(sx)) * 4 + 3];
                        cloudMask[static_cast<size_t>(z * 256 + x)] = (alpha > 128) ? 1 : 0;
                    }
                }
                return;
            }
        }

        // Clean procedural Beta-style cloud pattern if b1.7.3.jar was missing
        for (int z = 0; z < 256; ++z) {
            for (int x = 0; x < 256; ++x) {
                float v = std::sin(x * 0.098f) * std::cos(z * 0.098f) +
                          std::sin((x + z) * 0.049f) * 0.7f;
                cloudMask[static_cast<size_t>(z * 256 + x)] = (v > 0.45f) ? 1 : 0;
            }
        }
    }

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
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 9));
    }

    void buildStarfieldVBO() {
        uint64_t s = (10842ULL ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1ULL);
        auto nextF = [&]() -> float {
            s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1ULL);
            return static_cast<float>(s >> 24) / static_cast<float>(1 << 24);
        };

        std::vector<float> stars;
        stars.reserve(1500 * 6 * 9);
        for (int i = 0; i < 1500; ++i) {
            float x = nextF() * 2.0f - 1.0f;
            float y = nextF() * 2.0f - 1.0f;
            float z = nextF() * 2.0f - 1.0f;
            float sz = 0.15f + nextF() * 0.1f;
            float d = x * x + y * y + z * z;
            if (d < 1.0f && d > 0.01f) {
                float inv = 1.0f / std::sqrt(d);
                x *= inv * 100.0f;
                y *= inv * 100.0f;
                z *= inv * 100.0f;
                float q[6][9] = {
                    {x-sz, y, z-sz, 0,0, 1,1,1,1.0f}, {x+sz, y, z-sz, 0,0, 1,1,1,1.0f}, {x+sz, y, z+sz, 0,0, 1,1,1,1.0f},
                    {x-sz, y, z-sz, 0,0, 1,1,1,1.0f}, {x+sz, y, z+sz, 0,0, 1,1,1,1.0f}, {x-sz, y, z+sz, 0,0, 1,1,1,1.0f}
                };
                for (int k = 0; k < 6; ++k) stars.insert(stars.end(), q[k], q[k] + 9);
            }
        }
        starVertexCount = static_cast<GLsizei>(stars.size() / 9);
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