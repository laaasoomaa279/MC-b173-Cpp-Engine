#pragma once
#include "ui/ui_primitives.hpp"
#include "gameplay/physics.hpp"
#include "entities/entities_ai.hpp"

#include <windows.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <deque>
#include <cstdio>
#include <algorithm>


enum ActiveGuiScreen : uint8_t {
    GUI_NONE       = 0,
    GUI_INVENTORY  = 1,
    GUI_CRAFTING   = 2,
    GUI_FURNACE    = 3,
    GUI_CHEST      = 4,
    GUI_CHAT       = 5,
    GUI_PAUSE      = 6,
    GUI_DEATH      = 7
};

struct ChatLine {
    std::string text;
    int         ageTicks = 0;
};

class BetaInventoryAndHud {
public:
    ItemStack            mainSlots[36]{};
    ItemStack            armorSlots[4]{};
    ItemStack            craftGrid[9]{};
    ItemStack            craftOutput{};
    ItemStack            cursorStack{};

    int                  selectedHotbar  = 0;
    ActiveGuiScreen      activeScreen    = GUI_NONE;
    BlockPosKey          openTilePos{};

    bool                 tmiVisible      = true;
    int                  tmiPage         = 0;

    bool                 showDebugF3     = false;
    float                fpsHistory[128]{};
    int                  fpsHistIdx      = 0;

    std::string          chatInput;
    std::deque<ChatLine> chatHistory;

    void addChatMessage(const std::string& msg) {
        chatHistory.push_front({ msg, 0 });
        if (chatHistory.size() > 64) chatHistory.pop_back();
    }

    void tickChat() {
        for (auto& c : chatHistory) c.ageTicks++;
    }

    ItemStack& getHeldStack() { return mainSlots[selectedHotbar]; }
    const ItemStack& getHeldStack() const { return mainSlots[selectedHotbar]; }

    bool giveItem(const BetaAssetManager& assets, uint16_t id, uint16_t damage, uint8_t count) {
        if (id == 0 || count == 0) return false;
        uint8_t maxSt = assets.getMaxStackSize(id);

        if (maxSt > 1) {
            for (int i = 0; i < 36 && count > 0; ++i) {
                if (mainSlots[i].id == id && mainSlots[i].damage == damage && mainSlots[i].count < maxSt) {
                    uint8_t add = (uint8_t)std::min<int>(count, maxSt - mainSlots[i].count);
                    mainSlots[i].count += add;
                    count -= add;
                }
            }
        }

        for (int i = 0; i < 36 && count > 0; ++i) {
            if (mainSlots[i].isEmpty()) {
                uint8_t put = std::min(count, maxSt);
                mainSlots[i] = { id, damage, put };
                count -= put;
            }
        }
        return count == 0;
    }

    void updateCraftingPreview(const BetaRecipeRegistry& recipes) {
        int dim = (activeScreen == GUI_CRAFTING) ? 3 : 2;
        craftOutput = recipes.matchCraftingGrid(craftGrid, dim, dim);
    }

    //GuiIngame.renderGameOverlay (Exact Beta 1.7.3 HUD & F3 Screen)
    void renderHUD(BetaUiRenderer& ui, const WorldMap& world, const BetaAssetManager& assets,
                   const PlayerPhysics& player, const BetaEntityManager& entities, float fps) {
        fpsHistory[fpsHistIdx] = fps;
        fpsHistIdx = (fpsHistIdx + 1) & 127;

        float cx = ui.scaledW * 0.5f;
        float bottom = (float)ui.scaledH;

        // 1. Crosshair withexact XOR color inversion
        if (activeScreen == GUI_NONE && assets.iconsTex) {
            glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR);
            ui.drawTexturedRect(assets.iconsTex, std::floor(cx - 8.0f), std::floor(ui.scaledH * 0.5f - 8.0f), 16.0f, 16.0f, 0, 0, 16, 16);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // 2. Hotbar Background & Selector Frame (gui/gui.png)
        float hbX = std::floor(cx - 91.0f);
        float hbY = bottom - 22.0f;
        ui.drawTexturedRect(assets.guiTex, hbX, hbY, 182.0f, 22.0f, 0, 0, 182, 22);
        ui.drawTexturedRect(assets.guiTex, hbX - 1.0f + selectedHotbar * 20.0f, hbY - 1.0f, 24.0f, 24.0f, 0, 22, 24, 46);

        for (int i = 0; i < 9; ++i) {
            ui.drawItemStack(world, assets, mainSlots[i], hbX + 3.0f + i * 20.0f, hbY + 3.0f);
        }

        // 3. Survival Hearts, Armor & Air Bubbles (Exact Beta 1.7.3 GuiIngame.java)
        if (!player.flying && assets.iconsTex) {
            int hp = (int)std::ceil(player.health);
            bool lowHp = (hp <= 4);
            bool hurtFlash = (player.hurtTime > 0 && (player.hurtTime / 3) % 2 == 1);

            for (int h = 0; h < 10; ++h) {
                float hx = hbX + h * 8.0f;
                float hy = hbY - 10.0f + (lowHp ? ((world.worldTick + h) & 1) : 0);
                int bgU = hurtFlash ? 25 : 16;
                ui.drawTexturedRect(assets.iconsTex, hx, hy, 9.0f, 9.0f, (float)bgU, 0, (float)(bgU + 9), 9);

                if (h * 2 + 1 < hp)       ui.drawTexturedRect(assets.iconsTex, hx, hy, 9.0f, 9.0f, 52, 0, 61, 9);
                else if (h * 2 + 1 == hp) ui.drawTexturedRect(assets.iconsTex, hx, hy, 9.0f, 9.0f, 61, 0, 70, 9);
            }

            // Classic Beta 1.7.3 Armor Bar (Top-right above hotbar, durability-scaled)
            int curArmor = 0, maxArmor = 0;
            for (int a = 0; a < 4; ++a) {
                if (!armorSlots[a].isEmpty()) {
                    const ItemDef& ad = assets.getItemDef(armorSlots[a].id);
                    if (ad.maxDamage > 0 && ad.armorPoints > 0) {
                        int rem = std::max(0, (int)ad.maxDamage - (int)armorSlots[a].damage);
                        curArmor += (ad.armorPoints * rem) / (int)ad.maxDamage;
                        maxArmor += ad.armorPoints;
                    }
                }
            }
            if (maxArmor > 0) {
                int armorIcons = (curArmor * 20) / 25;
                for (int a = 0; a < 10; ++a) {
                    float ax = hbX + 182.0f - (a + 1) * 8.0f;
                    float ay = hbY - 10.0f;
                    if (a * 2 + 1 < armorIcons)       ui.drawTexturedRect(assets.iconsTex, ax, ay, 9.0f, 9.0f, 34, 9, 43, 18);
                    else if (a * 2 + 1 == armorIcons) ui.drawTexturedRect(assets.iconsTex, ax, ay, 9.0f, 9.0f, 25, 9, 34, 18);
                    else                              ui.drawTexturedRect(assets.iconsTex, ax, ay, 9.0f, 9.0f, 16, 9, 25, 18);
                }
            }

            // Underwater Air Bubbles
            if (player.eyesInWater) {
                int fullBubbles = (int)std::ceil((player.airSupply - 2) * 10.0f / 300.0f);
                int popBubbles  = (int)std::ceil(player.airSupply * 10.0f / 300.0f) - fullBubbles;
                for (int b = 0; b < fullBubbles + popBubbles; ++b) {
                    float bx = hbX + b * 8.0f;
                    float by = hbY - 20.0f;
                    if (b < fullBubbles) ui.drawTexturedRect(assets.iconsTex, bx, by, 9.0f, 9.0f, 16, 18, 25, 27);
                    else                 ui.drawTexturedRect(assets.iconsTex, bx, by, 9.0f, 9.0f, 25, 18, 34, 27);
                }
            }
        }

        // 4.Exact Beta 1.7.3 F3 Debug Screen + Frame Profiler Graph
        if (showDebugF3) {
            char line[160];
            std::snprintf(line, sizeof(line), "Minecraft Beta 1.7.3 (%.0f fps, 0 chunk updates)", fps);
            ui.drawString(assets, line, 2, 2, 0xFFFFFFFF);
            std::snprintf(line, sizeof(line), "C: %d/%d. F: 0, O: 0, E: %zu",
                          (world.renderDistance * 2 + 1) * (world.renderDistance * 2 + 1) * 8,
                          GRID_DIM * GRID_DIM * 8, entities.mobs.size());
            ui.drawString(assets, line, 2, 12, 0xFFFFFFFF);
            std::snprintf(line, sizeof(line), "E: %zu/%zu. B: 0, I: %zu",
                          entities.mobs.size(), entities.mobs.size() + entities.droppedItems.size(), entities.droppedItems.size());
            ui.drawString(assets, line, 2, 22, 0xFFFFFFFF);

            // Top-Right Memory Readout (ExactGuiIngame.java)
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                size_t usedMB = pmc.WorkingSetSize / (1024 * 1024);
                std::snprintf(line, sizeof(line), "Used memory: %zuMB", usedMB);
                ui.drawString(assets, line, (float)ui.scaledW - ui.getStringWidth(assets, line) - 2.0f, 2, 0xFFE0E0E0);
                std::snprintf(line, sizeof(line), "Allocated memory: 512MB");
                ui.drawString(assets, line, (float)ui.scaledW - ui.getStringWidth(assets, line) - 2.0f, 12, 0xFFE0E0E0);
            }

            // Coordinates (x, y, z, f)
            std::snprintf(line, sizeof(line), "x: %.5f", player.x);
            ui.drawString(assets, line, 2, 64, 0xFFE0E0E0);
            std::snprintf(line, sizeof(line), "y: %.3f (feet pos, %.3f eyes pos)", player.y, player.y + PlayerPhysics::EYE_HEIGHT);
            ui.drawString(assets, line, 2, 72, 0xFFE0E0E0);
            std::snprintf(line, sizeof(line), "z: %.5f", player.z);
            ui.drawString(assets, line, 2, 80, 0xFFE0E0E0);
            int dirF = ((int)std::floor(player.yaw / 90.0f + 0.5f)) & 3;
            std::snprintf(line, sizeof(line), "f: %d", dirF);
            ui.drawString(assets, line, 2, 88, 0xFFE0E0E0);

            // Bottom-Left Beta 1.7.3 Frame-Time Bar Graph
            float gx0 = 2.0f, gyBottom = bottom - 2.0f;
            for (int i = 0; i < 128; ++i) {
                float sampleFps = fpsHistory[(fpsHistIdx + i) & 127];
                float frameMs   = 1000.0f / std::max(10.0f, sampleFps);
                float barH      = std::clamp(frameMs * 1.5f, 2.0f, 48.0f);
                uint32_t barCol = (sampleFps >= 55.0f) ? 0xC020FF20 : ((sampleFps >= 30.0f) ? 0xC0FFFF20 : 0xC0FF2020);
                ui.drawSolidRect(gx0 + i, gyBottom - barH, gx0 + i + 1.0f, gyBottom, barCol);
            }
        }

        // 5. Authentic Beta 1.7.3 Chat Overlay (Clean floating text with shadow; dark box only when typing)
        int visibleLines = (activeScreen == GUI_CHAT) ? 20 : 10;
        int shown = 0;
        for (const auto& cl : chatHistory) {
            if (activeScreen != GUI_CHAT && cl.ageTicks > 200) break;
            if (shown >= visibleLines) break;
            float cyLine = bottom - 40.0f - shown * 9.0f;
            if (activeScreen == GUI_CHAT) {
                ui.drawSolidRect(2.0f, cyLine - 1.0f, 322.0f, cyLine + 8.0f, 0x80000000);
            }
            ui.drawString(assets, cl.text, 4.0f, cyLine, 0xFFFFFFFF, true);
            shown++;
        }
        if (activeScreen == GUI_CHAT) {
            ui.drawSolidRect(2.0f, bottom - 14.0f, (float)ui.scaledW - 2.0f, bottom - 2.0f, 0x80000000);
            ui.drawString(assets, "> " + chatInput + "_", 4.0f, bottom - 12.0f, 0xFFE0E0E0, true);
        }
    }

    //GuiContainer.drawScreen (Inventory 2x2 + Player Preview, Crafting 3x3, Furnace, Chest + TMI)
    void renderContainerScreen(BetaUiRenderer& ui, WorldMap& world, const BetaAssetManager& assets,
                               const BetaRecipeRegistry& recipes, int mouseX, int mouseY,
                               bool leftClick, bool rightClick) {
        if (activeScreen != GUI_INVENTORY && activeScreen != GUI_CRAFTING &&
            activeScreen != GUI_FURNACE && activeScreen != GUI_CHEST) {
            return;
        }

        ui.drawGradientRect(0, 0, (float)ui.scaledW, (float)ui.scaledH, 0xC0101010, 0xD0101010);

        float winW = 176.0f, winH = 166.0f;
        float gx = std::floor((ui.scaledW - winW) * 0.5f);
        float gy = std::floor((ui.scaledH - winH) * 0.5f);

        std::string hoveredName;

        auto handleSlot = [&](ItemStack& slot, float sx, float sy, bool outputOnly = false, ArmorSlotType armorReq = ARMOR_NONE) {
            ui.drawItemStack(world, assets, slot, sx, sy);
            bool hov = (mouseX >= sx - 1 && mouseX < sx + 17 && mouseY >= sy - 1 && mouseY < sy + 17);
            if (hov) {
                ui.drawSolidRect(sx, sy, sx + 16.0f, sy + 16.0f, 0x80FFFFFF);
                if (!slot.isEmpty()) {
                    hoveredName = (slot.id < 256) ? world.registry.getById((uint8_t)slot.id).name : assets.getItemDef(slot.id).name;
                }
                if (leftClick || rightClick) {
                    if (outputOnly) {
                        if (!slot.isEmpty() && (cursorStack.isEmpty() || (cursorStack.id == slot.id && cursorStack.damage == slot.damage && cursorStack.count + slot.count <= assets.getMaxStackSize(slot.id)))) {
                            if (cursorStack.isEmpty()) cursorStack = slot;
                            else cursorStack.count += slot.count;
                            slot.clear();
                            if (activeScreen == GUI_INVENTORY || activeScreen == GUI_CRAFTING) {
                                int cnt = (activeScreen == GUI_CRAFTING) ? 9 : 4;
                                for (int k = 0; k < cnt; ++k) {
                                    if (!craftGrid[k].isEmpty() && --craftGrid[k].count == 0) craftGrid[k].clear();
                                }
                                updateCraftingPreview(recipes);
                            }
                        }
                    } else {
                        if (armorReq != ARMOR_NONE && !cursorStack.isEmpty()) {
                            if (cursorStack.id < 256 || assets.getItemDef(cursorStack.id).armorSlot != armorReq) return;
                        }
                        if (leftClick) {
                            if (!cursorStack.isEmpty() && cursorStack.id == slot.id && cursorStack.damage == slot.damage) {
                                uint8_t maxS = assets.getMaxStackSize(slot.id);
                                uint8_t move = (uint8_t)std::min<int>(cursorStack.count, maxS - slot.count);
                                slot.count += move;
                                cursorStack.count -= move;
                                if (cursorStack.count == 0) cursorStack.clear();
                            } else {
                                std::swap(slot, cursorStack);
                            }
                        } else if (rightClick) {
                            if (cursorStack.isEmpty() && !slot.isEmpty()) {
                                uint8_t half = (slot.count + 1) / 2;
                                cursorStack = { slot.id, slot.damage, half };
                                slot.count -= half;
                                if (slot.count == 0) slot.clear();
                            } else if (!cursorStack.isEmpty() && (slot.isEmpty() || (slot.id == cursorStack.id && slot.damage == cursorStack.damage && slot.count < assets.getMaxStackSize(slot.id)))) {
                                if (slot.isEmpty()) slot = { cursorStack.id, cursorStack.damage, 1 };
                                else slot.count++;
                                if (--cursorStack.count == 0) cursorStack.clear();
                            }
                        }
                        if (activeScreen == GUI_INVENTORY || activeScreen == GUI_CRAFTING) {
                            updateCraftingPreview(recipes);
                        }
                    }
                }
            }
        };

        if (activeScreen == GUI_INVENTORY) {
            ui.drawTexturedRect(assets.inventoryTex, gx, gy, winW, winH, 0, 0, 176, 166);
            ui.drawString(assets, "Crafting", gx + 86.0f, gy + 16.0f, 0xFF404040, false);

            ui.drawPaperDollPlayer(assets, gx + 51.0f, gy + 75.0f, mouseX, mouseY);

            for (int a = 0; a < 4; ++a) {
                ArmorSlotType req = (ArmorSlotType)(ARMOR_HELMET + a);
                handleSlot(armorSlots[3 - a], gx + 8.0f, gy + 8.0f + a * 18.0f, false, req);
            }
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c)
                    handleSlot(craftGrid[r * 2 + c], gx + 88.0f + c * 18.0f, gy + 26.0f + r * 18.0f);
            handleSlot(craftOutput, gx + 144.0f, gy + 36.0f, true);
        } else if (activeScreen == GUI_CRAFTING) {
            ui.drawTexturedRect(assets.craftingTex, gx, gy, winW, winH, 0, 0, 176, 166);
            ui.drawString(assets, "Crafting", gx + 28.0f, gy + 6.0f, 0xFF404040, false);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    handleSlot(craftGrid[r * 3 + c], gx + 30.0f + c * 18.0f, gy + 17.0f + r * 18.0f);
            handleSlot(craftOutput, gx + 124.0f, gy + 35.0f, true);
        } else if (activeScreen == GUI_FURNACE) {
            ui.drawTexturedRect(assets.furnaceTex, gx, gy, winW, winH, 0, 0, 176, 166);
            ui.drawString(assets, "Furnace", gx + 60.0f, gy + 6.0f, 0xFF404040, false);
            std::lock_guard<std::mutex> lock(world.tileEntityMutex);
            auto it = world.tileEntities.find(openTilePos);
            if (it != world.tileEntities.end()) {
                TileEntityData& te = it->second;
                if (te.burnTime > 0 && te.maxBurnTime > 0) {
                    float h = 13.0f * ((float)te.burnTime / (float)te.maxBurnTime);
                    ui.drawTexturedRect(assets.furnaceTex, gx + 56.0f, gy + 36.0f + 13.0f - h, 14.0f, h, 176.0f, 13.0f - h, 190.0f, 13.0f);
                }
                if (te.cookTime > 0) {
                    float w = 24.0f * ((float)te.cookTime / 200.0f);
                    ui.drawTexturedRect(assets.furnaceTex, gx + 79.0f, gy + 34.0f, w, 17.0f, 176.0f, 14.0f, 176.0f + w, 31.0f);
                }
                handleSlot(te.slots[0], gx + 56.0f, gy + 17.0f);
                handleSlot(te.slots[1], gx + 56.0f, gy + 53.0f);
                handleSlot(te.slots[2], gx + 116.0f, gy + 35.0f, true);
            }
        } else if (activeScreen == GUI_CHEST) {
            ui.drawTexturedRect(assets.containerTex, gx, gy, winW, winH, 0, 0, 176, 166);
            ui.drawString(assets, "Chest", gx + 8.0f, gy + 6.0f, 0xFF404040, false);
            std::lock_guard<std::mutex> lock(world.tileEntityMutex);
            auto it = world.tileEntities.find(openTilePos);
            if (it != world.tileEntities.end()) {
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 9; ++c)
                        handleSlot(it->second.slots[r * 9 + c], gx + 8.0f + c * 18.0f, gy + 18.0f + r * 18.0f);
            }
        }

        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 9; ++c)
                handleSlot(mainSlots[9 + r * 9 + c], gx + 8.0f + c * 18.0f, gy + 84.0f + r * 18.0f);

        for (int c = 0; c < 9; ++c)
            handleSlot(mainSlots[c], gx + 8.0f + c * 18.0f, gy + 142.0f);

        if (tmiVisible && !recipes.tmiCatalog.empty()) {
            int cols = std::max(3, (int)((ui.scaledW - (gx + winW + 16.0f)) / 18.0f));
            int rows = std::max(6, (ui.scaledH - 36) / 18);
            int perPage = cols * rows;
            int totalPages = std::max(1, ((int)recipes.tmiCatalog.size() + perPage - 1) / perPage);
            tmiPage = std::clamp(tmiPage, 0, totalPages - 1);

            float sx0 = std::floor(ui.scaledW - cols * 18.0f - 6.0f);
            float sy0 = 22.0f;
            ui.drawSolidRect(sx0 - 4.0f, 4.0f, (float)ui.scaledW - 2.0f, (float)ui.scaledH - 4.0f, 0x80000000);

            if (ui.drawClassicButton(assets, "<", sx0, 6.0f, 18.0f, 14.0f, mouseX, mouseY, leftClick)) {
                tmiPage = (tmiPage + totalPages - 1) % totalPages;
            }
            char pgTxt[24];
            std::snprintf(pgTxt, sizeof(pgTxt), "%d/%d", tmiPage + 1, totalPages);
            ui.drawCenteredString(assets, pgTxt, sx0 + cols * 9.0f, 9.0f, 0xFFFFFFFF);
            if (ui.drawClassicButton(assets, ">", sx0 + cols * 18.0f - 18.0f, 6.0f, 18.0f, 14.0f, mouseX, mouseY, leftClick)) {
                tmiPage = (tmiPage + 1) % totalPages;
            }

            int startIdx = tmiPage * perPage;
            for (int idx = 0; idx < perPage && (startIdx + idx) < (int)recipes.tmiCatalog.size(); ++idx) {
                int r = idx / cols, c = idx % cols;
                float ix = sx0 + c * 18.0f, iy = sy0 + r * 18.0f;
                const ItemStack& item = recipes.tmiCatalog[(size_t)(startIdx + idx)];
                ui.drawItemStack(world, assets, item, ix, iy);

                if (mouseX >= ix && mouseX < ix + 16 && mouseY >= iy && mouseY < iy + 16) {
                    ui.drawSolidRect(ix, iy, ix + 16.0f, iy + 16.0f, 0x80FFFFFF);
                    hoveredName = (item.id < 256) ? world.registry.getById((uint8_t)item.id).name : assets.getItemDef(item.id).name;
                    if (leftClick) {
                        uint8_t amt = assets.getMaxStackSize(item.id);
                        giveItem(assets, item.id, item.damage, amt);
                    } else if (rightClick) {
                        giveItem(assets, item.id, item.damage, 1);
                    }
                }
            }
        }

        if (!cursorStack.isEmpty()) {
            ui.drawItemStack(world, assets, cursorStack, mouseX - 8.0f, mouseY - 8.0f);
        } else if (!hoveredName.empty()) {
            ui.drawTooltip(assets, hoveredName, (float)mouseX, (float)mouseY);
        }
    }
};