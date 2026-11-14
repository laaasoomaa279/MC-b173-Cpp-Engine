#pragma once
#include "ui/ui_primitives.hpp"
#include "world/world_gen_b173.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>


//       * Official title/mclogo.png (two 155x44 slices at y=30)
//       * Pulsing yellow splash text rotated -20 degrees (exact trig formula)
//       * Top-left version string (2, 2, 0x505050) & bottom-right copyright
//       * Scrollable world slot list sorted by LastPlayed timestamp, showing
//         LevelName, folder name + Seed, Play Selected World, Create New World,
//         Rename, Delete (with GuiYesNo confirmation), and Cancel.
//       * Automatic unique folder name generation ("New World", "New World-",
//         "New World--") + Windows reserved filename sanitization so every new
//         world gets its own clean directory and exact user-entered Seed!
//       * Safe overflow-free Java String.hashCode() & 64-bit integer parser +
//         one-click classic Beta 1.7.3 Seed presets (gargamel, 404, Glacier).
//       * Exact 2-columnoptions layout with authentic sensitivity
//         labels ("*yawn*" .. "HYPERSPEED!!!"), Render Distance, Difficulty,
//         Smooth Lighting, GUI Scale, and Sound Volume.
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

    // Sub-screen & scroll state inside World Select
    int                            worldScrollOffset = 0;
    bool                           confirmDeleteMode = false;
    bool                           renameWorldMode   = false;
    std::string                    renameBuffer      = "";

    // Exact JVM java.lang.String.hashCode() using unsigned arithmetic (zero UB in Clang -O3)
    static int64_t javaStringHashCode(const std::string& s) {
        uint32_t h = 0;
        for (unsigned char c : s) {
            h = 31u * h + static_cast<uint32_t>(c);
        }
        return static_cast<int64_t>(static_cast<int32_t>(h));
    }

    // ExactGuiCreateWorld.actionPerformed seed parser
    static int64_t parseBeta173Seed(const std::string& rawSeedInput) {
        // Trim leading/trailing ASCII whitespace
        size_t first = 0;
        while (first < rawSeedInput.size() && rawSeedInput[first] == ' ') first++;
        size_t last = rawSeedInput.size();
        while (last > first && rawSeedInput[last - 1] == ' ') last--;
        std::string trimmed = rawSeedInput.substr(first, last - first);

        if (trimmed.empty()) {
            uint64_t t = static_cast<uint64_t>(std::time(nullptr));
            uint64_t mixed = (t * 6364136223846793005ULL + 1442695040888963407ULL) ^ (t >> 17);
            return static_cast<int64_t>(mixed);
        }

        // Check if string is a valid signed 64-bit decimal integer (excluding "0" -> hashCode in Mojang!)
        char* endPtr = nullptr;
        errno = 0;
        long long parsed = std::strtoll(trimmed.c_str(), &endPtr, 10);
        if (errno == 0 && endPtr != trimmed.c_str() && *endPtr == '\0') {
            int64_t val = static_cast<int64_t>(parsed);
            if (val != 0LL) {
                return val;
            }
        }
        return javaStringHashCode(trimmed);
    }

    // ExactSaveFormatOld.makeUsableName() + GuiCreateWorld.func_25097_a()
    static std::string makeUniqueWorldFolder(const std::string& savesRoot, const std::string& desiredName) {
        std::string base = desiredName;
        // Trim leading/trailing spaces & dots
        while (!base.empty() && (base.front() == ' ' || base.front() == '.')) base.erase(base.begin());
        while (!base.empty() && (base.back()  == ' ' || base.back()  == '.')) base.pop_back();

        // Replace Windows illegal filename characters with '_'
        const char* illegalChars = "/\\:*?\"<>|\n\r\t\0";
        for (char& ch : base) {
            for (int i = 0; illegalChars[i] != '\0'; ++i) {
                if (ch == illegalChars[i]) {
                    ch = '_';
                    break;
                }
            }
        }
        if (base.empty()) base = "World";

        // Prefix Windows reserved DOS device names with '_' (exactSaveFormatOld list)
        static const char* reservedNames[] = {
            "CON", "COM", "PRN", "AUX", "CLOCK$", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };
        std::string upperBase = base;
        for (char& ch : upperBase) {
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        }
        for (const char* res : reservedNames) {
            if (upperBase == res) {
                base = "_" + base + "_";
                break;
            }
        }

        // Append '-' until folder does not exist in savesRoot (exact Beta 1.7.3 behavior!)
        std::error_code ec;
        std::filesystem::create_directories(savesRoot, ec);
        std::string candidate = base;
        while (std::filesystem::exists(savesRoot + "/" + candidate, ec)) {
            candidate += "-";
        }
        return candidate;
    }

    void refreshWorlds(const BetaAssetManager& assets) {
        savedWorlds = Beta173WorldGenerator::listSavedWorlds("saves");
        // Sort worlds by LastPlayed descending (most recently played at top, exact GuiSelectWorld)
        std::sort(savedWorlds.begin(), savedWorlds.end(), [](const SavedWorldSummary& a, const SavedWorldSummary& b) {
            return a.levelData.lastPlayed > b.levelData.lastPlayed;
        });

        if (savedWorlds.empty()) {
            selectedWorld     = 0;
            worldScrollOffset = 0;
        } else {
            selectedWorld     = std::clamp(selectedWorld, 0, static_cast<int>(savedWorlds.size()) - 1);
            worldScrollOffset = std::clamp(worldScrollOffset, 0, std::max(0, static_cast<int>(savedWorlds.size()) - 4));
        }

        confirmDeleteMode = false;
        renameWorldMode   = false;

        if (!assets.splashTexts.empty()) {
            splashIdx = static_cast<int>(static_cast<uint64_t>(std::time(nullptr)) % assets.splashTexts.size());
        }
    }

    //Gui.drawBackground(0): 32x32 tiled dirt at 0x404040 (0.25) brightness
    static void drawTiledDirtBackground(BetaUiRenderer& ui, const BetaAssetManager& assets) {
        if (!assets.terrainTex) {
            ui.drawSolidRect(0, 0, static_cast<float>(ui.scaledW), static_cast<float>(ui.scaledH), 0xFF281C14);
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

    //GuiMainMenu.java: draws official title/mclogo.png (two 155x44 slices at y=30)
    static void drawBetaTitleLogo(BetaUiRenderer& ui, const BetaAssetManager& assets, float cx, float topY) {
        if (assets.mcLogoTex) {
            float logoX = std::floor(cx - 137.0f);
            ui.drawTexturedRect(assets.mcLogoTex, logoX,          topY, 155.0f, 44.0f, 0.0f, 0.0f,  155.0f, 44.0f);
            ui.drawTexturedRect(assets.mcLogoTex, logoX + 155.0f, topY, 155.0f, 44.0f, 0.0f, 45.0f, 155.0f, 89.0f);
            return;
        }

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

    //GuiMainMenu.java rotated (-20 deg) & pulsing yellow Splash Text
    static void drawPulsingSplashText(BetaUiRenderer& ui, const BetaAssetManager& assets,
                                      const std::string& splash, float anchorX, float anchorY) {
        if (splash.empty() || !assets.fontTex) return;

        ULONGLONG ms = GetTickCount64() % 1000ULL;
        float pulse = 1.8f - std::abs(std::sin(static_cast<float>(ms) / 1000.0f * 3.14159265f * 2.0f) * 0.1f);
        float textW = static_cast<float>(ui.getStringWidth(assets, splash));
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
                uint8_t c = static_cast<uint8_t>(splash[i]);
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

                curX += static_cast<float>(assets.charWidths[c]);
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
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
        };

        drawTransformedPass(1.0f, 1.0f, 0.25f, 0.25f, 0.0f); //0x3F3F00 shadow
        drawTransformedPass(0.0f, 0.0f, 1.0f,  1.0f,  0.0f); //0xFFFF00 yellow
    }

    bool renderMenus(BetaUiRenderer& ui, const BetaAssetManager& assets, BetaGameSettings& settings,
                     const std::string& typedChars, bool backspacePressed,
                     int mouseX, int mouseY, bool clicked, bool& outQuitRequested,
                     std::string& outTargetSaveDir, std::string& outTargetWorldName, int64_t& outTargetSeed) {
        float cx = ui.scaledW * 0.5f;

        // ====================================================================
        // 1. GuiMainMenu (Title Screen)
        // ====================================================================
        if (state == MENU_TITLE) {
            drawTiledDirtBackground(ui, assets);

            // Top-Left Version Label (ExactGuiMainMenu.java: 2, 2, 0x505050)
            ui.drawString(assets, "Minecraft Beta 1.7.3", 2.0f, 2.0f, 0xFF505050, false);

            // Official title/mclogo.png at y = 30
            drawBetaTitleLogo(ui, assets, cx, 30.0f);

            // Pulsing Yellow Splash Text rotated -20 deg at (cx + 90, 70)
            if (!assets.splashTexts.empty()) {
                const std::string& splash = assets.splashTexts[static_cast<size_t>(splashIdx) % assets.splashTexts.size()];
                drawPulsingSplashText(ui, assets, splash, cx + 90.0f, 70.0f);
            }

            // ExactGuiMainMenu button layout (scaledH / 4 + 48)
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

            // Bottom-RightCopyright Notice
            const std::string copyStr = "CopyrightAB. Do not distribute!";
            float copyW = static_cast<float>(ui.getStringWidth(assets, copyStr));
            ui.drawString(assets, copyStr, static_cast<float>(ui.scaledW) - copyW - 2.0f, static_cast<float>(ui.scaledH) - 10.0f, 0xFFFFFFFF);
            return false;
        }

        // ====================================================================
        // 2. GuiSelectWorld (Singleplayer World List + Rename + Delete Confirm)
        // ====================================================================
        if (state == MENU_WORLD_SELECT) {
            drawTiledDirtBackground(ui, assets);

            bool hasWorlds = !savedWorlds.empty() && selectedWorld >= 0 && selectedWorld < static_cast<int>(savedWorlds.size());

            // Sub-dialog: Delete World Confirmation (GuiYesNo.java)
            if (confirmDeleteMode && hasWorlds) {
                ui.drawCenteredString(assets, "Are you sure you want to delete this world?", cx, ui.scaledH / 6.0f + 40.0f, 0xFFFFFFFF);
                std::string warnMsg = "'" + savedWorlds[static_cast<size_t>(selectedWorld)].levelData.levelName + "' will be lost forever! (A long time!)";
                ui.drawCenteredString(assets, warnMsg, cx, ui.scaledH / 6.0f + 56.0f, 0xFFA0A0A0);

                float yBtn = ui.scaledH / 6.0f + 96.0f;
                if (ui.drawClassicButton(assets, "Delete", cx - 155.0f, yBtn, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                    std::error_code ec;
                    std::filesystem::remove_all(savedWorlds[static_cast<size_t>(selectedWorld)].fullPath, ec);
                    confirmDeleteMode = false;
                    refreshWorlds(assets);
                }
                if (ui.drawClassicButton(assets, "Cancel", cx + 5.0f, yBtn, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                    confirmDeleteMode = false;
                }
                return false;
            }

            // Sub-dialog: Rename World (GuiRenameWorld.java)
            if (renameWorldMode && hasWorlds) {
                ui.drawCenteredString(assets, "Rename World", cx, 20.0f, 0xFFFFFFFF);
                ui.drawString(assets, "Enter world name", cx - 100.0f, 47.0f, 0xFFA0A0A0);

                if (backspacePressed && !renameBuffer.empty()) renameBuffer.pop_back();
                for (char c : typedChars) {
                    if (renameBuffer.size() < 32 && c >= 32 && c < 127) renameBuffer.push_back(c);
                }

                ui.drawSolidRect(cx - 100.0f, 60.0f, cx + 100.0f, 80.0f, 0xFFFFFFFF);
                ui.drawSolidRect(cx - 99.0f,  61.0f, cx + 99.0f,  79.0f, 0xFF000000);
                ui.drawString(assets, renameBuffer + "_", cx - 95.0f, 66.0f, 0xFFE0E0E0);

                float botY = static_cast<float>(ui.scaledH) - 52.0f;
                bool canSave = !renameBuffer.empty();
                if (ui.drawClassicButton(assets, "Rename", cx - 154.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked, canSave)) {
                    auto& w = savedWorlds[static_cast<size_t>(selectedWorld)];
                    w.levelData.levelName = renameBuffer;
                    McRegionStorage::saveLevelDat(w.fullPath, w.levelData);
                    renameWorldMode = false;
                    refreshWorlds(assets);
                }
                if (ui.drawClassicButton(assets, "Cancel", cx + 4.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                    renameWorldMode = false;
                }
                return false;
            }

            // Main GuiSelectWorld list (GuiSlot style dark scrollable container)
            ui.drawCenteredString(assets, "Select World", cx, 20.0f, 0xFFFFFFFF);

            float listTop    = 32.0f;
            float listBottom = static_cast<float>(ui.scaledH) - 64.0f;
            ui.drawSolidRect(0.0f, listTop, static_cast<float>(ui.scaledW), listBottom, 0xAA000000);
            ui.drawGradientRect(0.0f, listTop, static_cast<float>(ui.scaledW), listTop + 4.0f, 0xFF000000, 0x00000000);
            ui.drawGradientRect(0.0f, listBottom - 4.0f, static_cast<float>(ui.scaledW), listBottom, 0x00000000, 0xFF000000);

            int maxVisible = std::max(1, static_cast<int>((listBottom - listTop - 8.0f) / 36.0f));
            int maxOffset  = std::max(0, static_cast<int>(savedWorlds.size()) - maxVisible);
            worldScrollOffset = std::clamp(worldScrollOffset, 0, maxOffset);

            for (int v = 0; v < maxVisible && (worldScrollOffset + v) < static_cast<int>(savedWorlds.size()); ++v) {
                int idx = worldScrollOffset + v;
                const auto& w = savedWorlds[static_cast<size_t>(idx)];
                float wy = listTop + 4.0f + v * 36.0f;
                bool sel = (idx == selectedWorld);

                if (sel) {
                    ui.drawSolidRect(cx - 110.0f, wy - 2.0f, cx + 110.0f, wy + 32.0f, 0xFF808080);
                    ui.drawSolidRect(cx - 109.0f, wy - 1.0f, cx + 109.0f, wy + 31.0f, 0xFF000000);
                }

                ui.drawString(assets, w.levelData.levelName, cx - 105.0f, wy + 1.0f, 0xFFFFFFFF);

                char infoLine1[96];
                std::snprintf(infoLine1, sizeof(infoLine1), "%s (Seed: %lld)", w.folderName.c_str(), static_cast<long long>(w.levelData.seed));
                ui.drawString(assets, infoLine1, cx - 105.0f, wy + 12.0f, 0xFF808080);

                const char* dimLabel = (w.levelData.dimension == -1) ? "Nether (DIM-1)" : "Survival Mode";
                ui.drawString(assets, dimLabel, cx - 105.0f, wy + 22.0f, 0xFF808080);

                if (clicked && mouseX >= cx - 110 && mouseX <= cx + 110 && mouseY >= wy - 2 && mouseY <= wy + 32) {
                    selectedWorld = idx;
                }
            }

            // Up/Down scroll buttons when more worlds exist than fit on screen
            if (static_cast<int>(savedWorlds.size()) > maxVisible) {
                if (ui.drawClassicButton(assets, "^", cx + 116.0f, listTop + 4.0f, 20.0f, 20.0f, mouseX, mouseY, clicked, worldScrollOffset > 0)) {
                    worldScrollOffset--;
                }
                if (ui.drawClassicButton(assets, "v", cx + 116.0f, listBottom - 24.0f, 20.0f, 20.0f, mouseX, mouseY, clicked, worldScrollOffset < maxOffset)) {
                    worldScrollOffset++;
                }
            }

            // ExactGuiSelectWorld two-row bottom button layout
            float row1Y = static_cast<float>(ui.scaledH) - 52.0f;
            float row2Y = static_cast<float>(ui.scaledH) - 28.0f;

            if (ui.drawClassicButton(assets, "Play Selected World", cx - 154.0f, row1Y, 150.0f, 20.0f, mouseX, mouseY, clicked, hasWorlds)) {
                const auto& w = savedWorlds[static_cast<size_t>(selectedWorld)];
                outTargetSaveDir   = w.fullPath;
                outTargetWorldName = w.levelData.levelName;
                outTargetSeed      = w.levelData.seed;
                state = MENU_LOADING;
                return true;
            }
            if (ui.drawClassicButton(assets, "Create New World", cx + 4.0f, row1Y, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                newWorldName = "New World";
                newWorldSeed = "";
                focusedField = 0;
                state = MENU_CREATE_WORLD;
            }

            if (ui.drawClassicButton(assets, "Rename", cx - 154.0f, row2Y, 72.0f, 20.0f, mouseX, mouseY, clicked, hasWorlds)) {
                renameBuffer    = savedWorlds[static_cast<size_t>(selectedWorld)].levelData.levelName;
                renameWorldMode = true;
            }
            if (ui.drawClassicButton(assets, "Delete", cx - 76.0f, row2Y, 72.0f, 20.0f, mouseX, mouseY, clicked, hasWorlds)) {
                confirmDeleteMode = true;
            }
            if (ui.drawClassicButton(assets, "Cancel", cx + 4.0f, row2Y, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                state = MENU_TITLE;
            }
            return false;
        }

        // ====================================================================
        // 3. GuiCreateWorld (Unique Folder Generation + Exact Seed Input & Presets)
        // ====================================================================
        if (state == MENU_CREATE_WORLD) {
            drawTiledDirtBackground(ui, assets);
            ui.drawCenteredString(assets, "Create New World", cx, 20.0f, 0xFFFFFFFF);

            std::string& activeStr = (focusedField == 0) ? newWorldName : newWorldSeed;
            if (backspacePressed && !activeStr.empty()) activeStr.pop_back();
            for (char c : typedChars) {
                if (c == '\t') {
                    focusedField = 1 - focusedField;
                    continue;
                }
                if (activeStr.size() < 32 && c >= 32 && c < 127) {
                    activeStr.push_back(c);
                }
            }

            // World Name Text Box
            ui.drawString(assets, "World Name", cx - 100.0f, 44.0f, 0xFFA0A0A0);
            ui.drawSolidRect(cx - 100.0f, 56.0f, cx + 100.0f, 76.0f, focusedField == 0 ? 0xFFFFFFFF : 0xFFA0A0A0);
            ui.drawSolidRect(cx - 99.0f,  57.0f, cx + 99.0f,  75.0f, 0xFF000000);
            ui.drawString(assets, newWorldName + (focusedField == 0 ? "_" : ""), cx - 95.0f, 62.0f, 0xFFE0E0E0);
            if (clicked && mouseX >= cx - 100 && mouseX <= cx + 100 && mouseY >= 56 && mouseY <= 76) focusedField = 0;

            std::string previewFolder = makeUniqueWorldFolder("saves", newWorldName.empty() ? "World" : newWorldName);
            ui.drawString(assets, "Will be saved in: " + previewFolder, cx - 100.0f, 79.0f, 0xFFA0A0A0);

            // Seed Text Box
            ui.drawString(assets, "Seed for the World Generator", cx - 100.0f, 94.0f, 0xFFA0A0A0);
            ui.drawSolidRect(cx - 100.0f, 106.0f, cx + 100.0f, 126.0f, focusedField == 1 ? 0xFFFFFFFF : 0xFFA0A0A0);
            ui.drawSolidRect(cx - 99.0f,  107.0f, cx + 99.0f,  125.0f, 0xFF000000);
            ui.drawString(assets, newWorldSeed + (focusedField == 1 ? "_" : ""), cx - 95.0f, 112.0f, 0xFFE0E0E0);
            ui.drawString(assets, "Leave blank for a random seed", cx - 100.0f, 129.0f, 0xFFA0A0A0);
            if (clicked && mouseX >= cx - 100 && mouseX <= cx + 100 && mouseY >= 106 && mouseY <= 126) focusedField = 1;

            // Classic Beta 1.7.3 Famous Seed Quick-Fill Buttons
            float presetY = 144.0f;
            if (ui.drawClassicButton(assets, "gargamel", cx - 100.0f, presetY, 48.0f, 18.0f, mouseX, mouseY, clicked)) {
                newWorldSeed = "gargamel";
                focusedField = 1;
            }
            if (ui.drawClassicButton(assets, "404", cx - 49.0f, presetY, 48.0f, 18.0f, mouseX, mouseY, clicked)) {
                newWorldSeed = "404";
                focusedField = 1;
            }
            if (ui.drawClassicButton(assets, "Glacier", cx + 2.0f, presetY, 48.0f, 18.0f, mouseX, mouseY, clicked)) {
                newWorldSeed = "Glacier";
                focusedField = 1;
            }
            if (ui.drawClassicButton(assets, "Clear", cx + 53.0f, presetY, 47.0f, 18.0f, mouseX, mouseY, clicked)) {
                newWorldSeed.clear();
                focusedField = 1;
            }

            float botY = static_cast<float>(ui.scaledH) - 48.0f;
            if (ui.drawClassicButton(assets, "Create New World", cx - 154.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                std::string cleanName   = newWorldName.empty() ? "New World" : newWorldName;
                std::string uniqueDir   = makeUniqueWorldFolder("saves", cleanName);
                int64_t     seedVal     = parseBeta173Seed(newWorldSeed);

                outTargetWorldName = cleanName;
                outTargetSaveDir   = "saves/" + uniqueDir;
                outTargetSeed      = seedVal;
                state = MENU_LOADING;
                return true;
            }
            if (ui.drawClassicButton(assets, "Cancel", cx + 4.0f, botY, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                state = MENU_WORLD_SELECT;
            }
            return false;
        }

        // ====================================================================
        // 4. GuiLoading (Building Terrain Progress Bar)
        // ====================================================================
        if (state == MENU_LOADING) {
            drawTiledDirtBackground(ui, assets);
            ui.drawCenteredString(assets, "Generating level", cx, ui.scaledH * 0.5f - 24.0f, 0xFFFFFFFF);
            ui.drawCenteredString(assets, "Building terrain", cx, ui.scaledH * 0.5f - 8.0f, 0xFFAAAAAA);
            ui.drawSolidRect(cx - 50.0f, ui.scaledH * 0.5f + 8.0f, cx + 50.0f, ui.scaledH * 0.5f + 10.0f, 0xFF808080);
            ui.drawSolidRect(cx - 50.0f, ui.scaledH * 0.5f + 8.0f, cx - 50.0f + loadingProg * 100.0f, ui.scaledH * 0.5f + 10.0f, 0xFF80FF80);
            return false;
        }

        // ====================================================================
        // 5. GuiOptions (Exact2-Column Options Screen)
        // ====================================================================
        if (state == MENU_OPTIONS) {
            if (returnState == MENU_TITLE) drawTiledDirtBackground(ui, assets);
            else ui.drawGradientRect(0, 0, static_cast<float>(ui.scaledW), static_cast<float>(ui.scaledH), 0xC0101010, 0xD0101010);

            ui.drawCenteredString(assets, "Options", cx, 20.0f, 0xFFFFFFFF);

            char buf[64];
            float topRow = ui.scaledH / 6.0f + 24.0f;

            // Row 1 Left: Sound Volume
            if (settings.soundVolume <= 0.01f) std::snprintf(buf, sizeof(buf), "Sound: OFF");
            else std::snprintf(buf, sizeof(buf), "Sound: %d%%", static_cast<int>(settings.soundVolume * 100.0f));
            if (ui.drawClassicButton(assets, buf, cx - 155.0f, topRow, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.soundVolume += 0.25f;
                if (settings.soundVolume > 1.01f) settings.soundVolume = 0.0f;
            }

            // Row 1 Right: Sensitivity (Authentic"*yawn*" .. "HYPERSPEED!!!" labels)
            int sensPct = static_cast<int>(std::round(settings.mouseSensitivity * 200.0f));
            if (sensPct <= 0)        std::snprintf(buf, sizeof(buf), "Sensitivity: *yawn*");
            else if (sensPct >= 200) std::snprintf(buf, sizeof(buf), "Sensitivity: HYPERSPEED!!!");
            else                     std::snprintf(buf, sizeof(buf), "Sensitivity: %d%%", sensPct);
            if (ui.drawClassicButton(assets, buf, cx + 5.0f, topRow, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.mouseSensitivity += 0.125f;
                if (settings.mouseSensitivity > 1.01f) settings.mouseSensitivity = 0.125f;
            }

            // Row 2 Left: Render Distance
            const char* distNames[4] = { "TINY", "SHORT", "NORMAL", "FAR" };
            int distIdx = (settings.renderDistance <= 4) ? 0 : ((settings.renderDistance <= 6) ? 1 : ((settings.renderDistance <= 8) ? 2 : 3));
            std::snprintf(buf, sizeof(buf), "Render Distance: %s", distNames[distIdx]);
            if (ui.drawClassicButton(assets, buf, cx - 155.0f, topRow + 24.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.renderDistance += 2;
                if (settings.renderDistance > 12) settings.renderDistance = 4;
            }

            // Row 2 Right: Difficulty
            const char* diffNames[4] = { "Peaceful", "Easy", "Normal", "Hard" };
            std::snprintf(buf, sizeof(buf), "Difficulty: %s", diffNames[settings.difficulty & 3]);
            if (ui.drawClassicButton(assets, buf, cx + 5.0f, topRow + 24.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.difficulty = (settings.difficulty + 1) & 3;
            }

            // Row 3 Left: Smooth Lighting
            std::snprintf(buf, sizeof(buf), "Smooth Lighting: %s", settings.smoothLighting ? "ON" : "OFF");
            if (ui.drawClassicButton(assets, buf, cx - 155.0f, topRow + 48.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.smoothLighting = !settings.smoothLighting;
            }

            // Row 3 Right: GUI Scale
            const char* scaleNames[4] = { "Auto", "Small", "Normal", "Large" };
            std::snprintf(buf, sizeof(buf), "GUI Scale: %s", scaleNames[settings.guiScale & 3]);
            if (ui.drawClassicButton(assets, buf, cx + 5.0f, topRow + 48.0f, 150.0f, 20.0f, mouseX, mouseY, clicked)) {
                settings.guiScale = (settings.guiScale + 1) & 3;
            }

            if (ui.drawClassicButton(assets, "Done", cx - 100.0f, ui.scaledH / 6.0f + 168.0f, 200.0f, 20.0f, mouseX, mouseY, clicked)) {
                state = returnState;
            }
            return false;
        }
        return false;
    }
};