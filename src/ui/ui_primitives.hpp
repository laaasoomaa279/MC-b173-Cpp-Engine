#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "assets/recipes_b173.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 2D UI Primitives (Gui.java & FontRenderer.java 1:1)
// ============================================================================

struct UiVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

class BetaUiRenderer {
public:
    GLuint shader = 0;
    GLuint vao    = 0;
    GLuint vbo    = 0;

    int    screenW  = 1280;
    int    screenH  = 720;
    int    guiScale = 2;
    int    scaledW  = 640;
    int    scaledH  = 360;

    void init() {
        const char* vSrc = R"(
            #version 330 core
            layout(location = 0) in vec2 a_pos;
            layout(location = 1) in vec2 a_uv;
            layout(location = 2) in vec4 a_col;
            uniform mat4 u_ortho;
            out vec2 v_uv;
            out vec4 v_col;
            void main() {
                v_uv = a_uv;
                v_col = a_col;
                gl_Position = u_ortho * vec4(a_pos, 0.0, 1.0);
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
                if (c.a < 0.01) discard;
                FragColor = c;
            }
        )";

        GLuint vs = compileShaderStage(GL_VERTEX_SHADER, vSrc, "ui.vert");
        GLuint fs = compileShaderStage(GL_FRAGMENT_SHADER, fSrc, "ui.frag");
        shader = glCreateProgram();
        glAttachShader(shader, vs);
        glAttachShader(shader, fs);
        glLinkProgram(shader);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
    }

    void beginFrame(int winW, int winH, int scaleSetting = 0) {
        screenW = std::max(1, winW);
        screenH = std::max(1, winH);

        if (scaleSetting > 0) {
            guiScale = std::clamp(scaleSetting, 1, 4);
        } else {
            // Exact Mojang ScaledResolution.java Auto GUI Scale
            guiScale = 1;
            while (guiScale < 4 && (screenW / (guiScale + 1)) >= 320 && (screenH / (guiScale + 1)) >= 240) {
                guiScale++;
            }
        }
        scaledW = screenW / guiScale;
        scaledH = screenH / guiScale;

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(shader);
        Mat4 ortho = Mat4::ortho(0.0f, (float)scaledW, (float)scaledH, 0.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shader, "u_ortho"), 1, GL_FALSE, ortho.m);
    }

    void endFrame() {
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    // Mojang Gui.drawRect(int left, int top, int right, int bottom, int color)
    void drawSolidRect(float x0, float y0, float x1, float y1, uint32_t argb) {
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);

        float a = ((argb >> 24) & 0xFF) / 255.0f;
        float r = ((argb >> 16) & 0xFF) / 255.0f;
        float g = ((argb >> 8)  & 0xFF) / 255.0f;
        float b = (argb & 0xFF) / 255.0f;

        glUniform1i(glGetUniformLocation(shader, "u_useTex"), 0);
        UiVertex verts[6] = {
            {x0, y0, 0, 0, r, g, b, a}, {x1, y0, 0, 0, r, g, b, a}, {x1, y1, 0, 0, r, g, b, a},
            {x0, y0, 0, 0, r, g, b, a}, {x1, y1, 0, 0, r, g, b, a}, {x0, y1, 0, 0, r, g, b, a}
        };
        submitVertices(verts, 6);
    }

    // Mojang Gui.drawGradientRect(int left, int top, int right, int bottom, int startColor, int endColor)
    void drawGradientRect(float x0, float y0, float x1, float y1, uint32_t topArgb, uint32_t botArgb) {
        float a0 = ((topArgb >> 24) & 0xFF) / 255.0f, r0 = ((topArgb >> 16) & 0xFF) / 255.0f;
        float g0 = ((topArgb >> 8)  & 0xFF) / 255.0f, b0 = (topArgb & 0xFF) / 255.0f;

        float a1 = ((botArgb >> 24) & 0xFF) / 255.0f, r1 = ((botArgb >> 16) & 0xFF) / 255.0f;
        float g1 = ((botArgb >> 8)  & 0xFF) / 255.0f, b1 = (botArgb & 0xFF) / 255.0f;

        glUniform1i(glGetUniformLocation(shader, "u_useTex"), 0);
        UiVertex verts[6] = {
            {x0, y0, 0, 0, r0, g0, b0, a0}, {x1, y0, 0, 0, r0, g0, b0, a0}, {x1, y1, 0, 0, r1, g1, b1, a1},
            {x0, y0, 0, 0, r0, g0, b0, a0}, {x1, y1, 0, 0, r1, g1, b1, a1}, {x0, y1, 0, 0, r1, g1, b1, a1}
        };
        submitVertices(verts, 6);
    }

    // Mojang Gui.drawTexturedModalRect
    void drawTexturedRect(GLuint tex, float x, float y, float w, float h,
                          float u0Px, float v0Px, float u1Px, float v1Px,
                          float texW = 256.0f, float texH = 256.0f,
                          float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
        if (!tex) return;
        glUniform1i(glGetUniformLocation(shader, "u_useTex"), 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);

        float u0 = u0Px / texW, v0 = v0Px / texH;
        float u1 = u1Px / texW, v1 = v1Px / texH;
        UiVertex verts[6] = {
            {x,     y,     u0, v0, r, g, b, a}, {x + w, y,     u1, v0, r, g, b, a}, {x + w, y + h, u1, v1, r, g, b, a},
            {x,     y,     u0, v0, r, g, b, a}, {x + w, y + h, u1, v1, r, g, b, a}, {x,     y + h, u0, v1, r, g, b, a}
        };
        submitVertices(verts, 6);
    }

    int getStringWidth(const BetaAssetManager& assets, const std::string& text) const {
        int w = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            uint8_t c = (uint8_t)text[i];
            if (c == 0xC2 && i + 1 < text.size() && (uint8_t)text[i + 1] == 0xA7) { i += 2; continue; }
            if (c == 0xA7 && i + 1 < text.size()) { i += 1; continue; }
            w += assets.charWidths[c];
        }
        return w;
    }

    // Mojang FontRenderer.drawStringWithShadow
    void drawString(const BetaAssetManager& assets, const std::string& text, float x, float y,
                    uint32_t color = 0xFFFFFFFF, bool shadow = true) {
        if (text.empty() || !assets.fontTex) return;
        if (shadow) {
            drawStringPass(assets, text, x + 1.0f, y + 1.0f, color, true);
        }
        drawStringPass(assets, text, x, y, color, false);
    }

    void drawCenteredString(const BetaAssetManager& assets, const std::string& text, float cx, float y,
                            uint32_t color = 0xFFFFFFFF, bool shadow = true) {
        float w = (float)getStringWidth(assets, text);
        drawString(assets, text, std::floor(cx - w * 0.5f), y, color, shadow);
    }

    // Mojang GuiButton.drawButton (0 = Disabled, 1 = Normal, 2 = Hovered)
    bool drawClassicButton(const BetaAssetManager& assets, const std::string& label,
                           float x, float y, float w, float h,
                           int mouseX, int mouseY, bool clicked, bool enabled = true) {
        bool hovered = enabled && (mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h);
        int stateRow = !enabled ? 0 : (hovered ? 2 : 1);
        float vTop = 46.0f + stateRow * 20.0f;

        if (assets.guiTex) {
            float halfW = w * 0.5f;
            drawTexturedRect(assets.guiTex, x,         y, halfW, h, 0.0f,           vTop, halfW,  vTop + 20.0f);
            drawTexturedRect(assets.guiTex, x + halfW, y, halfW, h, 200.0f - halfW, vTop, 200.0f, vTop + 20.0f);
        } else {
            drawSolidRect(x, y, x + w, y + h, hovered ? 0xFF5A6FA8 : 0xFF4A4A4A);
        }

        uint32_t txtCol = !enabled ? 0xFFA0A0A0 : (hovered ? 0xFFFFFFA0 : 0xFFE0E0E0);
        drawCenteredString(assets, label, x + w * 0.5f, y + (h - 8.0f) * 0.5f, txtCol, true);
        return hovered && clicked;
    }

    // Mojang RenderItem.renderItemIntoGUI + renderItemOverlayIntoGUI
    void drawItemStack(const WorldMap& world, const BetaAssetManager& assets,
                       const ItemStack& st, float x, float y) {
        if (st.isEmpty()) return;

        if (st.id < 256) {
            uint16_t rawSt = (uint16_t)((st.id << 4) | (st.damage & 0x0F));
            const BlockDef& def = world.registry.get(rawSt);

            if (def.shape == SHAPE_CUBE || def.shape == SHAPE_SLAB || def.shape == SHAPE_STAIR || def.shape == SHAPE_PISTON) {
                drawIsometricBlockIcon(world, assets, rawSt, x, y);
            } else {
                uint16_t spr = world.registry.resolveFaceTexture(rawSt, FACE_NORTH);
                float u = (spr % 16) * 16.0f, v = (spr / 16) * 16.0f;
                float tr = 1.0f, tg = 1.0f, tb = 1.0f;
                if (def.tintMode == TINT_GRASS || def.tintMode == TINT_FOLIAGE) {
                    assets.sampleBiomeColor(0.7f, 0.7f, def.tintMode == TINT_FOLIAGE, tr, tg, tb);
                }
                drawTexturedRect(assets.terrainTex, x, y, 16.0f, 16.0f, u, v, u + 16.0f, v + 16.0f, 256.0f, 256.0f, tr, tg, tb, 1.0f);
            }
        } else {
            uint16_t spr = assets.getItemDef(st.id).spriteIdx;
            float u = (spr % 16) * 16.0f, v = (spr / 16) * 16.0f;
            drawTexturedRect(assets.itemsTex, x, y, 16.0f, 16.0f, u, v, u + 16.0f, v + 16.0f);
        }

        // Mojang Exact Tool/Armor Durability Bar
        if (st.id >= 256 && st.damage > 0) {
            uint16_t maxD = assets.getItemDef(st.id).maxDamage;
            if (maxD > 0) {
                int j = (int)std::round(13.0 - (double)st.damage * 13.0 / (double)maxD);
                int k = (int)std::round(255.0 - (double)st.damage * 255.0 / (double)maxD);
                drawSolidRect(x + 2.0f, y + 13.0f, x + 15.0f, y + 15.0f, 0xFF000000);
                uint32_t bgCol = 0xFF000000u | ((uint32_t)((255 - k) / 4) << 16) | (0x3F00u);
                drawSolidRect(x + 2.0f, y + 13.0f, x + 14.0f, y + 14.0f, bgCol);
                uint32_t fgCol = 0xFF000000u | ((uint32_t)(255 - k) << 16) | ((uint32_t)k << 8);
                drawSolidRect(x + 2.0f, y + 13.0f, x + 2.0f + (float)std::max(0, j), y + 14.0f, fgCol);
            }
        }

        // Stack Count (Exact Mojang offset: x + 19 - 2 - width, y + 6 + 3)
        if (st.count > 1) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", (int)st.count);
            float w = (float)getStringWidth(assets, buf);
            drawString(assets, buf, x + 17.0f - w, y + 9.0f, 0xFFFFFFFF, true);
        }
    }

    // Mojang GuiContainer Classic Purple-Bordered Tooltip
    void drawTooltip(const BetaAssetManager& assets, const std::string& text, float mx, float my) {
        if (text.empty()) return;
        float w = (float)getStringWidth(assets, text);
        float x = mx + 12.0f;
        float y = my - 12.0f;
        if (x + w + 6.0f > scaledW) x = mx - w - 12.0f;
        if (y < 4.0f) y = 4.0f;

        drawGradientRect(x - 3.0f, y - 3.0f, x + w + 3.0f, y + 11.0f, 0xC0100010, 0xC0100010);
        drawString(assets, text, x, y, 0xFFFFFFFF, true);
    }

    // Authentic Mojang 2D Paper-Doll Player Preview inside Inventory Black Box (GuiInventory.java)
    void drawPaperDollPlayer(const BetaAssetManager& assets, float centerX, float feetY, int mouseX, int mouseY) {
        if (!assets.mobSkins[SKIN_PLAYER]) return;

        float lookX = std::clamp((mouseX - centerX) / 40.0f, -1.0f, 1.0f);
        float lookY = std::clamp((mouseY - (feetY - 46.0f)) / 40.0f, -1.0f, 1.0f);
        float headShiftX = lookX * 2.5f;
        float headShiftY = lookY * 1.5f;

        // Legs (UV 4,20..8,32 on 64x32 char.png)
        drawTexturedRect(assets.mobSkins[SKIN_PLAYER], centerX - 8.0f, feetY - 24.0f, 8.0f, 24.0f, 4, 20, 8, 32, 64, 32);
        drawTexturedRect(assets.mobSkins[SKIN_PLAYER], centerX,        feetY - 24.0f, 8.0f, 24.0f, 8, 20, 4, 32, 64, 32);

        // Torso (UV 20,20..28,32)
        drawTexturedRect(assets.mobSkins[SKIN_PLAYER], centerX - 8.0f, feetY - 48.0f, 16.0f, 24.0f, 20, 20, 28, 32, 64, 32);

        // Arms (UV 44,20..48,32)
        drawTexturedRect(assets.mobSkins[SKIN_PLAYER], centerX - 15.0f, feetY - 48.0f, 7.0f, 24.0f, 44, 20, 48, 32, 64, 32);
        drawTexturedRect(assets.mobSkins[SKIN_PLAYER], centerX + 8.0f,  feetY - 48.0f, 7.0f, 24.0f, 48, 20, 44, 32, 64, 32);

        // Head (UV 8,8..16,16) + Outer Hat Layer (UV 40,8..48,16)
        float hx = centerX - 8.0f + headShiftX;
        float hy = feetY - 64.0f + headShiftY;
        drawTexturedRect(assets.mobSkins[SKIN_PLAYER], hx, hy, 16.0f, 16.0f, 8, 8, 16, 16, 64, 32);
        drawTexturedRect(assets.mobSkins[SKIN_PLAYER], hx - 0.5f, hy - 0.5f, 17.0f, 17.0f, 40, 8, 48, 16, 64, 32);
    }

private:
    void drawStringPass(const BetaAssetManager& assets, const std::string& text, float x, float y,
                        uint32_t baseColor, bool isShadowPass) {
        static constexpr uint32_t mcPalette[16] = {
            0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xFFAA00, 0xAAAAAA,
            0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
        };

        auto applyShadow = [&](uint32_t col) -> uint32_t {
            if (!isShadowPass) return col;
            uint32_t rgb = (col & 0xFCFCFCu) >> 2;
            return (col & 0xFF000000u) | rgb;
        };

        uint32_t activeCol = applyShadow(baseColor);
        float a = ((activeCol >> 24) & 0xFF) / 255.0f;
        if (a <= 0.01f) a = 1.0f;
        float r = ((activeCol >> 16) & 0xFF) / 255.0f;
        float g = ((activeCol >> 8)  & 0xFF) / 255.0f;
        float b = (activeCol & 0xFF) / 255.0f;

        std::vector<UiVertex> batch;
        batch.reserve(text.size() * 6);
        float curX = x;

        for (size_t i = 0; i < text.size(); ++i) {
            uint8_t c = (uint8_t)text[i];
            if (c == 0xC2 && i + 1 < text.size() && (uint8_t)text[i + 1] == 0xA7) { i++; c = 0xA7; }
            if (c == 0xA7 && i + 1 < text.size()) {
                char code = text[++i];
                int idx = -1;
                if (code >= '0' && code <= '9') idx = code - '0';
                else if (code >= 'a' && code <= 'f') idx = 10 + (code - 'a');
                else if (code >= 'A' && code <= 'F') idx = 10 + (code - 'A');
                if (idx >= 0) {
                    uint32_t pal = applyShadow(0xFF000000u | mcPalette[idx]);
                    r = ((pal >> 16) & 0xFF) / 255.0f;
                    g = ((pal >> 8)  & 0xFF) / 255.0f;
                    b = (pal & 0xFF) / 255.0f;
                }
                continue;
            }

            float u0 = (c % 16) / 16.0f;
            float v0 = (c / 16) / 16.0f;
            float u1 = u0 + 1.0f / 16.0f;
            float v1 = v0 + 1.0f / 16.0f;

            batch.push_back({curX,        y,        u0, v0, r, g, b, a});
            batch.push_back({curX + 8.0f, y,        u1, v0, r, g, b, a});
            batch.push_back({curX + 8.0f, y + 8.0f, u1, v1, r, g, b, a});
            batch.push_back({curX,        y,        u0, v0, r, g, b, a});
            batch.push_back({curX + 8.0f, y + 8.0f, u1, v1, r, g, b, a});
            batch.push_back({curX,        y + 8.0f, u0, v1, r, g, b, a});

            curX += (float)assets.charWidths[c];
        }

        if (!batch.empty()) {
            glUniform1i(glGetUniformLocation(shader, "u_useTex"), 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, assets.fontTex);
            submitVertices(batch.data(), batch.size());
        }
    }

    // Classic 3-Face Isometric Block Icon (Zero Texture Bleeding via 0.25-texel inset)
    void drawIsometricBlockIcon(const WorldMap& world, const BetaAssetManager& assets,
                                uint16_t rawSt, float x, float y) {
        if (!assets.terrainTex) return;
        uint16_t topSpr   = world.registry.resolveFaceTexture(rawSt, FACE_TOP);
        uint16_t leftSpr  = world.registry.resolveFaceTexture(rawSt, FACE_SOUTH);
        uint16_t rightSpr = world.registry.resolveFaceTexture(rawSt, FACE_EAST);

        const BlockDef& def = world.registry.get(rawSt);
        float tr = 1.0f, tg = 1.0f, tb = 1.0f;
        if (def.tintMode == TINT_GRASS || def.tintMode == TINT_FOLIAGE) {
            assets.sampleBiomeColor(0.7f, 0.7f, def.tintMode == TINT_FOLIAGE, tr, tg, tb);
        }

        float cx = x + 8.0f, cy = y + 8.0f;
        const float eps = 0.001f; // Half-texel safety inset on 256x256 atlas
        auto sprUV = [eps](uint16_t s, float& u0, float& v0, float& u1, float& v1) {
            u0 = (s % 16) / 16.0f + eps;
            v0 = (s / 16) / 16.0f + eps;
            u1 = (s % 16 + 1) / 16.0f - eps;
            v1 = (s / 16 + 1) / 16.0f - eps;
        };

        float topOffset = (def.shape == SHAPE_SLAB) ? 0.0f : -7.0f;
        float midOffset = (def.shape == SHAPE_SLAB) ? 3.5f : -3.5f;
        float ctrOffset = (def.shape == SHAPE_SLAB) ? 3.75f : 0.0f;

        float u0, v0, u1, v1;
        std::vector<UiVertex> tris;
        tris.reserve(18);

        // Top face (100% brightness)
        sprUV(topSpr, u0, v0, u1, v1);
        tris.push_back({cx,        cy + topOffset, u0, v0, tr, tg, tb, 1});
        tris.push_back({cx + 7.0f, cy + midOffset, u1, v0, tr, tg, tb, 1});
        tris.push_back({cx,        cy + ctrOffset, u1, v1, tr, tg, tb, 1});
        tris.push_back({cx,        cy + topOffset, u0, v0, tr, tg, tb, 1});
        tris.push_back({cx,        cy + ctrOffset, u1, v1, tr, tg, tb, 1});
        tris.push_back({cx - 7.0f, cy + midOffset, u0, v1, tr, tg, tb, 1});

        // Left face (80% brightness)
        float lr = (def.tintMode == TINT_FOLIAGE) ? tr * 0.8f : 0.8f;
        float lg = (def.tintMode == TINT_FOLIAGE) ? tg * 0.8f : 0.8f;
        float lb = (def.tintMode == TINT_FOLIAGE) ? tb * 0.8f : 0.8f;
        sprUV(leftSpr, u0, v0, u1, v1);
        tris.push_back({cx - 7.0f, cy + midOffset, u0, v0, lr, lg, lb, 1});
        tris.push_back({cx,        cy + ctrOffset, u1, v0, lr, lg, lb, 1});
        tris.push_back({cx,        cy + 7.5f,      u1, v1, lr, lg, lb, 1});
        tris.push_back({cx - 7.0f, cy + midOffset, u0, v0, lr, lg, lb, 1});
        tris.push_back({cx,        cy + 7.5f,      u1, v1, lr, lg, lb, 1});
        tris.push_back({cx - 7.0f, cy + 4.0f,      u0, v1, lr, lg, lb, 1});

        // Right face (60% brightness)
        float rr = (def.tintMode == TINT_FOLIAGE) ? tr * 0.6f : 0.6f;
        float rg = (def.tintMode == TINT_FOLIAGE) ? tg * 0.6f : 0.6f;
        float rb = (def.tintMode == TINT_FOLIAGE) ? tb * 0.6f : 0.6f;
        sprUV(rightSpr, u0, v0, u1, v1);
        tris.push_back({cx,        cy + ctrOffset, u0, v0, rr, rg, rb, 1});
        tris.push_back({cx + 7.0f, cy + midOffset, u1, v0, rr, rg, rb, 1});
        tris.push_back({cx + 7.0f, cy + 4.0f,      u1, v1, rr, rg, rb, 1});
        tris.push_back({cx,        cy + ctrOffset, u0, v0, rr, rg, rb, 1});
        tris.push_back({cx + 7.0f, cy + 4.0f,      u1, v1, rr, rg, rb, 1});
        tris.push_back({cx,        cy + 7.5f,      u0, v1, rr, rg, rb, 1});

        glUniform1i(glGetUniformLocation(shader, "u_useTex"), 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, assets.terrainTex);
        submitVertices(tris.data(), tris.size());
    }

    void submitVertices(const UiVertex* verts, size_t count) {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, count * sizeof(UiVertex), verts, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, u));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, r));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)count);
    }
};