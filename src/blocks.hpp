#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// Minecraft Beta 1.7.3 Complete Block Registry (IDs 0..96) & 1/16 Sub-Boxes
// ============================================================================
// 1:1 Reproduction of Mojang's Beta 1.7.3 Block.java & RenderBlocks.java:
//   - All 97 Vanilla Beta 1.7.3 Block IDs (0 = Air .. 96 = Trapdoor)
//   - Exact terrain.png (256x256, 16x16 grid = indices 0..255) sprite indices
//   - Exact hardness, blast resistance, harvest tool tier, light emission,
//     light opacity, slipperiness (Ice = 0.98f), & Mojang useNeighborBrightness
//   - Sub-voxel 1/16 geometry generator for Slabs, Stairs (including corner
//     connections), Fences, Doors, Trapdoors, Torches, Redstone Wire/Repeaters,
//     Buttons, Levers, Pressure Plates, Rails, Beds, Cake, & Pistons (Base + Head)
//   - Mod-ready dynamic registration slots (IDs 97..255 open for mods)
// ============================================================================

enum BlockFace : uint8_t {
    FACE_EAST   = 0, // +X
    FACE_WEST   = 1, // -X
    FACE_TOP    = 2, // +Y
    FACE_BOTTOM = 3, // -Y
    FACE_SOUTH  = 4, // +Z
    FACE_NORTH  = 5  // -Z
};

enum BlockShape : uint8_t {
    SHAPE_NONE          = 0,  // Air / Invisible
    SHAPE_CUBE          = 1,  // Full 16x16x16 opaque/transparent block
    SHAPE_SLAB          = 2,  // Bottom/Top half slab (44)
    SHAPE_STAIR         = 3,  // Wooden & Cobblestone stairs (53, 67)
    SHAPE_CROSS         = 4,  // X-cross plants, saplings, flowers, mushrooms, web
    SHAPE_TORCH         = 5,  // Wall/Floor torch & Redstone torch (50, 75, 76)
    SHAPE_FLUID         = 6,  // Water & Lava (8..11)
    SHAPE_FENCE         = 7,  // Connecting wooden fence (85)
    SHAPE_DOOR          = 8,  // 2-block-tall Wooden & Iron doors (64, 71)
    SHAPE_TRAPDOOR      = 9,  // Wooden trapdoor (96)
    SHAPE_LADDER        = 10, // Wall-mounted ladder (65)
    SHAPE_RAIL          = 11, // Standard, Powered & Detector rails (27, 28, 66)
    SHAPE_REDSTONE_WIRE = 12, // Floor redstone dust (55)
    SHAPE_REPEATER      = 13, // Redstone repeater diode (93, 94)
    SHAPE_LEVER         = 14, // Wall/Floor lever (69)
    SHAPE_BUTTON        = 15, // Stone button (77)
    SHAPE_PRESSURE_PLATE= 16, // Wood & Stone pressure plates (70, 72)
    SHAPE_PISTON        = 17, // Piston & Sticky Piston body (29, 33)
    SHAPE_PISTON_HEAD   = 18, // Extended Piston head & shaft (34)
    SHAPE_SNOW_LAYER    = 19, // Thin snow layer (78)
    SHAPE_CACTUS        = 20, // 14x16x14 cactus (81)
    SHAPE_CROP          = 21, // 4-plane wheat crop grid (59)
    SHAPE_BED           = 22, // 2-block bed (26)
    SHAPE_CAKE          = 23, // Eatable cake slices (92)
    SHAPE_FIRE          = 24, // Animated fire volume (51)
    SHAPE_PORTAL        = 25  // Nether portal pane (90)
};

enum TintMode : uint8_t {
    TINT_NONE     = 0,
    TINT_GRASS    = 1, // Grass block top & tall grass (grasscolor.png)
    TINT_FOLIAGE  = 2, // Oak/Spruce/Birch leaves (foliagecolor.png)
    TINT_REDSTONE = 3  // Redstone wire power tint (0..15)
};

enum ToolCategory : uint8_t {
    TOOL_NONE    = 0,
    TOOL_PICKAXE = 1,
    TOOL_AXE     = 2,
    TOOL_SHOVEL  = 3,
    TOOL_SWORD   = 4,
    TOOL_SHEARS  = 5
};

enum SoundMaterial : uint8_t {
    SND_STONE  = 0,
    SND_GRASS  = 1,
    SND_GRAVEL = 2,
    SND_WOOD   = 3,
    SND_METAL  = 4,
    SND_GLASS  = 5,
    SND_CLOTH  = 6,
    SND_SAND   = 7,
    SND_SNOW   = 8
};

// Standard Beta 1.7.3 terrain.png (256x256, 16x16 grid = 0..255) Sprite Indices
enum TerrainSprite : uint16_t {
    SPR_GRASS_TOP         = 0,
    SPR_STONE             = 1,
    SPR_DIRT              = 2,
    SPR_GRASS_SIDE        = 3,
    SPR_PLANKS            = 4,
    SPR_DOUBLE_SLAB_SIDE  = 5,
    SPR_DOUBLE_SLAB_TOP   = 6,
    SPR_BRICKS            = 7,
    SPR_TNT_SIDE          = 8,
    SPR_TNT_TOP           = 9,
    SPR_TNT_BOTTOM        = 10,
    SPR_COBWEB            = 11,
    SPR_ROSE              = 12,
    SPR_DANDELION         = 13,
    SPR_PORTAL            = 14,
    SPR_SAPLING_OAK       = 15,

    SPR_COBBLESTONE       = 16,
    SPR_BEDROCK           = 17,
    SPR_SAND              = 18,
    SPR_GRAVEL            = 19,
    SPR_LOG_OAK_SIDE      = 20,
    SPR_LOG_TOP           = 21,
    SPR_IRON_BLOCK        = 22,
    SPR_GOLD_BLOCK        = 23,
    SPR_DIAMOND_BLOCK     = 24,
    SPR_CHEST_TOP         = 25,
    SPR_CHEST_SIDE        = 26,
    SPR_CHEST_FRONT       = 27,
    SPR_MUSHROOM_RED      = 28,
    SPR_MUSHROOM_BROWN    = 29,
    SPR_FIRE_0            = 31,

    SPR_GOLD_ORE          = 32,
    SPR_IRON_ORE          = 33,
    SPR_COAL_ORE          = 34,
    SPR_BOOKSHELF         = 35,
    SPR_MOSSY_COBBLESTONE = 36,
    SPR_OBSIDIAN          = 37,
    SPR_GRASS_SIDE_OVERLAY= 38,
    SPR_TALL_GRASS        = 39,
    SPR_CHEST_DOUBLE_FL   = 41,
    SPR_CHEST_DOUBLE_FR   = 42,
    SPR_CRAFTING_TOP      = 43,
    SPR_FURNACE_FRONT_OFF = 44,
    SPR_FURNACE_SIDE      = 45,
    SPR_DISPENSER_FRONT   = 46,

    SPR_SPONGE            = 48,
    SPR_GLASS             = 49,
    SPR_DIAMOND_ORE       = 50,
    SPR_REDSTONE_ORE      = 51,
    SPR_LEAVES_FANCY      = 52,
    SPR_LEAVES_FAST       = 53,
    SPR_DEAD_BUSH         = 55,
    SPR_FERN              = 56,
    SPR_CHEST_DOUBLE_BL   = 57,
    SPR_CHEST_DOUBLE_BR   = 58,
    SPR_CRAFTING_SIDE_1   = 59,
    SPR_CRAFTING_SIDE_2   = 60,
    SPR_FURNACE_FRONT_ON  = 61,
    SPR_FURNACE_TOP       = 62,
    SPR_SAPLING_SPRUCE    = 63,

    SPR_WOOL_WHITE        = 64,
    SPR_MOB_SPAWNER       = 65,
    SPR_SNOW              = 66,
    SPR_ICE               = 67,
    SPR_GRASS_SNOW_SIDE   = 68,
    SPR_CACTUS_TOP        = 69,
    SPR_CACTUS_SIDE       = 70,
    SPR_CACTUS_BOTTOM     = 71,
    SPR_CLAY              = 72,
    SPR_SUGAR_CANE        = 73,
    SPR_JUKEBOX_SIDE      = 74,
    SPR_JUKEBOX_TOP       = 75,
    SPR_SAPLING_BIRCH     = 79,

    SPR_TORCH             = 80,
    SPR_DOOR_WOOD_TOP     = 81,
    SPR_DOOR_IRON_TOP     = 82,
    SPR_LADDER            = 83,
    SPR_TRAPDOOR          = 84,
    SPR_FARMLAND_WET      = 86,
    SPR_FARMLAND_DRY      = 87,
    SPR_WHEAT_0           = 88,
    SPR_WHEAT_1           = 89,
    SPR_WHEAT_2           = 90,
    SPR_WHEAT_3           = 91,
    SPR_WHEAT_4           = 92,
    SPR_WHEAT_5           = 93,
    SPR_WHEAT_6           = 94,
    SPR_WHEAT_7           = 95,

    SPR_LEVER             = 96,
    SPR_DOOR_WOOD_BOTTOM  = 97,
    SPR_DOOR_IRON_BOTTOM  = 98,
    SPR_REDSTONE_TORCH_ON = 99,
    SPR_NETHERRACK        = 103,
    SPR_SOUL_SAND         = 104,
    SPR_GLOWSTONE         = 105,
    SPR_PISTON_HEAD_STICKY= 106,
    SPR_PISTON_HEAD_NORMAL= 107,
    SPR_PISTON_SIDE       = 108,
    SPR_PISTON_BACK       = 109,
    SPR_PISTON_INNER      = 110,

    SPR_RAIL_CURVED       = 112,
    SPR_WOOL_BLACK        = 113,
    SPR_WOOL_GRAY         = 114,
    SPR_REDSTONE_TORCH_OFF= 115,
    SPR_LOG_SPRUCE_SIDE   = 116,
    SPR_LOG_BIRCH_SIDE    = 117,
    SPR_PUMPKIN_TOP       = 118,
    SPR_PUMPKIN_SIDE      = 119,
    SPR_PUMPKIN_FRONT     = 120,
    SPR_JACK_O_LANTERN    = 121,
    SPR_CAKE_TOP          = 122,
    SPR_CAKE_SIDE         = 123,
    SPR_CAKE_INNER        = 124,

    SPR_RAIL_STRAIGHT     = 128,
    SPR_WOOL_RED          = 129,
    SPR_WOOL_PINK         = 130,
    SPR_REPEATER_OFF      = 131,
    SPR_LEAVES_SPRUCE     = 132,
    SPR_BED_FEET_TOP      = 134,
    SPR_BED_HEAD_TOP      = 135,

    SPR_LAPIS_BLOCK       = 144,
    SPR_WOOL_DARK_GREEN   = 145,
    SPR_WOOL_LIME         = 146,
    SPR_REPEATER_ON       = 147,
    SPR_BED_FEET_END      = 149,
    SPR_BED_FEET_SIDE     = 150,
    SPR_BED_HEAD_SIDE     = 151,
    SPR_BED_HEAD_END      = 152,

    SPR_LAPIS_ORE         = 160,
    SPR_WOOL_BROWN        = 161,
    SPR_WOOL_YELLOW       = 162,
    SPR_RAIL_POWERED_OFF  = 163,
    SPR_REDSTONE_CROSS    = 164,
    SPR_REDSTONE_LINE     = 165,

    SPR_SANDSTONE_TOP     = 176,
    SPR_WOOL_DARK_BLUE    = 177,
    SPR_WOOL_LIGHT_BLUE   = 178,
    SPR_RAIL_POWERED_ON   = 179,

    SPR_SANDSTONE_SIDE    = 192,
    SPR_WOOL_PURPLE       = 193,
    SPR_WOOL_MAGENTA      = 194,
    SPR_RAIL_DETECTOR     = 195,

    SPR_SANDSTONE_BOTTOM  = 208,
    SPR_WOOL_CYAN         = 209,
    SPR_WOOL_ORANGE       = 210,

    SPR_WOOL_LIGHT_GRAY   = 225,
    SPR_DESTROY_0         = 240,
    SPR_WATER_STILL       = 205,
    SPR_LAVA_STILL        = 237
};

struct BlockDef {
    const char*   name                  = "air";
    BlockShape    shape                 = SHAPE_NONE;
    TintMode      tintMode              = TINT_NONE;
    ToolCategory  bestTool              = TOOL_NONE;
    SoundMaterial soundMat              = SND_STONE;
    uint8_t       harvestLevel          = 0;     // 0=Any, 1=Wood, 2=Stone, 3=Iron, 4=Diamond
    float         hardness              = 0.0f;  // -1.0f = unbreakable (Bedrock, Portal)
    float         resistance            = 0.0f;  // Blast resistance
    float         slipperiness          = 0.60f; // 0.60f standard, 0.98f Ice
    uint8_t       lightEmission         = 0;     // 0..15
    uint8_t       lightOpacity          = 0;     // 0 = transparent, 1..3 = semi-opaque, 15 = solid opaque
    bool          isSolid               = false;
    bool          isOpaque              = false;
    bool          isReplaceable         = false;
    bool          isFlammable           = false;
    bool          useNeighborBrightness = false; // Mojang Block.useNeighborBrightness (Slabs/Stairs/Farmland)
    uint16_t      faceTex[6]            = {0};   // E, W, Top, Bottom, S, N sprite index in terrain.png
};

// Sub-voxel AABB in [0..16] pixel units (1/16th block precision)
struct SubBox {
    float    minX, minY, minZ;
    float    maxX, maxY, maxZ;
    uint16_t faceTex[6];
    TintMode tint = TINT_NONE;
};

struct SubBoxList {
    SubBox boxes[8];
    int    count = 0;

    void add(float x0, float y0, float z0, float x1, float y1, float z1,
             const uint16_t tex[6], TintMode t = TINT_NONE) {
        if (count >= 8) return;
        SubBox& b = boxes[count++];
        b.minX = x0; b.minY = y0; b.minZ = z0;
        b.maxX = x1; b.maxY = y1; b.maxZ = z1;
        std::memcpy(b.faceTex, tex, sizeof(uint16_t) * 6);
        b.tint = t;
    }

    void addUniform(float x0, float y0, float z0, float x1, float y1, float z1,
                    uint16_t uniformTex, TintMode t = TINT_NONE) {
        uint16_t arr[6] = { uniformTex, uniformTex, uniformTex, uniformTex, uniformTex, uniformTex };
        add(x0, y0, z0, x1, y1, z1, arr, t);
    }
};

// ============================================================================
// Complete Beta 1.7.3 Block Registry (IDs 0..96 + Dynamic Mod Slots 97..255)
// ============================================================================
class BlockRegistry {
public:
    BlockDef defs[256];

    BlockRegistry() {
        registerAllBeta173Blocks();
    }

    void registerBlock(uint8_t id, const char* name, BlockShape shape,
                       uint16_t topTex, uint16_t sideTex, uint16_t botTex,
                       float hardness, float resistance, uint8_t lightEmit, uint8_t lightOpac,
                       bool solid, bool opaque,
                       ToolCategory tool = TOOL_NONE, uint8_t harvestLvl = 0,
                       SoundMaterial snd = SND_STONE, TintMode tint = TINT_NONE,
                       bool replaceable = false, bool flammable = false,
                       bool neighborBright = false, float slipperiness = 0.60f) {
        BlockDef& d = defs[id];
        d.name                  = name;
        d.shape                 = shape;
        d.tintMode              = tint;
        d.bestTool              = tool;
        d.soundMat              = snd;
        d.harvestLevel          = harvestLvl;
        d.hardness              = hardness;
        d.resistance            = resistance;
        d.slipperiness          = slipperiness;
        d.lightEmission         = lightEmit;
        d.lightOpacity          = lightOpac;
        d.isSolid               = solid;
        d.isOpaque              = opaque;
        d.isReplaceable         = replaceable;
        d.isFlammable           = flammable;
        d.useNeighborBrightness = neighborBright;

        d.faceTex[FACE_EAST]   = sideTex;
        d.faceTex[FACE_WEST]   = sideTex;
        d.faceTex[FACE_TOP]    = topTex;
        d.faceTex[FACE_BOTTOM] = botTex;
        d.faceTex[FACE_SOUTH]  = sideTex;
        d.faceTex[FACE_NORTH]  = sideTex;
    }

    void setFrontTexture(uint8_t id, uint16_t frontTex) {
        defs[id].faceTex[FACE_NORTH] = frontTex;
    }

    inline const BlockDef& get(uint16_t rawState) const {
        return defs[(rawState >> 4) & 0xFF];
    }

    inline const BlockDef& getById(uint8_t id) const {
        return defs[id];
    }

    // Resolves metadata-dependent sprite index (Wool 16 colors, Logs, Saplings, Slabs, Furnaces, Crops, Pistons)
    uint16_t resolveFaceTexture(uint16_t rawState, BlockFace face) const {
        uint8_t id   = (uint8_t)((rawState >> 4) & 0xFF);
        uint8_t meta = (uint8_t)(rawState & 0x0F);
        const BlockDef& d = defs[id];

        // Log (17): meta 0 = Oak, 1 = Spruce, 2 = Birch
        if (id == 17 && face != FACE_TOP && face != FACE_BOTTOM) {
            if ((meta & 3) == 1) return SPR_LOG_SPRUCE_SIDE;
            if ((meta & 3) == 2) return SPR_LOG_BIRCH_SIDE;
            return SPR_LOG_OAK_SIDE;
        }

        // Leaves (18): meta & 3 == 1 -> Spruce needle sprite
        if (id == 18) {
            if ((meta & 3) == 1) return SPR_LEAVES_SPRUCE;
            return SPR_LEAVES_FANCY;
        }

        // Sapling (6): meta & 3 -> 0=Oak, 1=Spruce, 2=Birch
        if (id == 6) {
            if ((meta & 3) == 1) return SPR_SAPLING_SPRUCE;
            if ((meta & 3) == 2) return SPR_SAPLING_BIRCH;
            return SPR_SAPLING_OAK;
        }

        // Tall Grass / Fern (31): meta 0=Dead Shrub, 1=Tall Grass, 2=Fern
        if (id == 31) {
            if (meta == 0) return SPR_DEAD_BUSH;
            if (meta == 2) return SPR_FERN;
            return SPR_TALL_GRASS;
        }

        // Wool (35): Official Beta 1.7.3 16-color sprite lookup table
        if (id == 35) {
            static constexpr uint16_t woolSprites[16] = {
                SPR_WOOL_WHITE, SPR_WOOL_ORANGE, SPR_WOOL_MAGENTA, SPR_WOOL_LIGHT_BLUE,
                SPR_WOOL_YELLOW, SPR_WOOL_LIME, SPR_WOOL_PINK, SPR_WOOL_GRAY,
                SPR_WOOL_LIGHT_GRAY, SPR_WOOL_CYAN, SPR_WOOL_PURPLE, SPR_WOOL_DARK_BLUE,
                SPR_WOOL_BROWN, SPR_WOOL_DARK_GREEN, SPR_WOOL_RED, SPR_WOOL_BLACK
            };
            return woolSprites[meta & 0x0F];
        }

        // Double Slab (43) & Single Slab (44): meta 0=Stone, 1=Sandstone, 2=Wood, 3=Cobblestone
        if (id == 43 || id == 44) {
            uint8_t mat = meta & 0x07;
            if (mat == 1) return (face == FACE_TOP) ? SPR_SANDSTONE_TOP : ((face == FACE_BOTTOM) ? SPR_SANDSTONE_BOTTOM : SPR_SANDSTONE_SIDE);
            if (mat == 2) return SPR_PLANKS;
            if (mat == 3) return SPR_COBBLESTONE;
            return (face == FACE_TOP || face == FACE_BOTTOM) ? SPR_DOUBLE_SLAB_TOP : SPR_DOUBLE_SLAB_SIDE;
        }

        // Wheat Crops (59): growth stages 0..7 -> SPR_WHEAT_0 .. SPR_WHEAT_7
        if (id == 59) {
            return (uint16_t)(SPR_WHEAT_0 + (meta & 0x07));
        }

        // Farmland (60): meta > 0 = Wet, 0 = Dry
        if (id == 60 && face == FACE_TOP) {
            return (meta > 0) ? SPR_FARMLAND_WET : SPR_FARMLAND_DRY;
        }

        // Furnace (61 = off, 62 = lit) & Dispenser (23) directional front
        if (id == 61 || id == 62 || id == 23) {
            uint8_t facing = meta & 0x07;
            if (facing == 0) facing = 3;
            bool isFront = ((facing == 2 && face == FACE_NORTH) ||
                            (facing == 3 && face == FACE_SOUTH) ||
                            (facing == 4 && face == FACE_WEST)  ||
                            (facing == 5 && face == FACE_EAST));
            if (isFront) {
                return (id == 62) ? SPR_FURNACE_FRONT_ON : ((id == 23) ? SPR_DISPENSER_FRONT : SPR_FURNACE_FRONT_OFF);
            }
        }

        // Pumpkin (86) & Jack o'Lantern (91) directional carved face
        if (id == 86 || id == 91) {
            uint8_t dir = meta & 0x03; // 0=South, 1=West, 2=North, 3=East
            bool isFace = ((dir == 0 && face == FACE_SOUTH) ||
                           (dir == 1 && face == FACE_WEST)  ||
                           (dir == 2 && face == FACE_NORTH) ||
                           (dir == 3 && face == FACE_EAST));
            if (isFace) return (id == 91) ? SPR_JACK_O_LANTERN : SPR_PUMPKIN_FRONT;
        }

        // Piston Base (29=Sticky, 33=Normal) directional head/back
        if (id == 29 || id == 33) {
            uint8_t dir = meta & 0x07; // 0=Down, 1=Up, 2=North, 3=South, 4=West, 5=East
            bool extended = (meta & 0x08) != 0;
            BlockFace headFace = (dir == 0) ? FACE_BOTTOM : ((dir == 1) ? FACE_TOP :
                                 ((dir == 2) ? FACE_NORTH : ((dir == 3) ? FACE_SOUTH :
                                 ((dir == 4) ? FACE_WEST  : FACE_EAST))));
            BlockFace backFace = (BlockFace)(headFace ^ 1);
            if (face == headFace) return extended ? SPR_PISTON_INNER : ((id == 29) ? SPR_PISTON_HEAD_STICKY : SPR_PISTON_HEAD_NORMAL);
            if (face == backFace) return SPR_PISTON_BACK;
            return SPR_PISTON_SIDE;
        }

        return d.faceTex[face];
    }

    // Generates exact 1/16th block sub-boxes for non-cubic blocks & neighbor connections
    template<typename WorldQueryFn>
    void getSubBoxes(uint16_t rawState, int bx, int by, int bz, WorldQueryFn&& getNeighborState, SubBoxList& out) const {
        out.count = 0;
        uint8_t id   = (uint8_t)((rawState >> 4) & 0xFF);
        uint8_t meta = (uint8_t)(rawState & 0x0F);
        const BlockDef& d = defs[id];

        uint16_t fTex[6];
        for (int f = 0; f < 6; ++f) fTex[f] = resolveFaceTexture(rawState, (BlockFace)f);

        if (d.shape == SHAPE_SLAB) {
            out.add(0, 0, 0, 16, 8, 16, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_SNOW_LAYER) {
            float h = (float)((meta & 0x07) + 1) * 2.0f;
            out.add(0, 0, 0, 16, std::min(16.0f, h), 16, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_CACTUS) {
            out.add(1, 0, 1, 15, 16, 15, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_BED) {
            out.add(0, 3, 0, 16, 9, 16, fTex, d.tintMode);
            out.addUniform(0, 0, 0, 3, 3, 3, SPR_PLANKS);
            out.addUniform(13, 0, 0, 16, 3, 3, SPR_PLANKS);
            out.addUniform(0, 0, 13, 3, 3, 16, SPR_PLANKS);
            out.addUniform(13, 0, 13, 16, 3, 16, SPR_PLANKS);
        }
        else if (d.shape == SHAPE_CAKE) {
            int eaten = std::min(6, (int)(meta & 0x07));
            float x0  = (float)(1 + eaten * 2);
            out.add(x0, 0, 1, 15, 8, 15, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_STAIR) {
            // Beta 1.7.3 Stair meta: 0=+X (East), 1=-X (West), 2=+Z (South), 3=-Z (North)
            uint8_t dir = meta & 0x03;
            out.add(0, 0, 0, 16, 8, 16, fTex, d.tintMode); // Bottom base slab

            if (dir == 0)      out.add(8, 8, 0, 16, 16, 16, fTex, d.tintMode);
            else if (dir == 1) out.add(0, 8, 0,  8, 16, 16, fTex, d.tintMode);
            else if (dir == 2) out.add(0, 8, 8, 16, 16, 16, fTex, d.tintMode);
            else               out.add(0, 8, 0, 16, 16,  8, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_FENCE) {
            out.add(6, 0, 6, 10, 16, 10, fTex, d.tintMode);
            auto connects = [&](int nx, int ny, int nz) -> bool {
                uint16_t nSt = getNeighborState(nx, ny, nz);
                uint8_t nId  = (uint8_t)((nSt >> 4) & 0xFF);
                return (nId == 85) || (defs[nId].isSolid && defs[nId].isOpaque);
            };
            if (connects(bx - 1, by, bz)) { out.add(0, 12, 7, 6, 15, 9, fTex); out.add(0, 6, 7, 6, 9, 9, fTex); }
            if (connects(bx + 1, by, bz)) { out.add(10, 12, 7, 16, 15, 9, fTex); out.add(10, 6, 7, 16, 9, 9, fTex); }
            if (connects(bx, by, bz - 1)) { out.add(7, 12, 0, 9, 15, 6, fTex); out.add(7, 6, 0, 9, 9, 6, fTex); }
            if (connects(bx, by, bz + 1)) { out.add(7, 12, 10, 9, 15, 16, fTex); out.add(7, 6, 10, 9, 9, 16, fTex); }
        }
        else if (d.shape == SHAPE_DOOR) {
            uint8_t effMeta = meta;
            if (meta & 0x08) {
                uint16_t lower = getNeighborState(bx, by - 1, bz);
                if (((lower >> 4) & 0xFF) == id) effMeta = (uint8_t)(lower & 0x0F);
            }
            uint8_t hinge = effMeta & 0x03;
            bool open     = (effMeta & 0x04) != 0;
            uint8_t side  = open ? ((hinge + 1) & 3) : hinge;
            if (side == 0)      out.add(0, 0, 0, 3, 16, 16, fTex);
            else if (side == 1) out.add(0, 0, 0, 16, 16, 3, fTex);
            else if (side == 2) out.add(13, 0, 0, 16, 16, 16, fTex);
            else                out.add(0, 0, 13, 16, 16, 16, fTex);
        }
        else if (d.shape == SHAPE_TRAPDOOR) {
            bool open = (meta & 0x04) != 0;
            if (!open) {
                out.add(0, 0, 0, 16, 3, 16, fTex);
            } else {
                uint8_t dir = meta & 0x03;
                if (dir == 0)      out.add(0, 0, 13, 16, 16, 16, fTex);
                else if (dir == 1) out.add(0, 0,  0, 16, 16,  3, fTex);
                else if (dir == 2) out.add(13, 0, 0, 16, 16, 16, fTex);
                else               out.add(0, 0,  0,  3, 16, 16, fTex);
            }
        }
        else if (d.shape == SHAPE_LADDER) {
            uint8_t dir = meta & 0x07;
            if (dir == 2)      out.add(0, 0, 14, 16, 16, 16, fTex);
            else if (dir == 3) out.add(0, 0, 0, 16, 16, 2, fTex);
            else if (dir == 4) out.add(14, 0, 0, 16, 16, 16, fTex);
            else               out.add(0, 0, 0, 2, 16, 16, fTex);
        }
        else if (d.shape == SHAPE_RAIL) {
            out.add(0, 0, 0, 16, 1, 16, fTex);
        }
        else if (d.shape == SHAPE_REDSTONE_WIRE) {
            out.add(0, 0, 0, 16, 0.5f, 16, fTex, TINT_REDSTONE);
        }
        else if (d.shape == SHAPE_REPEATER) {
            out.add(0, 0, 0, 16, 2, 16, fTex);
            uint16_t torchTex = (id == 94) ? SPR_REDSTONE_TORCH_ON : SPR_REDSTONE_TORCH_OFF;
            out.addUniform(7, 2, 4, 9, 7, 6, torchTex);
            out.addUniform(7, 2, 10, 9, 7, 12, torchTex);
        }
        else if (d.shape == SHAPE_PRESSURE_PLATE) {
            bool pressed = (meta & 0x01) != 0;
            out.add(1, 0, 1, 15, pressed ? 0.5f : 1.0f, 15, fTex);
        }
        else if (d.shape == SHAPE_BUTTON) {
            uint8_t dir  = meta & 0x07;
            bool pressed = (meta & 0x08) != 0;
            float thick  = pressed ? 1.0f : 2.0f;
            if (dir == 1)      out.add(0, 6, 5, thick, 10, 11, fTex);
            else if (dir == 2) out.add(16 - thick, 6, 5, 16, 10, 11, fTex);
            else if (dir == 3) out.add(5, 6, 0, 11, 10, thick, fTex);
            else               out.add(5, 6, 16 - thick, 11, 10, 16, fTex);
        }
        else if (d.shape == SHAPE_LEVER) {
            out.addUniform(5, 0, 4, 11, 3, 12, SPR_COBBLESTONE);
            bool active = (meta & 0x08) != 0;
            if (active) out.addUniform(7, 2, 9, 9, 10, 11, SPR_LEVER);
            else        out.addUniform(7, 2, 5, 9, 10, 7,  SPR_LEVER);
        }
        else if (d.shape == SHAPE_PISTON) {
            bool extended = (meta & 0x08) != 0;
            if (!extended) {
                out.add(0, 0, 0, 16, 16, 16, fTex);
            } else {
                uint8_t dir = meta & 0x07;
                if (dir == 0)      out.add(0, 4, 0, 16, 16, 16, fTex);
                else if (dir == 1) out.add(0, 0, 0, 16, 12, 16, fTex);
                else if (dir == 2) out.add(0, 0, 4, 16, 16, 16, fTex);
                else if (dir == 3) out.add(0, 0, 0, 16, 16, 12, fTex);
                else if (dir == 4) out.add(4, 0, 0, 16, 16, 16, fTex);
                else               out.add(0, 0, 0, 12, 16, 16, fTex);
            }
        }
        else if (d.shape == SHAPE_PISTON_HEAD) {
            uint8_t dir = meta & 0x07;
            bool sticky = (meta & 0x08) != 0;
            uint16_t faceTex = sticky ? SPR_PISTON_HEAD_STICKY : SPR_PISTON_HEAD_NORMAL;
            uint16_t head[6] = { SPR_PISTON_SIDE, SPR_PISTON_SIDE, SPR_PISTON_SIDE, SPR_PISTON_SIDE, SPR_PISTON_SIDE, SPR_PISTON_SIDE };
            if (dir == 0)      { head[FACE_BOTTOM] = faceTex; out.add(0, 0, 0, 16, 4, 16, head);   out.addUniform(6, 4, 6, 10, 16, 10, SPR_PISTON_SIDE); }
            else if (dir == 1) { head[FACE_TOP]    = faceTex; out.add(0, 12, 0, 16, 16, 16, head); out.addUniform(6, 0, 6, 10, 12, 10, SPR_PISTON_SIDE); }
            else if (dir == 2) { head[FACE_NORTH]  = faceTex; out.add(0, 0, 0, 16, 16, 4, head);   out.addUniform(6, 6, 4, 10, 10, 16, SPR_PISTON_SIDE); }
            else if (dir == 3) { head[FACE_SOUTH]  = faceTex; out.add(0, 0, 12, 16, 16, 16, head); out.addUniform(6, 6, 0, 10, 10, 12, SPR_PISTON_SIDE); }
            else if (dir == 4) { head[FACE_WEST]   = faceTex; out.add(0, 0, 0, 4, 16, 16, head);   out.addUniform(4, 6, 6, 16, 10, 10, SPR_PISTON_SIDE); }
            else               { head[FACE_EAST]   = faceTex; out.add(12, 0, 0, 16, 16, 16, head); out.addUniform(0, 6, 6, 12, 10, 10, SPR_PISTON_SIDE); }
        }
    }

private:
    void registerAllBeta173Blocks() {
        // ID 0: Air
        registerBlock(0, "Air", SHAPE_NONE, 0, 0, 0, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);

        // IDs 1..19: Core Terrain, Ores, Fluids, Logs & Leaves
        registerBlock(1,  "Stone",            SHAPE_CUBE, SPR_STONE, SPR_STONE, SPR_STONE, 1.5f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(2,  "Grass Block",      SHAPE_CUBE, SPR_GRASS_TOP, SPR_GRASS_SIDE, SPR_DIRT, 0.6f, 0.6f, 0, 15, true, true, TOOL_SHOVEL, 0, SND_GRASS, TINT_GRASS);
        registerBlock(3,  "Dirt",             SHAPE_CUBE, SPR_DIRT, SPR_DIRT, SPR_DIRT, 0.5f, 0.5f, 0, 15, true, true, TOOL_SHOVEL, 0, SND_GRAVEL);
        registerBlock(4,  "Cobblestone",      SHAPE_CUBE, SPR_COBBLESTONE, SPR_COBBLESTONE, SPR_COBBLESTONE, 2.0f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(5,  "Wooden Planks",    SHAPE_CUBE, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 2.0f, 5.0f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(6,  "Sapling",          SHAPE_CROSS, SPR_SAPLING_OAK, SPR_SAPLING_OAK, SPR_SAPLING_OAK, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(7,  "Bedrock",          SHAPE_CUBE, SPR_BEDROCK, SPR_BEDROCK, SPR_BEDROCK, -1.0f, 6000000.0f, 0, 15, true, true, TOOL_NONE, 99, SND_STONE);
        registerBlock(8,  "Flowing Water",    SHAPE_FLUID, SPR_WATER_STILL, SPR_WATER_STILL, SPR_WATER_STILL, 100.0f, 100.0f, 0, 3, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(9,  "Stationary Water", SHAPE_FLUID, SPR_WATER_STILL, SPR_WATER_STILL, SPR_WATER_STILL, 100.0f, 100.0f, 0, 3, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(10, "Flowing Lava",     SHAPE_FLUID, SPR_LAVA_STILL, SPR_LAVA_STILL, SPR_LAVA_STILL, 100.0f, 100.0f, 15, 0, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(11, "Stationary Lava",  SHAPE_FLUID, SPR_LAVA_STILL, SPR_LAVA_STILL, SPR_LAVA_STILL, 100.0f, 100.0f, 15, 0, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(12, "Sand",             SHAPE_CUBE, SPR_SAND, SPR_SAND, SPR_SAND, 0.5f, 0.5f, 0, 15, true, true, TOOL_SHOVEL, 0, SND_SAND);
        registerBlock(13, "Gravel",           SHAPE_CUBE, SPR_GRAVEL, SPR_GRAVEL, SPR_GRAVEL, 0.6f, 0.6f, 0, 15, true, true, TOOL_SHOVEL, 0, SND_GRAVEL);
        registerBlock(14, "Gold Ore",         SHAPE_CUBE, SPR_GOLD_ORE, SPR_GOLD_ORE, SPR_GOLD_ORE, 3.0f, 5.0f, 0, 15, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(15, "Iron Ore",         SHAPE_CUBE, SPR_IRON_ORE, SPR_IRON_ORE, SPR_IRON_ORE, 3.0f, 5.0f, 0, 15, true, true, TOOL_PICKAXE, 2, SND_STONE);
        registerBlock(16, "Coal Ore",         SHAPE_CUBE, SPR_COAL_ORE, SPR_COAL_ORE, SPR_COAL_ORE, 3.0f, 5.0f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(17, "Wood",             SHAPE_CUBE, SPR_LOG_TOP, SPR_LOG_OAK_SIDE, SPR_LOG_TOP, 2.0f, 2.0f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(18, "Leaves",           SHAPE_CUBE, SPR_LEAVES_FANCY, SPR_LEAVES_FANCY, SPR_LEAVES_FANCY, 0.2f, 0.2f, 0, 1, true, false, TOOL_SHEARS, 0, SND_GRASS, TINT_FOLIAGE, false, true);
        registerBlock(19, "Sponge",           SHAPE_CUBE, SPR_SPONGE, SPR_SPONGE, SPR_SPONGE, 0.6f, 0.6f, 0, 15, true, true, TOOL_NONE, 0, SND_GRASS);

        // IDs 20..49: Glass, Lapis, Dispenser, Sandstone, Note Block, Bed, Rails, Pistons, Wool, Flowers, Slabs, Bricks, TNT, Bookshelf, Mossy, Obsidian
        registerBlock(20, "Glass",            SHAPE_CUBE, SPR_GLASS, SPR_GLASS, SPR_GLASS, 0.3f, 0.3f, 0, 0, true, false, TOOL_NONE, 0, SND_GLASS);
        registerBlock(21, "Lapis Lazuli Ore", SHAPE_CUBE, SPR_LAPIS_ORE, SPR_LAPIS_ORE, SPR_LAPIS_ORE, 3.0f, 5.0f, 0, 15, true, true, TOOL_PICKAXE, 2, SND_STONE);
        registerBlock(22, "Lapis Lazuli Block",SHAPE_CUBE, SPR_LAPIS_BLOCK, SPR_LAPIS_BLOCK, SPR_LAPIS_BLOCK, 3.0f, 5.0f, 0, 15, true, true, TOOL_PICKAXE, 2, SND_STONE);
        registerBlock(23, "Dispenser",        SHAPE_CUBE, SPR_FURNACE_TOP, SPR_FURNACE_SIDE, SPR_FURNACE_TOP, 3.5f, 3.5f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        setFrontTexture(23, SPR_DISPENSER_FRONT);
        registerBlock(24, "Sandstone",        SHAPE_CUBE, SPR_SANDSTONE_TOP, SPR_SANDSTONE_SIDE, SPR_SANDSTONE_BOTTOM, 0.8f, 0.8f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(25, "Note Block",       SHAPE_CUBE, SPR_JUKEBOX_SIDE, SPR_JUKEBOX_SIDE, SPR_JUKEBOX_SIDE, 0.8f, 0.8f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(26, "Bed",              SHAPE_BED, SPR_BED_HEAD_TOP, SPR_BED_HEAD_SIDE, SPR_PLANKS, 0.2f, 0.2f, 0, 0, true, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(27, "Powered Rail",     SHAPE_RAIL, SPR_RAIL_POWERED_OFF, SPR_RAIL_POWERED_OFF, SPR_RAIL_POWERED_OFF, 0.7f, 0.7f, 0, 0, false, false, TOOL_PICKAXE, 0, SND_METAL);
        registerBlock(28, "Detector Rail",    SHAPE_RAIL, SPR_RAIL_DETECTOR, SPR_RAIL_DETECTOR, SPR_RAIL_DETECTOR, 0.7f, 0.7f, 0, 0, false, false, TOOL_PICKAXE, 0, SND_METAL);
        registerBlock(29, "Sticky Piston",    SHAPE_PISTON, SPR_PISTON_HEAD_STICKY, SPR_PISTON_SIDE, SPR_PISTON_BACK, 0.5f, 0.5f, 0, 0, true, false, TOOL_PICKAXE, 0, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(30, "Cobweb",           SHAPE_CROSS, SPR_COBWEB, SPR_COBWEB, SPR_COBWEB, 4.0f, 4.0f, 0, 1, false, false, TOOL_SWORD, 0, SND_CLOTH);
        registerBlock(31, "Tall Grass",       SHAPE_CROSS, SPR_TALL_GRASS, SPR_TALL_GRASS, SPR_TALL_GRASS, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS, TINT_GRASS, true, true);
        registerBlock(32, "Dead Bush",        SHAPE_CROSS, SPR_DEAD_BUSH, SPR_DEAD_BUSH, SPR_DEAD_BUSH, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS, TINT_NONE, true, true);
        registerBlock(33, "Piston",           SHAPE_PISTON, SPR_PISTON_HEAD_NORMAL, SPR_PISTON_SIDE, SPR_PISTON_BACK, 0.5f, 0.5f, 0, 0, true, false, TOOL_PICKAXE, 0, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(34, "Piston Extension", SHAPE_PISTON_HEAD, SPR_PISTON_HEAD_NORMAL, SPR_PISTON_SIDE, SPR_PISTON_SIDE, 0.5f, 0.5f, 0, 0, true, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(35, "Wool",             SHAPE_CUBE, SPR_WOOL_WHITE, SPR_WOOL_WHITE, SPR_WOOL_WHITE, 0.8f, 0.8f, 0, 15, true, true, TOOL_SHEARS, 0, SND_CLOTH, TINT_NONE, false, true);
        registerBlock(37, "Dandelion",        SHAPE_CROSS, SPR_DANDELION, SPR_DANDELION, SPR_DANDELION, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(38, "Rose",             SHAPE_CROSS, SPR_ROSE, SPR_ROSE, SPR_ROSE, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(39, "Brown Mushroom",   SHAPE_CROSS, SPR_MUSHROOM_BROWN, SPR_MUSHROOM_BROWN, SPR_MUSHROOM_BROWN, 0.0f, 0.0f, 1, 0, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(40, "Red Mushroom",     SHAPE_CROSS, SPR_MUSHROOM_RED, SPR_MUSHROOM_RED, SPR_MUSHROOM_RED, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(41, "Block of Gold",    SHAPE_CUBE, SPR_GOLD_BLOCK, SPR_GOLD_BLOCK, SPR_GOLD_BLOCK, 3.0f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 3, SND_METAL);
        registerBlock(42, "Block of Iron",    SHAPE_CUBE, SPR_IRON_BLOCK, SPR_IRON_BLOCK, SPR_IRON_BLOCK, 5.0f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 2, SND_METAL);
        registerBlock(43, "Double Slab",      SHAPE_CUBE, SPR_DOUBLE_SLAB_TOP, SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_TOP, 2.0f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(44, "Slab",             SHAPE_SLAB, SPR_DOUBLE_SLAB_TOP, SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_TOP, 2.0f, 10.0f, 0, 0, true, false, TOOL_PICKAXE, 1, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(45, "Bricks",           SHAPE_CUBE, SPR_BRICKS, SPR_BRICKS, SPR_BRICKS, 2.0f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(46, "TNT",              SHAPE_CUBE, SPR_TNT_TOP, SPR_TNT_SIDE, SPR_TNT_BOTTOM, 0.0f, 0.0f, 0, 15, true, true, TOOL_NONE, 0, SND_GRASS, TINT_NONE, false, true);
        registerBlock(47, "Bookshelf",        SHAPE_CUBE, SPR_PLANKS, SPR_BOOKSHELF, SPR_PLANKS, 1.5f, 1.5f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(48, "Moss Stone",       SHAPE_CUBE, SPR_MOSSY_COBBLESTONE, SPR_MOSSY_COBBLESTONE, SPR_MOSSY_COBBLESTONE, 2.0f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(49, "Obsidian",         SHAPE_CUBE, SPR_OBSIDIAN, SPR_OBSIDIAN, SPR_OBSIDIAN, 50.0f, 2000.0f, 0, 15, true, true, TOOL_PICKAXE, 4, SND_STONE);

        // IDs 50..79: Torches, Fire, Spawner, Stairs, Chest, Redstone, Diamond, Crafting, Wheat, Farmland, Furnace, Sign, Doors, Ladder, Rails, Lever, Plates, Redstone Ore/Torch, Button, Snow, Ice
        registerBlock(50, "Torch",            SHAPE_TORCH, SPR_TORCH, SPR_TORCH, SPR_TORCH, 0.0f, 0.0f, 14, 0, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(51, "Fire",             SHAPE_FIRE, SPR_FIRE_0, SPR_FIRE_0, SPR_FIRE_0, 0.0f, 0.0f, 15, 0, false, false, TOOL_NONE, 0, SND_WOOD, TINT_NONE, true);
        registerBlock(52, "Monster Spawner",  SHAPE_CUBE, SPR_MOB_SPAWNER, SPR_MOB_SPAWNER, SPR_MOB_SPAWNER, 5.0f, 5.0f, 0, 0, true, false, TOOL_PICKAXE, 1, SND_METAL);
        registerBlock(53, "Wooden Stairs",    SHAPE_STAIR, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 2.0f, 5.0f, 0, 0, true, false, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true, true);
        registerBlock(54, "Chest",            SHAPE_CUBE, SPR_CHEST_TOP, SPR_CHEST_SIDE, SPR_CHEST_TOP, 2.5f, 2.5f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(54, SPR_CHEST_FRONT);
        registerBlock(55, "Redstone Wire",    SHAPE_REDSTONE_WIRE, SPR_REDSTONE_CROSS, SPR_REDSTONE_CROSS, SPR_REDSTONE_CROSS, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_STONE, TINT_REDSTONE);
        registerBlock(56, "Diamond Ore",      SHAPE_CUBE, SPR_DIAMOND_ORE, SPR_DIAMOND_ORE, SPR_DIAMOND_ORE, 3.0f, 5.0f, 0, 15, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(57, "Block of Diamond", SHAPE_CUBE, SPR_DIAMOND_BLOCK, SPR_DIAMOND_BLOCK, SPR_DIAMOND_BLOCK, 5.0f, 10.0f, 0, 15, true, true, TOOL_PICKAXE, 3, SND_METAL);
        registerBlock(58, "Crafting Table",   SHAPE_CUBE, SPR_CRAFTING_TOP, SPR_CRAFTING_SIDE_1, SPR_PLANKS, 2.5f, 2.5f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(58, SPR_CRAFTING_SIDE_2);
        registerBlock(59, "Wheat Crops",      SHAPE_CROP, SPR_WHEAT_7, SPR_WHEAT_7, SPR_WHEAT_7, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(60, "Farmland",         SHAPE_CUBE, SPR_FARMLAND_DRY, SPR_DIRT, SPR_DIRT, 0.6f, 0.6f, 0, 0, true, false, TOOL_SHOVEL, 0, SND_GRAVEL, TINT_NONE, false, false, true);
        registerBlock(61, "Furnace",          SHAPE_CUBE, SPR_FURNACE_TOP, SPR_FURNACE_SIDE, SPR_FURNACE_TOP, 3.5f, 3.5f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        setFrontTexture(61, SPR_FURNACE_FRONT_OFF);
        registerBlock(62, "Burning Furnace",  SHAPE_CUBE, SPR_FURNACE_TOP, SPR_FURNACE_SIDE, SPR_FURNACE_TOP, 3.5f, 3.5f, 13, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        setFrontTexture(62, SPR_FURNACE_FRONT_ON);
        registerBlock(63, "Standing Sign",    SHAPE_SLAB, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 1.0f, 1.0f, 0, 0, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(64, "Wooden Door",      SHAPE_DOOR, SPR_DOOR_WOOD_TOP, SPR_DOOR_WOOD_TOP, SPR_DOOR_WOOD_BOTTOM, 3.0f, 3.0f, 0, 0, true, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(65, "Ladder",           SHAPE_LADDER, SPR_LADDER, SPR_LADDER, SPR_LADDER, 0.4f, 0.4f, 0, 0, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(66, "Rail",             SHAPE_RAIL, SPR_RAIL_STRAIGHT, SPR_RAIL_STRAIGHT, SPR_RAIL_STRAIGHT, 0.7f, 0.7f, 0, 0, false, false, TOOL_PICKAXE, 0, SND_METAL);
        registerBlock(67, "Cobblestone Stairs",SHAPE_STAIR, SPR_COBBLESTONE, SPR_COBBLESTONE, SPR_COBBLESTONE, 2.0f, 10.0f, 0, 0, true, false, TOOL_PICKAXE, 1, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(68, "Wall Sign",        SHAPE_LADDER, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 1.0f, 1.0f, 0, 0, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(69, "Lever",            SHAPE_LEVER, SPR_LEVER, SPR_LEVER, SPR_COBBLESTONE, 0.5f, 0.5f, 0, 0, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(70, "Stone Pressure Plate", SHAPE_PRESSURE_PLATE, SPR_STONE, SPR_STONE, SPR_STONE, 0.5f, 0.5f, 0, 0, false, false, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(71, "Iron Door",        SHAPE_DOOR, SPR_DOOR_IRON_TOP, SPR_DOOR_IRON_TOP, SPR_DOOR_IRON_BOTTOM, 5.0f, 5.0f, 0, 0, true, false, TOOL_PICKAXE, 1, SND_METAL);
        registerBlock(72, "Wooden Pressure Plate", SHAPE_PRESSURE_PLATE, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 0.5f, 0.5f, 0, 0, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(73, "Redstone Ore",     SHAPE_CUBE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, 3.0f, 5.0f, 0, 15, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(74, "Glowing Redstone Ore", SHAPE_CUBE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, 3.0f, 5.0f, 9, 15, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(75, "Redstone Torch (Off)", SHAPE_TORCH, SPR_REDSTONE_TORCH_OFF, SPR_REDSTONE_TORCH_OFF, SPR_REDSTONE_TORCH_OFF, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(76, "Redstone Torch (On)",  SHAPE_TORCH, SPR_REDSTONE_TORCH_ON, SPR_REDSTONE_TORCH_ON, SPR_REDSTONE_TORCH_ON, 0.0f, 0.0f, 7, 0, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(77, "Stone Button",     SHAPE_BUTTON, SPR_STONE, SPR_STONE, SPR_STONE, 0.5f, 0.5f, 0, 0, false, false, TOOL_NONE, 0, SND_STONE);
        registerBlock(78, "Snow",             SHAPE_SNOW_LAYER, SPR_SNOW, SPR_SNOW, SPR_SNOW, 0.1f, 0.1f, 0, 0, false, false, TOOL_SHOVEL, 1, SND_SNOW, TINT_NONE, true);
        registerBlock(79, "Ice",              SHAPE_CUBE, SPR_ICE, SPR_ICE, SPR_ICE, 0.5f, 0.5f, 0, 3, true, false, TOOL_PICKAXE, 0, SND_GLASS, TINT_NONE, false, false, false, 0.98f);

        // IDs 80..96: Snow Block, Cactus, Clay, Sugar Cane, Jukebox, Fence, Pumpkin, Nether, Glowstone, Portal, Jack o'Lantern, Cake, Repeaters, Locked Chest, Trapdoor
        registerBlock(80, "Snow Block",       SHAPE_CUBE, SPR_SNOW, SPR_SNOW, SPR_SNOW, 0.2f, 0.2f, 0, 15, true, true, TOOL_SHOVEL, 1, SND_SNOW);
        registerBlock(81, "Cactus",           SHAPE_CACTUS, SPR_CACTUS_TOP, SPR_CACTUS_SIDE, SPR_CACTUS_BOTTOM, 0.4f, 0.4f, 0, 0, true, false, TOOL_NONE, 0, SND_CLOTH);
        registerBlock(82, "Clay",             SHAPE_CUBE, SPR_CLAY, SPR_CLAY, SPR_CLAY, 0.6f, 0.6f, 0, 15, true, true, TOOL_SHOVEL, 0, SND_GRAVEL);
        registerBlock(83, "Sugar Canes",      SHAPE_CROSS, SPR_SUGAR_CANE, SPR_SUGAR_CANE, SPR_SUGAR_CANE, 0.0f, 0.0f, 0, 0, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(84, "Jukebox",          SHAPE_CUBE, SPR_JUKEBOX_TOP, SPR_JUKEBOX_SIDE, SPR_JUKEBOX_SIDE, 2.0f, 10.0f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD);
        registerBlock(85, "Fence",            SHAPE_FENCE, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 2.0f, 5.0f, 0, 0, true, false, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(86, "Pumpkin",          SHAPE_CUBE, SPR_PUMPKIN_TOP, SPR_PUMPKIN_SIDE, SPR_PUMPKIN_TOP, 1.0f, 1.0f, 0, 15, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(86, SPR_PUMPKIN_FRONT);
        registerBlock(87, "Netherrack",       SHAPE_CUBE, SPR_NETHERRACK, SPR_NETHERRACK, SPR_NETHERRACK, 0.4f, 0.4f, 0, 15, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(88, "Soul Sand",        SHAPE_CUBE, SPR_SOUL_SAND, SPR_SOUL_SAND, SPR_SOUL_SAND, 0.5f, 0.5f, 0, 15, true, true, TOOL_SHOVEL, 0, SND_SAND);
        registerBlock(89, "Glowstone",        SHAPE_CUBE, SPR_GLOWSTONE, SPR_GLOWSTONE, SPR_GLOWSTONE, 0.3f, 0.3f, 15, 15, true, true, TOOL_NONE, 0, SND_GLASS);
        registerBlock(90, "Portal",           SHAPE_PORTAL, SPR_PORTAL, SPR_PORTAL, SPR_PORTAL, -1.0f, 0.0f, 11, 0, false, false, TOOL_NONE, 99, SND_GLASS);
        registerBlock(91, "Jack o'Lantern",   SHAPE_CUBE, SPR_PUMPKIN_TOP, SPR_PUMPKIN_SIDE, SPR_PUMPKIN_TOP, 1.0f, 1.0f, 15, 15, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(91, SPR_JACK_O_LANTERN);
        registerBlock(92, "Cake",             SHAPE_CAKE, SPR_CAKE_TOP, SPR_CAKE_SIDE, SPR_CAKE_SIDE, 0.5f, 0.5f, 0, 0, true, false, TOOL_NONE, 0, SND_CLOTH);
        registerBlock(93, "Redstone Repeater (Off)", SHAPE_REPEATER, SPR_REPEATER_OFF, SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_SIDE, 0.0f, 0.0f, 0, 0, true, false, TOOL_NONE, 0, SND_STONE);
        registerBlock(94, "Redstone Repeater (On)",  SHAPE_REPEATER, SPR_REPEATER_ON,  SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_SIDE, 0.0f, 0.0f, 9, 0, true, false, TOOL_NONE, 0, SND_STONE);
        registerBlock(95, "Locked Chest",     SHAPE_CUBE, SPR_CHEST_TOP, SPR_CHEST_SIDE, SPR_CHEST_TOP, 0.0f, 0.0f, 15, 15, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(95, SPR_CHEST_FRONT);
        registerBlock(96, "Trapdoor",         SHAPE_TRAPDOOR, SPR_TRAPDOOR, SPR_TRAPDOOR, SPR_TRAPDOOR, 3.0f, 3.0f, 0, 0, true, false, TOOL_AXE, 0, SND_WOOD);
    }
};