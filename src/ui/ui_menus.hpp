#pragma once
#include "ui/ui_primitives.hpp"
#include "world/world_gen_b173.hpp"

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Classic Main Menu (GuiMainMenu.java 1:1 Fidelity)
// ============================================================================
// - Renders official title/mclogo.png from b1.7.3.jar (two slices at y=30)
// - Pulsing Yellow Splash Text rotated -20 degrees (Mojang exact formula)
// - Exact Button Positions, Dirt Tiled Background (32x32, 0x404040 tint),
//   and Top-Left Version + Bottom-Right Copyright Footer
// ============================================================================

enum AppMenuState : uint8_t {
    MENU_TITLE        = 0,
    MENU_WORLD_SELECT = 1,
    MENU_CREATE_WORLD = 2,
    MENU_LOADING      = 3,
    MENU_OPTIONS      = 4,
    MENU_IN_GAME      = 5
};

struct BetaGameSettings {
    float mouseSensitivity = 0.50f;
    int   renderDistance   = 8;
    bool  smoothLighting   = true;
    int   difficulty       = 2;
    int   guiScale         = 0;
    float soundVolume      = 1.0f;
};

class BetaMenuSystem {
public:
    AppMenuState                   state         = MENU_TITLE;
    AppMenuState                   returnState   = MENU_TITLE;
    std::vector<SavedWorldSummary> savedWorlds;
    int                            selectedWorld = 0;

    std::string                    newWorldName  = "New World";
    std::string                    newWorldSeed  = "";
    int                            focusedField  = 0;

    int                            splashIdx     = 0;
    float                          loadingProg   = 0.0f;

    static int64_t javaStringHashCode(const std::string& s) {
        int32_t h = 0;
        for (unsigned char c : s) h = 31 * h + (int32_t)c;
        return (int64_t)h;
    }

    void refreshWorlds(const BetaAssetManager& assets) {
        savedWorlds = Beta173WorldGenerator::listSavedWorlds("saves");
        if (!assets.splashTexts.empty()) {
            splashIdx = (int)(std::time(nullptr) % assets.splashTexts.size());
        }
    }

    // Mojang Gui.drawBackground(0): 32x32 tiled dirt at 0x404040 (0.25) brightness
    static void drawTiledDirtBackground(BetaUiRenderer& ui, const BetaAssetManager& assets) {
        if (!assets.terrainTex) {
            ui.drawSolidRect(0, 0, (float)ui.scaledW, (float)ui.scaledH, 0xFF281C14);
            return;
        }
        float tile = 32.0f;
        float u0 = (SPR_DIRT % 16) * 16.0f, v0 = (SPR_DIRT / 16) * 16.0f;
        for (float y = 0; y < ui.scaledH; y += tile) {
            for (float x = 0; x < ui.scaledW; x += tile) {
                ui.drawTexturedRect(assets.terrainTex, x, y, tile, tile, u0, v0, u0 + 16.0f, v0 + 16.0f,
                                    256.0f, 256.0f, 0.25f, 0.25f, 0.25f, 1.0f);
            }
        }
    }

    // Mojang GuiMainMenu.java: draws official title/mclogo.png (274x44 split into two 155x44 slices)
    static void drawBetaTitleLogo(BetaUiRenderer& ui, const BetaAssetManager& assets, float cx, float topY) {
        if (assets.mcLogoTex) {
            float logoX = std::floor(cx - 137.0f);
            ui.drawTexturedRect(assets.mcLogoTex, logoX,          topY, 155.0f, 44.0f, 0.0f, 0.0f,  155.0f, 44.0f);
            ui.drawTexturedRect(assets.mcLogoTex, logoX + 155.0f, topY, 155.0f, 44.0f, 0.0f, 45.0f, 155.0f, 89.0f);
            return;
        }

        // Clean stone-block fallback only if b1.7.3.jar is missing
        static const char* logoRows[5] = {
            "*   * * *   * *****  ***  ****   ***  ***** *****",
            "** ** * **  * *     *   * *   * *   * *       *  ",
            "* * * * * * * ****  *     ****  ***** ****    *  ",
            "*   * * *  ** *     *   * *  *  *   * *       *  ",
            "*   * * *   * *****  ***  *   * *   * *       *  "
        };
        const float blk = 5.0f;
        float startX = std::floor(cx - (49.0f * blk) * 0.5f);
        float uStone = (SPR_STONE % 16) * 16.0f, vStone = (SPR_STONE / 16) * 16.0f;
        for (int r = 0; r < 5; ++r) {
            for (int c = 0; c < 49; ++c) {
                if (logoRows[r][c] != '*') continue;
                float bx = startX + c * blk, by = topY + r * blk;
                ui.drawTexturedRect(assets.terrainTex, bx + 1.5f, by + 1.5f, blk, blk, uStone, vStone, uStone + 16.0f, vStone + 16.0f, 256, 256, 0.25f, 0.25f, 0.25f, 1.0f);
                ui.drawTexturedRect(assets.terrainTex, bx, by, blk, blk, uStone, vStone, uStone + 16.0f, vStone + 16.0f, 256, 256, 0.95f, 0.95f, 0.95f, 1.0f);
            }
        }
    }

    // Mojang GuiMainMenu.java rotated (-20 deg) & pulsing yellow Splash Text
    static void drawPulsingSplashText(BetaUiRenderer& ui, const BetaAssetManager& assets,
                                      const std::string& splash, float anchorX, float anchorY) {
        if (splash.empty() || !assets.fontTex) return;

        ULONGLONG ms = GetTickCount64() % 1000ULL;
        float pulse = 1.8f - std::abs(std::sin((float)ms / 1000.0f * 3.14159265f * 2.0f) * 0.1f);
        float textW = (float)ui.getStringWidth(assets, splash);
        float scale = (pulse * 100.0f) / (textW + 32.0f);

        Mat4 xf = Mat4::translate(anchorX, anchorY, 0.0f) *
                  Mat4::rotateZ(-20.0f) *
                  Mat4::scale(scale, scale, 1.0f);

        auto xform2D = [&](float lx, float ly) -> std::pair<float, float> {
            return {
                xf.m[0] * lx + xf.m[4] * ly + xf.m[12],
                xf.m[1] * lx + xf.m[5] * ly + xf.m[13]
            };
        };

        auto drawTransformedPass = [&](float offX, float offY, float r, float g, float b) {
            std::vector<UiVertex> verts;
            verts.reserve(splash.size() * 6);
            float curX = -textW * 0.5f + offX;
            float curY = -4.0f + offY;

            for (size_t i = 0; i < splash.size(); ++i) {
                uint8_t c = (uint8_t)splash[i];
                float u0 = (c % 16) / 16.0f, v0 = (c / 16) / 16.0f;
                float u1 = u0 + 1.0f / 16.0f, v1 = v0 + 1.0f / 16.0f;

                auto p0 = xform2D(curX,        curY);
                auto p1 = xform2D(curX + 8.0f, curY);
                auto p2 = xform2D(curX + 8.0f, curY + 8.0f);
                auto p3 = xform2D(curX,        curY + 8.0f);

                verts.push_back({p0.first, p0.second, u0, v0, r, g, b, 1.0f});
                verts.push_back({p1.first, p1.second, u1, v0, r, g, b, 1.0f});
                verts.push_back({p2.first, p2.second, u1, v1, r, g, b, 1.0f});
                verts.push_back({p0.first, p0.second, u0, v0, r, g, b, 1.0f});
                verts.push_back({p2.first, p2.second, u1, v1, r, g, b, 1.0f});
                verts.push_back({p3.first, p3.second, u0, v1, r, g, b, 1.0f});

                curX += (float)assets.charWidths[c];
            }

            glUniform1i(glGetUniformLocation(ui.shader, "u_useTex"), 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, assets.fontTex);
            glBindVertexArray(ui.vao);
            glBindBuffer(GL_ARRAY_BUFFER, ui.vbo);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(UiVertex), verts.data(), GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, x));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, u));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, r));
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
        };

        drawTransformedPass(1.0f, 1.0f, 0.25f, 0.25f, 0.0f); // Mojang 0x3F3F00 shadow
        drawTransformedPass(0.0f, 0.0f, 1.0f,  1.0f,  0.0f); // Mojang 0xFFFF00 yellow
    }

    bool renderMenus(BetaUiRenderer& ui, const BetaAssetManager& assets, BetaGameSettings& settings,
                     const std::string& typedChars, bool backspacePressed,
                     int mouseX, int mouseY, bool clicked, bool& outQuitRequested,
                     std::string& outTargetSaveDir, std::string& outTargetWorldName, int64_t& outTargetSeed) {
        float cx = ui.scaledW * 0.5f;

        if (state == MENU_TITLE) {
            drawTiledDirtBackground(ui, assets);

            // Top-Left Version Label (Exact Mojang GuiMainMenu.java: 2, 2, 0x505050)
            ui.drawString(assets, "Minecraft Beta 1.7.3 C++", 2.0f, 2.0f, 0xFF505050, false);

            // Official title/mclogo.png at y = 30
            drawBetaTitleLogo(ui, assets, cx, 30.0f);

            // Pulsing Yellow Splash Text rotated -20 deg at (cx + 90, 70)
            if (!assets.splashTexts.empty()) {
                const std::string& splash = assets.splashTexts[(size_t)splashIdx % assets.splashTexts.size()];
                drawPulsingSplashText(ui, assets, splash, cx + 90.0f, 70.0f);
            }

            // Exact Mojang GuiMainMenu button layout (scaledH / 4 + 48)
            float btnY = std::floor(ui.scaledH * 0.25f + 48.0f);
            if (ui.drawClassicButton(assets, "Singleplayer", cx - 100.0f, btnY, 200.0f, 20.0f, mouseX, mouseY, clicked)) {
                refreshWorlds(assets);
                state = MENU_WORLD_SELECT;
            }
            ui.drawClassicButton(assets, "Multiplayer", cx - 100.0f, btnY + 24.0f, 200.0f, 20.0f, mouseX, mouseY, clicked, false);
            ui.drawClassicButton(assets, "Mods and Texture Packs", cx - 100.0f, btnY + 48.0f, 200.0f, 20.0f, mouseX, mouseY, clicked, false);

            if (ui.drawClassicButton(assets, "Options...", cx - 100.0f, btnY + 84.0f, 98.0f, 20.0f, mouseX, mouseY, clicked)) {
                returnState = MENU_TITLE;
                state = MENU_OPTIONS;
            }
            if (ui.drawClassicButton(assets, "Quit Game", cx + 2.0f, btnY + 84.0f, 98.0f, 20.0f, mouseX, mouseY, clicked)) {
                outQuitRequested = true;
            }

            // Bottom-Right Mojang Copyright Notice
            const std::string copyStr = "Copyright Mojang AB. Do not distribute!";
            float copyW = (float)ui.getStringWidth(assets, copyStr);
            ui.drawString(assets, copyStr, (float)ui.scaledW - copyW - 2.0f, (float)ui.scaledH - 10.0f, 0xFFFFFFFF);
            return false;
        }

        if (state == MENU_WORLD_SELECT) {
            drawTiledDirtBackground(ui, assets);
            ui.drawCenteredString(assets, "Select World", cx, 16.0f, 0xFFFFFFFF);

            ui.drawSolidRect(cx - 110.0f, 32.0f, cx + 110.0f, (float)ui.scaledH - 60.0f, 0xAA000000);
            for (size_t i = 0; i < savedWorlds.size() && i < 6; ++i) {
                float wy = 36.0f + i * 32.0f;
                bool sel = ((int)i == selectedWorld);
                if (sel) {
                    ui.drawSolidRect(cx - 106.0f, wy - 2.0f, cx + 106.0f, wy + 28.0f, 0xFF808080);
                    ui.drawSolidRect(cx - 105.0f, wy - 1.0f, cx + 105.0f, wy + 27.0f, 0xFF000000);
                }
                ui.drawString(assets, savedWorlds[i].levelData.levelName, cx - 100.0f, wy + 2.0f, 0xFFFFFFFF);
                char seedStr[64];
                std::snprintf(seedStr, sizeof(seedStr), "%s (Seed: %lld)", savedWorlds[i].folderName.c_str(), (long long)savedWorlds[i].levelData.seed);
                ui.drawString(assets, seedStr, cx - 100.0f, wy + 14.0f, 0xFF808080);

                if (clicked && mouseX >= cx - 106 && mouseX <= cx + 106 && mouseY >= wy && mouseY <= wy + 28) {
                    selectedWorld = (int)i;
                }
            }

            float botY = (float)ui.scaledH - 52.0f;
            bool hasWorlds = !savedWorlds.empty() && selectedWorld >= 0 && selectedWorld < (int)savedWorlds.size();
            if (ui.drawClassicButton(assets, "Play Selected World", cx - 154.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked, hasWorlds)) {
                const auto& w = savedWorlds[(size_t)selectedWorld];
                outTargetSaveDir   = w.fullPath;
                outTargetWorldName = w.levelData.levelName;
                outTargetSeed      = w.levelData.seed;
                state = MENU_LOADING;
                return true;
            }
            if (ui.drawClassicButton(assets, "Create New World", cx + 4.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                newWorldName = "New World";
                newWorldSeed = "";
                focusedField = 0;
                state = MENU_CREATE_WORLD;
            }
            if (ui.drawClassicButton(assets, "Cancel", cx - 75.0f, botY + 24.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                state = MENU_TITLE;
            }
            return false;
        }

        if (state == MENU_CREATE_WORLD) {
            drawTiledDirtBackground(ui, assets);
            ui.drawCenteredString(assets, "Create New World", cx, 20.0f, 0xFFFFFFFF);

            std::string& activeStr = (focusedField == 0) ? newWorldName : newWorldSeed;
            if (backspacePressed && !activeStr.empty()) activeStr.pop_back();
            for (char c : typedChars) {
                if (activeStr.size() < 28 && c != '/' && c != '\\' && c != ':') activeStr.push_back(c);
            }

            ui.drawString(assets, "World Name", cx - 100.0f, 48.0f, 0xFFA0A0A0);
            ui.drawSolidRect(cx - 100.0f, 60.0f, cx + 100.0f, 80.0f, focusedField == 0 ? 0xFFFFFFFF : 0xFFA0A0A0);
            ui.drawSolidRect(cx - 99.0f,  61.0f, cx + 99.0f,  79.0f, 0xFF000000);
            ui.drawString(assets, newWorldName + (focusedField == 0 ? "_" : ""), cx - 95.0f, 66.0f, 0xFFE0E0E0);
            if (clicked && mouseX >= cx - 100 && mouseX <= cx + 100 && mouseY >= 60 && mouseY <= 80) focusedField = 0;

            ui.drawString(assets, "Seed for the World Generator", cx - 100.0f, 94.0f, 0xFFA0A0A0);
            ui.drawSolidRect(cx - 100.0f, 106.0f, cx + 100.0f, 126.0f, focusedField == 1 ? 0xFFFFFFFF : 0xFFA0A0A0);
            ui.drawSolidRect(cx - 99.0f,  107.0f, cx + 99.0f,  125.0f, 0xFF000000);
            ui.drawString(assets, newWorldSeed + (focusedField == 1 ? "_" : ""), cx - 95.0f, 112.0f, 0xFFE0E0E0);
            ui.drawString(assets, "Leave blank for a random seed", cx - 100.0f, 130.0f, 0xFFA0A0A0);
            if (clicked && mouseX >= cx - 100 && mouseX <= cx + 100 && mouseY >= 106 && mouseY <= 126) focusedField = 1;

            float botY = (float)ui.scaledH - 52.0f;
            if (ui.drawClassicButton(assets, "Create New World", cx - 154.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                std::string cleanName = newWorldName.empty() ? "New World" : newWorldName;
                int64_t seedVal = 0;
                if (newWorldSeed.empty()) {
                    seedVal = (int64_t)std::time(nullptr) * 1103515245LL + 12345LL;
                } else {
                    char* endPtr = nullptr;
                    long long parsed = std::strtoll(newWorldSeed.c_str(), &endPtr, 10);
                    seedVal = (endPtr && *endPtr == '\0') ? (int64_t)parsed : javaStringHashCode(newWorldSeed);
                }
                outTargetWorldName = cleanName;
                outTargetSaveDir   = "saves/" + cleanName;
                outTargetSeed      = seedVal;
                state = MENU_LOADING;
                return true;
            }
            if (ui.drawClassicButton(assets, "Cancel", cx + 4.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                state = MENU_WORLD_SELECT;
            }
            return false;
        }

        if (state == MENU_LOADING) {
            drawTiledDirtBackground(ui, assets);
            ui.drawCenteredString(assets, "Generating level", cx, ui.scaledH * 0.5f - 24.0f, 0xFFFFFFFF);
            ui.drawCenteredString(assets, "Building terrain", cx, ui.scaledH * 0.5f - 8.0f, 0xFFAAAAAA);
            ui.drawSolidRect(cx - 50.0f, ui.scaledH * 0.5f + 8.0f, cx + 50.0f, ui.scaledH * 0.5f + 10.0f, 0xFF808080);
            ui.drawSolidRect(cx - 50.0f, ui.scaledH * 0.5f + 8.0f, cx - 50.0f + loadingProg * 100.0f, ui.scaledH * 0.5f + 10.0f, 0xFF80FF80);
            return false;
        }

        if (state == MENU_OPTIONS) {
            if (returnState == MENU_TITLE) drawTiledDirtBackground(ui, assets);
            else ui.drawGradientRect(0, 0, (float)ui.scaledW, (float)ui.scaledH, 0xC0101010, 0xD0101010);

            ui.drawCenteredString(assets, "Options", cx, 20.0f, 0xFFFFFFFF);

            char buf[64];
            const char* distNames[4] = { "TINY", "SHORT", "NORMAL", "FAR" };
            int distIdx = (settings.renderDistance <= 4) ? 0 : ((settings.renderDistance <= 6) ? 1 : ((settings.renderDistance <= 8) ? 2 : 3));
            std::snprintf(buf, sizeof(buf), "Render Distance: %s", distNames[distIdx]);
            if (ui.drawClassicButton(assets, buf, cx - 155.0f, ui.scaledH / 6.0f + 24.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.renderDistance += 2;
                if (settings.renderDistance > 12) settings.renderDistance = 4;
            }

            const char* diffNames[4] = { "Peaceful", "Easy", "Normal", "Hard" };
            std::snprintf(buf, sizeof(buf), "Difficulty: %s", diffNames[settings.difficulty & 3]);
            if (ui.drawClassicButton(assets, buf, cx + 5.0f, ui.scaledH / 6.0f + 24.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.difficulty = (settings.difficulty + 1) & 3;
            }

            std::snprintf(buf, sizeof(buf), "Smooth Lighting: %s", settings.smoothLighting ? "ON" : "OFF");
            if (ui.drawClassicButton(assets, buf, cx - 155.0f, ui.scaledH / 6.0f + 48.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.smoothLighting = !settings.smoothLighting;
            }

            const char* scaleNames[4] = { "Auto", "Small", "Normal", "Large" };
            std::snprintf(buf, sizeof(buf), "GUI Scale: %s", scaleNames[settings.guiScale & 3]);
            if (ui.drawClassicButton(assets, buf, cx + 5.0f, ui.scaledH / 6.0f + 48.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.guiScale = (settings.guiScale + 1) & 3;
            }

            std::snprintf(buf, sizeof(buf), "Sensitivity: %d%%", (int)(settings.mouseSensitivity * 200.0f));
            if (ui.drawClassicButton(assets, buf, cx - 155.0f, ui.scaledH / 6.0f + 72.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.mouseSensitivity += 0.125f;
                if (settings.mouseSensitivity > 1.0f) settings.mouseSensitivity = 0.25f;
            }

            if (ui.drawClassicButton(assets, "Done", cx - 100.0f, ui.scaledH / 6.0f + 168.0f, 200.0f, 20.0f, mouseX, mouseY, clicked)) {
                state = returnState;
            }
            return false;
        }
        return false;
    }
};