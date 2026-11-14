#pragma once
#include "assets/asset_loader.hpp"
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>

// ============================================================================
// Crafting, Smelting & TooManyItems (TMI) List
// ============================================================================

struct ItemStack {
    uint16_t id     = 0;
    uint16_t damage = 0; // Metadata for blocks (wool color, log type) or durability damage for tools
    uint8_t  count  = 0;

    bool isEmpty() const { return id == 0 || count == 0; }
    void clear() { id = 0; damage = 0; count = 0; }
};

struct CraftingIngredient {
    uint16_t id        = 0;
    uint16_t damage    = 0;
    bool     anyDamage = true;

    bool matches(const ItemStack& st) const {
        if (id == 0) return st.isEmpty();
        if (st.isEmpty() || st.id != id) return false;
        return anyDamage || (st.damage == damage);
    }
};

struct BetaRecipe {
    bool                              shapeless = false;
    int                               width     = 0;
    int                               height    = 0;
    std::array<CraftingIngredient, 9> pattern{};
    std::vector<CraftingIngredient>   ingredients;
    ItemStack                         result{};
};

class BetaRecipeRegistry {
public:
    std::vector<BetaRecipe> recipes;
    std::vector<ItemStack>  tmiCatalog; // Complete TooManyItems sidebar list

    BetaRecipeRegistry() {
        registerAllBeta173Recipes();
        buildTooManyItemsCatalog();
    }

    // Matches a 2x2 (w=2,h=2) or 3x3 (w=3,h=3) crafting grid with horizontal mirroring
    ItemStack matchCraftingGrid(const ItemStack* grid, int gridW, int gridH) const {
        for (const auto& rec : recipes) {
            if (rec.shapeless) {
                if (matchShapeless(rec, grid, gridW * gridH)) return rec.result;
            } else {
                if (matchShaped(rec, grid, gridW, gridH, false)) return rec.result;
                if (rec.width > 1 && matchShaped(rec, grid, gridW, gridH, true)) return rec.result;
            }
        }
        return ItemStack{};
    }

    // Beta 1.7.3 Furnace Smelting Recipes
    static ItemStack getSmeltingResult(const ItemStack& input) {
        if (input.isEmpty()) return ItemStack{};
        switch (input.id) {
            case 15:                  return { ITEM_IRON_INGOT,      0, 1 }; // Iron Ore -> Iron Ingot
            case 14:                  return { ITEM_GOLD_INGOT,      0, 1 }; // Gold Ore -> Gold Ingot
            case 12:                  return { 20,                   0, 1 }; // Sand -> Glass
            case 4:                   return { 1,                    0, 1 }; // Cobblestone -> Stone
            case ITEM_PORKCHOP_RAW:   return { ITEM_PORKCHOP_COOKED, 0, 1 }; // Raw Porkchop -> Cooked
            case ITEM_FISH_RAW:       return { ITEM_FISH_COOKED,     0, 1 }; // Raw Fish -> Cooked
            case ITEM_CLAY_BALL:      return { ITEM_BRICK_INGOT,     0, 1 }; // Clay Ball -> Brick
            case 17:                  return { ITEM_COAL,            1, 1 }; // Log -> Charcoal (meta 1)
            case 81:                  return { ITEM_DYE,             2, 1 }; // Cactus -> Cactus Green
            default:                  return ItemStack{};
        }
    }

    // Beta 1.7.3 Furnace Fuel Burn Time in Ticks (20 ticks = 1 second)
    static int getFuelBurnTicks(const ItemStack& fuel) {
        if (fuel.isEmpty()) return 0;
        switch (fuel.id) {
            case ITEM_LAVA_BUCKET: return 20000;
            case ITEM_COAL:        return 1600;
            case 5:                return 300;  // Wooden Planks
            case 17:               return 300;  // Wood Log
            case 53:               return 300;  // Wooden Stairs
            case 85:               return 300;  // Fence
            case 58:               return 300;  // Crafting Table
            case 47:               return 300;  // Bookshelf
            case 54:               return 300;  // Chest
            case 84:               return 300;  // Jukebox
            case 25:               return 300;  // Note Block
            case 96:               return 300;  // Trapdoor
            case ITEM_WOOD_SWORD:
            case ITEM_WOOD_PICKAXE:
            case ITEM_WOOD_AXE:
            case ITEM_WOOD_SHOVEL:
            case ITEM_WOOD_HOE:    return 200;
            case ITEM_STICK:       return 100;
            case 6:                return 100;  // Sapling
            default:               return 0;
        }
    }

private:
    static bool matchShapeless(const BetaRecipe& rec, const ItemStack* grid, int totalSlots) {
        std::vector<bool> used(rec.ingredients.size(), false);
        int nonEmptyCount = 0;

        for (int i = 0; i < totalSlots; ++i) {
            if (grid[i].isEmpty()) continue;
            nonEmptyCount++;
            bool found = false;
            for (size_t k = 0; k < rec.ingredients.size(); ++k) {
                if (!used[k] && rec.ingredients[k].matches(grid[i])) {
                    used[k] = true;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return nonEmptyCount == (int)rec.ingredients.size();
    }

    static bool matchShaped(const BetaRecipe& rec, const ItemStack* grid, int gridW, int gridH, bool mirrorX) {
        if (rec.width > gridW || rec.height > gridH) return false;

        for (int offY = 0; offY <= gridH - rec.height; ++offY) {
            for (int offX = 0; offX <= gridW - rec.width; ++offX) {
                bool ok = true;
                for (int gy = 0; gy < gridH && ok; ++gy) {
                    for (int gx = 0; gx < gridW && ok; ++gx) {
                        int rx = gx - offX;
                        int ry = gy - offY;
                        const ItemStack& slot = grid[gy * gridW + gx];

                        if (rx >= 0 && rx < rec.width && ry >= 0 && ry < rec.height) {
                            int srcX = mirrorX ? (rec.width - 1 - rx) : rx;
                            const CraftingIngredient& ing = rec.pattern[ry * rec.width + srcX];
                            if (!ing.matches(slot)) ok = false;
                        } else {
                            if (!slot.isEmpty()) ok = false;
                        }
                    }
                }
                if (ok) return true;
            }
        }
        return false;
    }

    void addShaped(int w, int h, std::initializer_list<uint16_t> ids, ItemStack out) {
        BetaRecipe r;
        r.shapeless = false;
        r.width     = w;
        r.height    = h;
        r.result    = out;
        int idx = 0;
        for (uint16_t id : ids) {
            if (idx < 9) r.pattern[idx++] = { id, 0, true };
        }
        recipes.push_back(std::move(r));
    }

    void addShapeless(std::initializer_list<uint16_t> ids, ItemStack out) {
        BetaRecipe r;
        r.shapeless = true;
        r.result    = out;
        for (uint16_t id : ids) {
            r.ingredients.push_back({ id, 0, true });
        }
        recipes.push_back(std::move(r));
    }

    void addToolSet(uint16_t matId, uint16_t swordId, uint16_t pickId, uint16_t axeId, uint16_t shovelId, uint16_t hoeId) {
        uint16_t s = ITEM_STICK;
        addShaped(1, 3, { matId, matId, s }, { swordId, 0, 1 });
        addShaped(3, 3, { matId, matId, matId, 0, s, 0, 0, s, 0 }, { pickId, 0, 1 });
        addShaped(2, 3, { matId, matId, matId, s, 0, s }, { axeId, 0, 1 });
        addShaped(1, 3, { matId, s, s }, { shovelId, 0, 1 });
        addShaped(2, 3, { matId, matId, 0, s, 0, s }, { hoeId, 0, 1 });
    }

    void addArmorSet(uint16_t matId, uint16_t helmId, uint16_t chestId, uint16_t legsId, uint16_t bootsId) {
        addShaped(3, 2, { matId, matId, matId, matId, 0, matId }, { helmId, 0, 1 });
        addShaped(3, 3, { matId, 0, matId, matId, matId, matId, matId, matId, matId }, { chestId, 0, 1 });
        addShaped(3, 3, { matId, matId, matId, matId, 0, matId, matId, 0, matId }, { legsId, 0, 1 });
        addShaped(3, 2, { matId, 0, matId, matId, 0, matId }, { bootsId, 0, 1 });
    }

    void registerAllBeta173Recipes() {
        const uint16_t PLANK  = 5;          // Wooden Planks
        const uint16_t COBBLE = 4;          // Cobblestone
        const uint16_t STICK  = ITEM_STICK; // Stick
        const uint16_t IRON   = ITEM_IRON_INGOT;
        const uint16_t GOLD   = ITEM_GOLD_INGOT;
        const uint16_t DIAM   = ITEM_DIAMOND;
        const uint16_t RED    = ITEM_REDSTONE;

        // Basic Wood, Crafting, Furnace, Chest & Torches
        addShapeless({ 17 }, { 5, 0, 4 }); // 1 Log -> 4 Planks
        addShaped(1, 2, { PLANK, PLANK }, { STICK, 0, 4 }); // 2 Planks -> 4 Sticks
        addShaped(2, 2, { PLANK, PLANK, PLANK, PLANK }, { 58, 0, 1 }); // Crafting Table
        addShaped(3, 3, { COBBLE, COBBLE, COBBLE, COBBLE, 0, COBBLE, COBBLE, COBBLE, COBBLE }, { 61, 0, 1 }); // Furnace
        addShaped(3, 3, { PLANK, PLANK, PLANK, PLANK, 0, PLANK, PLANK, PLANK, PLANK }, { 54, 0, 1 }); // Chest
        addShaped(1, 2, { ITEM_COAL, STICK }, { 50, 0, 4 }); // 4 Torches

        // Tools & Armor Sets
        addToolSet(PLANK,  ITEM_WOOD_SWORD,    ITEM_WOOD_PICKAXE,    ITEM_WOOD_AXE,    ITEM_WOOD_SHOVEL,    ITEM_WOOD_HOE);
        addToolSet(COBBLE, ITEM_STONE_SWORD,   ITEM_STONE_PICKAXE,   ITEM_STONE_AXE,   ITEM_STONE_SHOVEL,   ITEM_STONE_HOE);
        addToolSet(IRON,   ITEM_IRON_SWORD,    ITEM_IRON_PICKAXE,    ITEM_IRON_AXE,    ITEM_IRON_SHOVEL,    ITEM_IRON_HOE);
        addToolSet(DIAM,   ITEM_DIAMOND_SWORD, ITEM_DIAMOND_PICKAXE, ITEM_DIAMOND_AXE, ITEM_DIAMOND_SHOVEL, ITEM_DIAMOND_HOE);
        addToolSet(GOLD,   ITEM_GOLD_SWORD,    ITEM_GOLD_PICKAXE,    ITEM_GOLD_AXE,    ITEM_GOLD_SHOVEL,    ITEM_GOLD_HOE);

        addArmorSet(ITEM_LEATHER, ITEM_LEATHER_HELMET,  ITEM_LEATHER_CHEST,  ITEM_LEATHER_LEGS,  ITEM_LEATHER_BOOTS);
        addArmorSet(IRON,         ITEM_IRON_HELMET,     ITEM_IRON_CHEST,     ITEM_IRON_LEGS,     ITEM_IRON_BOOTS);
        addArmorSet(DIAM,         ITEM_DIAMOND_HELMET,  ITEM_DIAMOND_CHEST,  ITEM_DIAMOND_LEGS,  ITEM_DIAMOND_BOOTS);
        addArmorSet(GOLD,         ITEM_GOLD_HELMET,     ITEM_GOLD_CHEST,     ITEM_GOLD_LEGS,     ITEM_GOLD_BOOTS);

        // Archery, Flint & Steel, Shears, Bucket, Compass & Clock
        addShaped(3, 3, { 0, STICK, ITEM_STRING, STICK, 0, ITEM_STRING, 0, STICK, ITEM_STRING }, { ITEM_BOW, 0, 1 });
        addShaped(1, 3, { ITEM_FLINT, STICK, ITEM_FEATHER }, { ITEM_ARROW, 0, 4 });
        addShapeless({ IRON, ITEM_FLINT }, { ITEM_FLINT_AND_STEEL, 0, 1 });
        addShaped(2, 2, { 0, IRON, IRON, 0 }, { ITEM_SHEARS, 0, 1 });
        addShaped(3, 2, { IRON, 0, IRON, 0, IRON, 0 }, { ITEM_BUCKET, 0, 1 });
        addShaped(3, 3, { 0, IRON, 0, IRON, RED, IRON, 0, IRON, 0 }, { ITEM_COMPASS, 0, 1 });
        addShaped(3, 3, { 0, GOLD, 0, GOLD, RED, GOLD, 0, GOLD, 0 }, { ITEM_CLOCK, 0, 1 });

        // Beta 1.7 Pistons, Sticky Pistons & Redstone Devices!
        addShaped(3, 3, { PLANK, PLANK, PLANK, COBBLE, IRON, COBBLE, COBBLE, RED, COBBLE }, { 33, 0, 1 }); // Piston
        addShaped(1, 2, { ITEM_SLIME_BALL, 33 }, { 29, 0, 1 }); // Sticky Piston
        addShaped(1, 2, { RED, STICK }, { 76, 0, 1 }); // Redstone Torch
        addShaped(3, 2, { 76, RED, 76, 1, 1, 1 }, { ITEM_REPEATER_ITEM, 0, 1 }); // Redstone Repeater
        addShaped(1, 2, { STICK, COBBLE }, { 69, 0, 1 }); // Lever
        addShaped(1, 2, { 1, 1 }, { 77, 0, 1 }); // Stone Button
        addShaped(2, 1, { 1, 1 }, { 70, 0, 1 }); // Stone Pressure Plate
        addShaped(2, 1, { PLANK, PLANK }, { 72, 0, 1 }); // Wooden Pressure Plate
        addShaped(3, 3, { ITEM_GUNPOWDER, 12, ITEM_GUNPOWDER, 12, ITEM_GUNPOWDER, 12, ITEM_GUNPOWDER, 12, ITEM_GUNPOWDER }, { 46, 0, 1 }); // TNT

        // Building Blocks, Stairs, Slabs, Doors, Trapdoors, Fences & Rails
        addShaped(3, 3, { PLANK, 0, 0, PLANK, PLANK, 0, PLANK, PLANK, PLANK }, { 53, 0, 4 }); // Wooden Stairs
        addShaped(3, 3, { COBBLE, 0, 0, COBBLE, COBBLE, 0, COBBLE, COBBLE, COBBLE }, { 67, 0, 4 }); // Cobblestone Stairs
        addShaped(3, 1, { 1, 1, 1 }, { 44, 0, 3 }); // Stone Slab
        addShaped(3, 1, { 24, 24, 24 }, { 44, 1, 3 }); // Sandstone Slab
        addShaped(3, 1, { PLANK, PLANK, PLANK }, { 44, 2, 3 }); // Wooden Slab
        addShaped(3, 1, { COBBLE, COBBLE, COBBLE }, { 44, 3, 3 }); // Cobblestone Slab
        addShaped(2, 3, { PLANK, PLANK, PLANK, PLANK, PLANK, PLANK }, { ITEM_WOOD_DOOR, 0, 1 });
        addShaped(2, 3, { IRON, IRON, IRON, IRON, IRON, IRON }, { ITEM_IRON_DOOR, 0, 1 });
        addShaped(3, 2, { PLANK, PLANK, PLANK, PLANK, PLANK, PLANK }, { 96, 0, 2 }); // Trapdoor
        addShaped(3, 2, { STICK, STICK, STICK, STICK, STICK, STICK }, { 85, 0, 2 }); // Fence
        addShaped(3, 3, { STICK, 0, STICK, STICK, STICK, STICK, STICK, 0, STICK }, { 65, 0, 2 }); // Ladder
        addShaped(3, 3, { PLANK, PLANK, PLANK, PLANK, PLANK, PLANK, 0, STICK, 0 }, { ITEM_SIGN, 0, 1 });
        addShaped(3, 3, { IRON, 0, IRON, IRON, STICK, IRON, IRON, 0, IRON }, { 66, 0, 16 }); // Rails
        addShaped(3, 3, { GOLD, 0, GOLD, GOLD, STICK, GOLD, GOLD, RED, GOLD }, { 27, 0, 6 }); // Powered Rails
        addShaped(3, 3, { IRON, 0, IRON, IRON, 70, IRON, IRON, RED, IRON }, { 28, 0, 6 }); // Detector Rails

        // Storage Blocks & Compressed Minerals
        addShaped(3, 3, { IRON, IRON, IRON, IRON, IRON, IRON, IRON, IRON, IRON }, { 42, 0, 1 });
        addShapeless({ 42 }, { IRON, 0, 9 });
        addShaped(3, 3, { GOLD, GOLD, GOLD, GOLD, GOLD, GOLD, GOLD, GOLD, GOLD }, { 41, 0, 1 });
        addShapeless({ 41 }, { GOLD, 0, 9 });
        addShaped(3, 3, { DIAM, DIAM, DIAM, DIAM, DIAM, DIAM, DIAM, DIAM, DIAM }, { 57, 0, 1 });
        addShapeless({ 57 }, { DIAM, 0, 9 });
        addShaped(2, 2, { 12, 12, 12, 12 }, { 24, 0, 1 }); // Sandstone
        addShaped(2, 2, { ITEM_BRICK_INGOT, ITEM_BRICK_INGOT, ITEM_BRICK_INGOT, ITEM_BRICK_INGOT }, { 45, 0, 1 });
        addShaped(2, 2, { ITEM_CLAY_BALL, ITEM_CLAY_BALL, ITEM_CLAY_BALL, ITEM_CLAY_BALL }, { 82, 0, 1 });
        addShaped(2, 2, { ITEM_SNOWBALL, ITEM_SNOWBALL, ITEM_SNOWBALL, ITEM_SNOWBALL }, { 80, 0, 1 });
        addShaped(2, 2, { ITEM_GLOWSTONE_DUST, ITEM_GLOWSTONE_DUST, ITEM_GLOWSTONE_DUST, ITEM_GLOWSTONE_DUST }, { 89, 0, 1 });
        addShaped(2, 2, { ITEM_STRING, ITEM_STRING, ITEM_STRING, ITEM_STRING }, { 35, 0, 1 }); // White Wool

        // Books, Bookshelves, Bed, Boat, Minecart, Food & Cake
        addShaped(3, 1, { ITEM_REEDS, ITEM_REEDS, ITEM_REEDS }, { ITEM_PAPER, 0, 3 });
        addShaped(1, 3, { ITEM_PAPER, ITEM_PAPER, ITEM_PAPER }, { ITEM_BOOK, 0, 1 });
        addShaped(3, 3, { PLANK, PLANK, PLANK, ITEM_BOOK, ITEM_BOOK, ITEM_BOOK, PLANK, PLANK, PLANK }, { 47, 0, 1 });
        addShaped(3, 2, { 35, 35, 35, PLANK, PLANK, PLANK }, { ITEM_BED_ITEM, 0, 1 });
        addShaped(3, 2, { PLANK, 0, PLANK, PLANK, PLANK, PLANK }, { ITEM_BOAT, 0, 1 });
        addShaped(3, 2, { IRON, 0, IRON, IRON, IRON, IRON }, { ITEM_MINECART, 0, 1 });
        addShaped(3, 1, { ITEM_WHEAT, ITEM_WHEAT, ITEM_WHEAT }, { ITEM_BREAD, 0, 1 });
        addShaped(3, 2, { PLANK, 0, PLANK, 0, PLANK, 0 }, { ITEM_BOWL, 0, 4 });
        addShapeless({ ITEM_BOWL, 39, 40 }, { ITEM_MUSHROOM_STEW, 0, 1 });
        addShapeless({ ITEM_REEDS }, { ITEM_SUGAR, 0, 1 });
        addShaped(3, 3, { ITEM_MILK_BUCKET, ITEM_MILK_BUCKET, ITEM_MILK_BUCKET,
                          ITEM_SUGAR, ITEM_EGG, ITEM_SUGAR,
                          ITEM_WHEAT, ITEM_WHEAT, ITEM_WHEAT }, { ITEM_CAKE_ITEM, 0, 1 });
    }

    void buildTooManyItemsCatalog() {
        tmiCatalog.clear();
        const uint16_t blocks[] = {
            1, 2, 3, 4, 5, 17, 18, 12, 13, 24, 45, 48, 49, 20, 47, 80, 79, 82,
            14, 15, 16, 21, 56, 73, 41, 42, 57, 22,
            58, 61, 54, 84, 25, 23, 33, 29, 46, 50, 76, 55, 93, 69, 77, 70, 72,
            53, 67, 44, 85, 96, 65, 66, 27, 28,
            86, 91, 87, 88, 89, 81, 37, 38, 39, 40, 6, 30, 19, 7
        };
        for (uint16_t b : blocks) tmiCatalog.push_back({ b, 0, 64 });

        for (uint16_t m = 0; m < 16; ++m) tmiCatalog.push_back({ 35, m, 64 });
        tmiCatalog.push_back({ 17, 1, 64 });
        tmiCatalog.push_back({ 17, 2, 64 });

        const uint16_t items[] = {
            ITEM_DIAMOND_SWORD, ITEM_DIAMOND_PICKAXE, ITEM_DIAMOND_AXE, ITEM_DIAMOND_SHOVEL, ITEM_DIAMOND_HOE,
            ITEM_IRON_SWORD, ITEM_IRON_PICKAXE, ITEM_IRON_AXE, ITEM_IRON_SHOVEL,
            ITEM_BOW, ITEM_ARROW, ITEM_FLINT_AND_STEEL, ITEM_SHEARS,
            ITEM_DIAMOND_HELMET, ITEM_DIAMOND_CHEST, ITEM_DIAMOND_LEGS, ITEM_DIAMOND_BOOTS,
            ITEM_IRON_HELMET, ITEM_IRON_CHEST, ITEM_IRON_LEGS, ITEM_IRON_BOOTS,
            ITEM_WATER_BUCKET, ITEM_LAVA_BUCKET, ITEM_BUCKET, ITEM_COMPASS, ITEM_CLOCK,
            ITEM_WOOD_DOOR, ITEM_IRON_DOOR, ITEM_BED_ITEM, ITEM_CAKE_ITEM, ITEM_SIGN,
            ITEM_PORKCHOP_COOKED, ITEM_BREAD, ITEM_MUSHROOM_STEW, ITEM_GOLDEN_APPLE, ITEM_APPLE,
            ITEM_COAL, ITEM_IRON_INGOT, ITEM_GOLD_INGOT, ITEM_DIAMOND, ITEM_REDSTONE, ITEM_STICK
        };
        for (uint16_t it : items) tmiCatalog.push_back({ it, 0, 1 });
    }
};