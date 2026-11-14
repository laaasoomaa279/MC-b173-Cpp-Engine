#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// CANONICAL VOXEL ENGINE BLOCK REGISTRY & SUB-VOXEL GEOMETRY SPECIFICATION
// ============================================================================
// Technical Subsystem Architecture:
// 1. Dual Spatial Property Model (Solidity vs Opaque Cubical Bounds):
//    - isSolid: Governs kinetic collision volumes, kinetic raycast interception,
//      and entity pathfinding bounds.
//    - isOpaque: Dictates optical attenuation; opaque blocks fully occlude adjoining
//      geometry faces (culling internal quads) and attenuate transmission light.
//    - isOpaqueCube: Defines whether a block is a complete 16x16x16 boundary cube
//      with complete surface opacity. Crucial invariant: only blocks satisfying
//      isOpaqueCube == true can mechanically sustain surface cover overlays
//      (such as snow layers, pressure plates, redstone dust, and floor torches).
//      Non-full blocks (e.g., Cactus, Slabs, Stairs, Fences, Glass, Ice) evaluate
//      isOpaqueCube as false to inhibit invalid structural attachments.
// 2. Sub-Voxel Geometry Decomposition (1/16th Grid Quantization):
//    - Non-cubic blocks are decomposed into discrete bounding sub-boxes quantized
//      in integer sixteenths of a unit block [0..16]:
//        * Slabs (44): Half-height volumes occupying [0..8]/16.
//        * Stairs (53, 67): Stepped union of a base slab [0..8]/16 and a directional
//          quarter-step [8..16]/16.
//        * Cactus (81): Concentric column spanning [1..15]/16 horizontally and [0..16]/16
//          vertically, leaving a 1/16 boundary inset along perimeter edges.
//        * Beds (26): Base mattress occupying [3..9]/16 with directional leg pairs (height [0..3]/16)
//          isolated exclusively to the outer extremities of the head and foot halves.
//        * Cake (92): Dynamic horizontal depletion along X spanning [1 + eaten*2 .. 15]/16.
// 3. Metadata Texture & Spatial Orientation Resolution:
//    - Parametric texture resolution (resolveFaceTexture) calculates discrete atlas
//      sprite indices for directional blocks (Furnaces, Dispensers, Pumpkins, Pistons),
//      multi-species forestry (Oak, Spruce, Birch Logs/Leaves/Saplings), 16-color Wool,
//      and multi-stage agricultural crops.
// 4. Directional Kinematics & Neighbor Connectivity:
//    - 4-way planar adjacency masks compute dynamic connectivity for horizontal
//      Redstone wire networks and connective wooden fences based on neighbor solid state.
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
    SHAPE_NONE          = 0,  // Air / Optical vacuum
    SHAPE_CUBE          = 1,  // Complete 16x16x16 standard volume
    SHAPE_SLAB          = 2,  // Half-block volumetric step
    SHAPE_STAIR         = 3,  // Angular stepped volume
    SHAPE_CROSS         = 4,  // Dual intersecting diagonal planar quads
    SHAPE_TORCH         = 5,  // Angled or upright wall/floor luminary
    SHAPE_FLUID         = 6,  // Dynamic fluid surface volume
    SHAPE_FENCE         = 7,  // Centered post with dynamic neighbor crossbars
    SHAPE_DOOR          = 8,  // Dual-height portal aperture
    SHAPE_TRAPDOOR      = 9,  // Horizontal planar aperture flap
    SHAPE_LADDER        = 10, // Wall-flush mounting traverse rung
    SHAPE_RAIL          = 11, // Flat and ascending vehicular guide track
    SHAPE_REDSTONE_WIRE = 12, // Sub-pixel surface and wall conductive trace
    SHAPE_REPEATER      = 13, // Diode substrate with adjustable delay notches
    SHAPE_LEVER         = 14, // Mechanical pivot switch
    SHAPE_BUTTON        = 15, // Transient contact actuator
    SHAPE_PRESSURE_PLATE= 16, // Weight-activated floor membrane
    SHAPE_PISTON        = 17, // Pneumatic core chassis
    SHAPE_PISTON_HEAD   = 18, // Extended pneumatic thrust plate and rod
    SHAPE_SNOW_LAYER    = 19, // Variable sub-pixel granular surface layer
    SHAPE_CACTUS        = 20, // Ribbed 14x16x14 perimeter-inset volume
    SHAPE_CROP          = 21, // Multi-plane agricultural crop array
    SHAPE_BED           = 22, // Dual-block articulated slumber platform
    SHAPE_CAKE          = 23, // Segmented consumable food volume
    SHAPE_FIRE          = 24, // Thermal atmospheric flame volume
    SHAPE_PORTAL        = 25  // Dimensional transit membrane
};

enum TintMode : uint8_t {
    TINT_NONE     = 0,
    TINT_GRASS    = 1, // Thermodynamic grass map coloration
    TINT_FOLIAGE  = 2, // Thermodynamic foliage map coloration
    TINT_REDSTONE = 3  // Conduction power gradient coloration (0..15)
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

// Canonical 256x256 Texture Atlas Sprite Indices (16x16 Grid, 0..255)
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
    float         hardness              = 0.0f;  // Negative indicates unbreakable substrate
    float         resistance            = 0.0f;  // Kinetic blast resistance
    float         slipperiness          = 0.60f; // 0.60f standard friction, 0.98f Ice
    uint8_t       lightEmission         = 0;     // 0..15 luminance level
    uint8_t       lightOpacity          = 0;     // Optical attenuation: 0 = clear, 15 = fully occluding
    bool          isSolid               = false; // Spatial collision geometry presence
    bool          isOpaque              = false; // Total optical face occlusion
    bool          isOpaqueCube          = false; // Structural 16x16x16 solid cube (permits surface cover support)
    bool          isReplaceable         = false; // Permits fluid or placement overwrite
    bool          isFlammable           = false; // Subject to thermal ignition
    bool          useNeighborBrightness = false; // Samples ambient occlusion from adjacent offsets
    uint16_t      faceTex[6]            = {0};   // East, West, Top, Bottom, South, North texture indices

    // Structural rule: only solid, fully opaque cubic volumes permit surface snow attachments
    inline bool canSupportSnow() const {
        return isOpaqueCube && isSolid;
    }
};

// Quantized 1/16th Sub-Box bounding geometry
struct SubBox {
    float    minX, minY, minZ;
    float    maxX, maxY, maxZ;
    uint16_t faceTex[6];
    TintMode tint = TINT_NONE;
};

struct SubBoxList {
    SubBox boxes[12];
    int    count = 0;

    void add(float x0, float y0, float z0, float x1, float y1, float z1,
             const uint16_t tex[6], TintMode t = TINT_NONE) {
        if (count >= 12) return;
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
// Canonical Block Registry (IDs 0..96 Canonical + 97..255 Extensibility Hooks)
// ============================================================================
class BlockRegistry {
public:
    BlockDef defs[256];

    BlockRegistry() {
        registerAllCanonicalBlocks();
    }

    void registerBlock(uint8_t id, const char* name, BlockShape shape,
                       uint16_t topTex, uint16_t sideTex, uint16_t botTex,
                       float hardness, float resistance, uint8_t lightEmit, uint8_t lightOpac,
                       bool solid, bool opaque, bool opaqueCube,
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
        d.isOpaqueCube          = opaqueCube;
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

    // Evaluates horizontal conduction eligibility for adjacent Redstone traces
    // sideFromWire: 0 = West(-X), 1 = East(+X), 2 = North(-Z), 3 = South(+Z)
    static inline bool isRedstoneConnectableBlock(uint16_t rawState, int sideFromWire) {
        uint8_t id   = static_cast<uint8_t>((rawState >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(rawState & 0x0F);
        if (id == 0) return false;
        if (id == 55 || id == 75 || id == 76 || id == 69 || id == 77 || id == 70 || id == 72 || id == 28) {
            return true;
        }
        if (id == 93 || id == 94) {
            int dir = meta & 0x03; // 0=North(-Z), 1=East(+X), 2=South(+Z), 3=West(-X)
            bool repeaterAlongX  = (dir == 1 || dir == 3);
            bool wireCheckAlongX = (sideFromWire == 0 || sideFromWire == 1);
            return repeaterAlongX == wireCheckAlongX;
        }
        return false;
    }

    // Calculates 4-bit planar connection topology mask for Redstone Wire (bit 0: -X, 1: +X, 2: -Z, 3: +Z)
    template<typename WorldQueryFn>
    uint8_t getRedstoneWireConnections(int bx, int by, int bz, WorldQueryFn&& getNeighborState,
                                       uint8_t* outWallClimbMask = nullptr) const {
        uint8_t mask      = 0;
        uint8_t climbMask = 0;
        bool aboveIsOpaque = defs[(getNeighborState(bx, by + 1, bz) >> 4) & 0xFF].isOpaque;

        static constexpr int dirs[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
        for (int d = 0; d < 4; ++d) {
            int nx = bx + dirs[d][0];
            int nz = bz + dirs[d][1];
            uint16_t adjSt = getNeighborState(nx, by, nz);
            uint8_t  adjId = static_cast<uint8_t>((adjSt >> 4) & 0xFF);

            if (isRedstoneConnectableBlock(adjSt, d)) {
                mask |= (1u << d);
            } else if (!defs[adjId].isOpaque) {
                uint16_t downSt = getNeighborState(nx, by - 1, nz);
                if (((downSt >> 4) & 0xFF) == 55) {
                    mask |= (1u << d);
                }
            } else if (!aboveIsOpaque) {
                uint16_t upSt = getNeighborState(nx, by + 1, nz);
                if (((upSt >> 4) & 0xFF) == 55) {
                    mask      |= (1u << d);
                    climbMask |= (1u << d);
                }
            }
        }
        if (outWallClimbMask) *outWallClimbMask = climbMask;
        return mask;
    }

    // Resolves state and metadata dependent sprite atlas index
    uint16_t resolveFaceTexture(uint16_t rawState, BlockFace face, bool isSnowCovered = false) const {
        uint8_t id   = static_cast<uint8_t>((rawState >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(rawState & 0x0F);
        const BlockDef& d = defs[id];

        // Grass Block (2): snowy side overlay when covered by snow layer or block
        if (id == 2) {
            if (face == FACE_TOP)    return SPR_GRASS_TOP;
            if (face == FACE_BOTTOM) return SPR_DIRT;
            return isSnowCovered ? SPR_GRASS_SNOW_SIDE : SPR_GRASS_SIDE;
        }

        // Wood Log (17): meta 0 = Oak, 1 = Spruce, 2 = Birch
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

        // Powered Rail (27): meta & 0x08 -> active illuminated state
        if (id == 27) {
            return (meta & 0x08) ? SPR_RAIL_POWERED_ON : SPR_RAIL_POWERED_OFF;
        }

        // Standard Rail (66): meta 6..9 -> curved track quadrant
        if (id == 66) {
            return (meta >= 6 && meta <= 9) ? SPR_RAIL_CURVED : SPR_RAIL_STRAIGHT;
        }

        // Plant Cover (31): meta 0=Dead Shrub, 1=Tall Grass, 2=Fern
        if (id == 31) {
            if (meta == 0) return SPR_DEAD_BUSH;
            if (meta == 2) return SPR_FERN;
            return SPR_TALL_GRASS;
        }

        // Wool Matrix (35): Canonical 16-color lookup
        if (id == 35) {
            static constexpr uint16_t woolSprites[16] = {
                SPR_WOOL_WHITE, SPR_WOOL_ORANGE, SPR_WOOL_MAGENTA, SPR_WOOL_LIGHT_BLUE,
                SPR_WOOL_YELLOW, SPR_WOOL_LIME, SPR_WOOL_PINK, SPR_WOOL_GRAY,
                SPR_WOOL_LIGHT_GRAY, SPR_WOOL_CYAN, SPR_WOOL_PURPLE, SPR_WOOL_DARK_BLUE,
                SPR_WOOL_BROWN, SPR_WOOL_DARK_GREEN, SPR_WOOL_RED, SPR_WOOL_BLACK
            };
            return woolSprites[meta & 0x0F];
        }

        // Slabs (43, 44): meta 0=Stone, 1=Sandstone, 2=Wood, 3=Cobblestone
        if (id == 43 || id == 44) {
            uint8_t mat = meta & 0x07;
            if (mat == 1) {
                return (face == FACE_TOP) ? SPR_SANDSTONE_TOP :
                       ((face == FACE_BOTTOM) ? SPR_SANDSTONE_BOTTOM : SPR_SANDSTONE_SIDE);
            }
            if (mat == 2) return SPR_PLANKS;
            if (mat == 3) return SPR_COBBLESTONE;
            return (face == FACE_TOP || face == FACE_BOTTOM) ? SPR_DOUBLE_SLAB_TOP : SPR_DOUBLE_SLAB_SIDE;
        }

        // Agricultural Wheat (59): growth stages 0..7
        if (id == 59) {
            return static_cast<uint16_t>(SPR_WHEAT_0 + (meta & 0x07));
        }

        // Farmland (60): meta > 0 = Hydrated, 0 = Arid
        if (id == 60) {
            if (face == FACE_TOP) return (meta > 0) ? SPR_FARMLAND_WET : SPR_FARMLAND_DRY;
            return SPR_DIRT;
        }

        // Combustion Furnaces (61, 62) & Dispensers (23) directional aperture
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

        // Articulated Doors (64, 71): meta & 0x08 -> upper vs lower segment
        if (id == 64 || id == 71) {
            bool isTop = (meta & 0x08) != 0;
            if (id == 64) return isTop ? SPR_DOOR_WOOD_TOP : SPR_DOOR_WOOD_BOTTOM;
            return isTop ? SPR_DOOR_IRON_TOP : SPR_DOOR_IRON_BOTTOM;
        }

        // Slumber Platform (26): meta & 0x08 -> head vs foot module
        if (id == 26) {
            bool isHead = (meta & 0x08) != 0;
            if (face == FACE_BOTTOM) return SPR_PLANKS;
            if (face == FACE_TOP)    return isHead ? SPR_BED_HEAD_TOP : SPR_BED_FEET_TOP;
            return isHead ? SPR_BED_HEAD_SIDE : SPR_BED_FEET_SIDE;
        }

        // Carved Cucurbita (86, 91): directional carved facial relief
        if (id == 86 || id == 91) {
            uint8_t dir = meta & 0x03; // 0=South, 1=West, 2=North, 3=East
            bool isFace = ((dir == 0 && face == FACE_SOUTH) ||
                           (dir == 1 && face == FACE_WEST)  ||
                           (dir == 2 && face == FACE_NORTH) ||
                           (dir == 3 && face == FACE_EAST));
            if (isFace) return (id == 91) ? SPR_JACK_O_LANTERN : SPR_PUMPKIN_FRONT;
        }

        // Pneumatic Core Base (29, 33): directional cylinder face and thrust collar
        if (id == 29 || id == 33) {
            uint8_t dir = meta & 0x07; // 0=Down, 1=Up, 2=North, 3=South, 4=West, 5=East
            bool extended = (meta & 0x08) != 0;
            BlockFace headFace = (dir == 0) ? FACE_BOTTOM : ((dir == 1) ? FACE_TOP :
                                 ((dir == 2) ? FACE_NORTH : ((dir == 3) ? FACE_SOUTH :
                                 ((dir == 4) ? FACE_WEST  : FACE_EAST))));
            BlockFace backFace = static_cast<BlockFace>(headFace ^ 1);
            if (face == headFace) return extended ? SPR_PISTON_INNER : ((id == 29) ? SPR_PISTON_HEAD_STICKY : SPR_PISTON_HEAD_NORMAL);
            if (face == backFace) return SPR_PISTON_BACK;
            return SPR_PISTON_SIDE;
        }

        return d.faceTex[face];
    }

    // Resolves exact 1/16th sub-box spatial decomposition for non-cubic structures
    template<typename WorldQueryFn>
    void getSubBoxes(uint16_t rawState, int bx, int by, int bz, WorldQueryFn&& getNeighborState, SubBoxList& out) const {
        out.count = 0;
        uint8_t id   = static_cast<uint8_t>((rawState >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(rawState & 0x0F);
        const BlockDef& d = defs[id];

        uint16_t fTex[6];
        for (int f = 0; f < 6; ++f) fTex[f] = resolveFaceTexture(rawState, static_cast<BlockFace>(f));

        if (d.shape == SHAPE_SLAB) {
            if (id == 63) {
                // Standing Signpost: central mounting shaft + sign board
                out.addUniform(7, 0, 7, 9,  9,  9, SPR_PLANKS);
                out.addUniform(1, 9, 7, 15, 16, 9, SPR_PLANKS);
            } else {
                out.add(0, 0, 0, 16, 8, 16, fTex, d.tintMode);
            }
        }
        else if (d.shape == SHAPE_SNOW_LAYER) {
            float h = static_cast<float>((meta & 0x07) + 1) * 2.0f;
            out.add(0, 0, 0, 16, std::min(16.0f, h), 16, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_CACTUS) {
            // Inset by 1/16th on all horizontal margins [1..15]/16
            out.add(1, 0, 1, 15, 16, 15, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_BED) {
            // Mattress occupies [3..9]/16 height. Two leg posts isolated to the outer end of each half
            uint8_t dir     = meta & 0x03; // 0=South(+Z), 1=West(-X), 2=North(-Z), 3=East(+X)
            bool isHead     = (meta & 0x08) != 0;
            uint16_t endTex = isHead ? SPR_BED_HEAD_END : SPR_BED_FEET_END;

            if (dir == 0)      fTex[isHead ? FACE_SOUTH : FACE_NORTH] = endTex;
            else if (dir == 1) fTex[isHead ? FACE_WEST  : FACE_EAST]  = endTex;
            else if (dir == 2) fTex[isHead ? FACE_NORTH : FACE_SOUTH] = endTex;
            else               fTex[isHead ? FACE_EAST  : FACE_WEST]  = endTex;

            out.add(0, 3, 0, 16, 9, 16, fTex, d.tintMode);

            // Canonical 2-leg allocation per bed block at the outer edge
            if (dir == 0) { // Head at +Z, feet at -Z
                if (isHead) {
                    out.addUniform(0,  0, 13, 3,  3, 16, SPR_PLANKS);
                    out.addUniform(13, 0, 13, 16, 3, 16, SPR_PLANKS);
                } else {
                    out.addUniform(0,  0, 0,  3,  3, 3,  SPR_PLANKS);
                    out.addUniform(13, 0, 0,  16, 3, 3,  SPR_PLANKS);
                }
            } else if (dir == 1) { // Head at -X, feet at +X
                if (isHead) {
                    out.addUniform(0, 0, 0,  3, 3, 3,  SPR_PLANKS);
                    out.addUniform(0, 0, 13, 3, 3, 16, SPR_PLANKS);
                } else {
                    out.addUniform(13, 0, 0,  16, 3, 3,  SPR_PLANKS);
                    out.addUniform(13, 0, 13, 16, 3, 16, SPR_PLANKS);
                }
            } else if (dir == 2) { // Head at -Z, feet at +Z
                if (isHead) {
                    out.addUniform(0,  0, 0, 3,  3, 3, SPR_PLANKS);
                    out.addUniform(13, 0, 0, 16, 3, 3, SPR_PLANKS);
                } else {
                    out.addUniform(0,  0, 13, 3,  3, 16, SPR_PLANKS);
                    out.addUniform(13, 0, 13, 16, 3, 16, SPR_PLANKS);
                }
            } else { // Head at +X, feet at -X
                if (isHead) {
                    out.addUniform(13, 0, 0,  16, 3, 3,  SPR_PLANKS);
                    out.addUniform(13, 0, 13, 16, 3, 16, SPR_PLANKS);
                } else {
                    out.addUniform(0, 0, 0,  3, 3, 3,  SPR_PLANKS);
                    out.addUniform(0, 0, 13, 3, 3, 16, SPR_PLANKS);
                }
            }
        }
        else if (d.shape == SHAPE_CAKE) {
            int eaten = std::min(6, static_cast<int>(meta & 0x07));
            float x0  = static_cast<float>(1 + eaten * 2);
            if (eaten > 0) fTex[FACE_WEST] = SPR_CAKE_INNER;
            out.add(x0, 0, 1, 15, 8, 15, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_STAIR) {
            uint8_t dir = meta & 0x03; // 0=+X, 1=-X, 2=+Z, 3=-Z
            out.add(0, 0, 0, 16, 8, 16, fTex, d.tintMode);

            if (dir == 0)      out.add(8, 8, 0, 16, 16, 16, fTex, d.tintMode);
            else if (dir == 1) out.add(0, 8, 0,  8, 16, 16, fTex, d.tintMode);
            else if (dir == 2) out.add(0, 8, 8, 16, 16, 16, fTex, d.tintMode);
            else               out.add(0, 8, 0, 16, 16,  8, fTex, d.tintMode);
        }
        else if (d.shape == SHAPE_FENCE) {
            out.add(6, 0, 6, 10, 16, 10, fTex, d.tintMode);
            auto connects = [&](int nx, int ny, int nz) -> bool {
                uint16_t nSt = getNeighborState(nx, ny, nz);
                uint8_t nId  = static_cast<uint8_t>((nSt >> 4) & 0xFF);
                return (nId == 85) || defs[nId].isOpaqueCube;
            };
            if (connects(bx - 1, by, bz)) { out.add(0,  12, 7, 6,  15, 9, fTex); out.add(0,  6, 7, 6,  9, 9, fTex); }
            if (connects(bx + 1, by, bz)) { out.add(10, 12, 7, 16, 15, 9, fTex); out.add(10, 6, 7, 16, 9, 9, fTex); }
            if (connects(bx, by, bz - 1)) { out.add(7, 12, 0,  9, 15, 6, fTex);  out.add(7, 6, 0,  9, 9, 6, fTex); }
            if (connects(bx, by, bz + 1)) { out.add(7, 12, 10, 9, 15, 16, fTex); out.add(7, 6, 10, 9, 9, 16, fTex); }
        }
        else if (d.shape == SHAPE_DOOR) {
            uint8_t effMeta = meta;
            if (meta & 0x08) {
                uint16_t lower = getNeighborState(bx, by - 1, bz);
                if (((lower >> 4) & 0xFF) == id) effMeta = static_cast<uint8_t>(lower & 0x0F);
            }
            uint8_t hinge = effMeta & 0x03;
            bool open     = (effMeta & 0x04) != 0;
            uint8_t side  = open ? ((hinge + 1) & 3) : hinge;
            if (side == 0)      out.add(0,  0, 0,  3, 16, 16, fTex);
            else if (side == 1) out.add(0,  0, 0, 16, 16,  3, fTex);
            else if (side == 2) out.add(13, 0, 0, 16, 16, 16, fTex);
            else                out.add(0,  0, 13, 16, 16, 16, fTex);
        }
        else if (d.shape == SHAPE_TRAPDOOR) {
            bool open = (meta & 0x04) != 0;
            if (!open) {
                out.add(0, 0, 0, 16, 3, 16, fTex);
            } else {
                uint8_t dir = meta & 0x03;
                if (dir == 0)      out.add(0,  0, 13, 16, 16, 16, fTex);
                else if (dir == 1) out.add(0,  0,  0, 16, 16,  3, fTex);
                else if (dir == 2) out.add(13, 0,  0, 16, 16, 16, fTex);
                else               out.add(0,  0,  0,  3, 16, 16, fTex);
            }
        }
        else if (d.shape == SHAPE_LADDER) {
            uint8_t dir = meta & 0x07;
            if (id == 68) {
                if (dir == 2)      out.add(1,  4.5f, 14, 15, 12.5f, 16, fTex);
                else if (dir == 3) out.add(1,  4.5f, 0,  15, 12.5f, 2,  fTex);
                else if (dir == 4) out.add(14, 4.5f, 1,  16, 12.5f, 15, fTex);
                else               out.add(0,  4.5f, 1,  2,  12.5f, 15, fTex);
            } else {
                if (dir == 2)      out.add(0,  0, 14, 16, 16, 16, fTex);
                else if (dir == 3) out.add(0,  0,  0, 16, 16,  2, fTex);
                else if (dir == 4) out.add(14, 0,  0, 16, 16, 16, fTex);
                else               out.add(0,  0,  0,  2, 16, 16, fTex);
            }
        }
        else if (d.shape == SHAPE_RAIL) {
            uint16_t railTex = resolveFaceTexture(rawState, FACE_TOP);
            uint8_t track    = meta & 0x0F;
            if (id == 27 || id == 28) track &= 0x07;

            if (track == 2) {
                for (int s = 0; s < 4; ++s) out.addUniform(s * 4.0f, s * 4.0f, 0, (s + 1) * 4.0f, s * 4.0f + 2.0f, 16, railTex);
            } else if (track == 3) {
                for (int s = 0; s < 4; ++s) out.addUniform((3 - s) * 4.0f, s * 4.0f, 0, (4 - s) * 4.0f, s * 4.0f + 2.0f, 16, railTex);
            } else if (track == 4) {
                for (int s = 0; s < 4; ++s) out.addUniform(0, s * 4.0f, (3 - s) * 4.0f, 16, s * 4.0f + 2.0f, (4 - s) * 4.0f, railTex);
            } else if (track == 5) {
                for (int s = 0; s < 4; ++s) out.addUniform(0, s * 4.0f, s * 4.0f, 16, s * 4.0f + 2.0f, (s + 1) * 4.0f, railTex);
            } else {
                out.addUniform(0, 0, 0, 16, 2, 16, railTex);
            }
        }
        else if (d.shape == SHAPE_REDSTONE_WIRE) {
            uint8_t climbMask = 0;
            uint8_t connMask  = getRedstoneWireConnections(bx, by, bz, getNeighborState, &climbMask);
            bool west  = (connMask & 0x01) != 0;
            bool east  = (connMask & 0x02) != 0;
            bool north = (connMask & 0x04) != 0;
            bool south = (connMask & 0x08) != 0;

            if ((west || east) && !north && !south) {
                out.addUniform(0, 0, 0, 16, 0.35f, 16, SPR_REDSTONE_LINE, TINT_REDSTONE);
            } else if ((north || south) && !west && !east) {
                out.addUniform(6, 0, 0, 10, 0.35f, 16, SPR_REDSTONE_CROSS, TINT_REDSTONE);
            } else {
                out.addUniform(0, 0, 0, 16, 0.35f, 16, SPR_REDSTONE_CROSS, TINT_REDSTONE);
            }

            if (climbMask & 0x01) out.addUniform(0.0f,   0, 6, 0.35f, 16, 10, SPR_REDSTONE_CROSS, TINT_REDSTONE);
            if (climbMask & 0x02) out.addUniform(15.65f, 0, 6, 16.0f, 16, 10, SPR_REDSTONE_CROSS, TINT_REDSTONE);
            if (climbMask & 0x04) out.addUniform(6, 0, 0.0f,   10, 16, 0.35f, SPR_REDSTONE_CROSS, TINT_REDSTONE);
            if (climbMask & 0x08) out.addUniform(6, 0, 15.65f, 10, 16, 16.0f, SPR_REDSTONE_CROSS, TINT_REDSTONE);
        }
        else if (d.shape == SHAPE_REPEATER) {
            out.add(0, 0, 0, 16, 2, 16, fTex);
            uint16_t torchTex = (id == 94) ? SPR_REDSTONE_TORCH_ON : SPR_REDSTONE_TORCH_OFF;
            uint8_t dir   = meta & 0x03;
            int     delay = (meta >> 2) & 0x03;
            float   slide = static_cast<float>(1 + delay * 2);

            if (dir == 0) {
                out.addUniform(7, 2, 2,         9, 7, 4,          torchTex);
                out.addUniform(7, 2, 5 + slide, 9, 7, 7 + slide,  torchTex);
            } else if (dir == 2) {
                out.addUniform(7, 2, 12,        9, 7, 14,         torchTex);
                out.addUniform(7, 2, 9 - slide, 9, 7, 11 - slide, torchTex);
            } else if (dir == 1) {
                out.addUniform(12, 2, 7,        14, 7, 9,         torchTex);
                out.addUniform(9 - slide, 2, 7, 11 - slide, 7, 9, torchTex);
            } else {
                out.addUniform(2, 2, 7,         4, 7, 9,          torchTex);
                out.addUniform(5 + slide, 2, 7, 7 + slide, 7, 9,  torchTex);
            }
        }
        else if (d.shape == SHAPE_PRESSURE_PLATE) {
            bool pressed = (meta & 0x01) != 0;
            out.add(1, 0, 1, 15, pressed ? 0.5f : 1.0f, 15, fTex);
        }
        else if (d.shape == SHAPE_BUTTON) {
            uint8_t dir  = meta & 0x07;
            bool pressed = (meta & 0x08) != 0;
            float thick  = pressed ? 1.0f : 2.0f;
            if (dir == 1)      out.add(0,          6, 5, thick, 10, 11, fTex);
            else if (dir == 2) out.add(16 - thick, 6, 5, 16,    10, 11, fTex);
            else if (dir == 3) out.add(5, 6, 0,          11, 10, thick, fTex);
            else               out.add(5, 6, 16 - thick, 11, 10, 16,    fTex);
        }
        else if (d.shape == SHAPE_LEVER) {
            uint8_t dir = meta & 0x07;
            bool active = (meta & 0x08) != 0;
            if (dir == 1) {
                out.addUniform(0, 4, 5, 3, 12, 11, SPR_COBBLESTONE);
                out.addUniform(2, active ? 3 : 9, 7, 10, active ? 5 : 11, 9, SPR_LEVER);
            } else if (dir == 2) {
                out.addUniform(13, 4, 5, 16, 12, 11, SPR_COBBLESTONE);
                out.addUniform(6, active ? 3 : 9, 7, 14, active ? 5 : 11, 9, SPR_LEVER);
            } else if (dir == 3) {
                out.addUniform(5, 4, 0, 11, 12, 3, SPR_COBBLESTONE);
                out.addUniform(7, active ? 3 : 9, 2, 9, active ? 5 : 11, 10, SPR_LEVER);
            } else if (dir == 4) {
                out.addUniform(5, 4, 13, 11, 12, 16, SPR_COBBLESTONE);
                out.addUniform(7, active ? 3 : 9, 6, 9, active ? 5 : 11, 14, SPR_LEVER);
            } else {
                out.addUniform(5, 0, 4, 11, 3, 12, SPR_COBBLESTONE);
                if (active) out.addUniform(7, 2, 9, 9, 10, 11, SPR_LEVER);
                else        out.addUniform(7, 2, 5, 9, 10, 7,  SPR_LEVER);
            }
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
            if (dir == 0)      { head[FACE_BOTTOM] = faceTex; out.add(0,  0,  0, 16,  4, 16, head); out.addUniform(6,  4, 6, 10, 16, 10, SPR_PISTON_SIDE); }
            else if (dir == 1) { head[FACE_TOP]    = faceTex; out.add(0, 12,  0, 16, 16, 16, head); out.addUniform(6,  0, 6, 10, 12, 10, SPR_PISTON_SIDE); }
            else if (dir == 2) { head[FACE_NORTH]  = faceTex; out.add(0,  0,  0, 16, 16,  4, head); out.addUniform(6,  6, 4, 10, 10, 16, SPR_PISTON_SIDE); }
            else if (dir == 3) { head[FACE_SOUTH]  = faceTex; out.add(0,  0, 12, 16, 16, 16, head); out.addUniform(6,  6, 0, 10, 10, 12, SPR_PISTON_SIDE); }
            else if (dir == 4) { head[FACE_WEST]   = faceTex; out.add(0,  0,  0,  4, 16, 16, head); out.addUniform(4,  6, 6, 16, 10, 10, SPR_PISTON_SIDE); }
            else               { head[FACE_EAST]   = faceTex; out.add(12, 0,  0, 16, 16, 16, head); out.addUniform(0,  6, 6, 12, 10, 10, SPR_PISTON_SIDE); }
        }
    }

private:
    void registerAllCanonicalBlocks() {
        // ID 0: Optical vacuum (Air)
        registerBlock(0, "Air", SHAPE_NONE, 0, 0, 0, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);

        // IDs 1..19: Core Structural Substrates, Minerals, Fluids, Forestry
        registerBlock(1,  "Stone",            SHAPE_CUBE, SPR_STONE, SPR_STONE, SPR_STONE, 1.5f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(2,  "Grass Block",      SHAPE_CUBE, SPR_GRASS_TOP, SPR_GRASS_SIDE, SPR_DIRT, 0.6f, 0.6f, 0, 15, true, true, true, TOOL_SHOVEL, 0, SND_GRASS, TINT_GRASS);
        registerBlock(3,  "Dirt",             SHAPE_CUBE, SPR_DIRT, SPR_DIRT, SPR_DIRT, 0.5f, 0.5f, 0, 15, true, true, true, TOOL_SHOVEL, 0, SND_GRAVEL);
        registerBlock(4,  "Cobblestone",      SHAPE_CUBE, SPR_COBBLESTONE, SPR_COBBLESTONE, SPR_COBBLESTONE, 2.0f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(5,  "Wooden Planks",    SHAPE_CUBE, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 2.0f, 5.0f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(6,  "Sapling",          SHAPE_CROSS, SPR_SAPLING_OAK, SPR_SAPLING_OAK, SPR_SAPLING_OAK, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(7,  "Bedrock",          SHAPE_CUBE, SPR_BEDROCK, SPR_BEDROCK, SPR_BEDROCK, -1.0f, 6000000.0f, 0, 15, true, true, true, TOOL_NONE, 99, SND_STONE);
        registerBlock(8,  "Flowing Water",    SHAPE_FLUID, SPR_WATER_STILL, SPR_WATER_STILL, SPR_WATER_STILL, 100.0f, 500.0f, 0, 3, false, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(9,  "Stationary Water", SHAPE_FLUID, SPR_WATER_STILL, SPR_WATER_STILL, SPR_WATER_STILL, 100.0f, 500.0f, 0, 3, false, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(10, "Flowing Lava",     SHAPE_FLUID, SPR_LAVA_STILL, SPR_LAVA_STILL, SPR_LAVA_STILL, 0.0f, 0.0f, 15, 0, false, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(11, "Stationary Lava",  SHAPE_FLUID, SPR_LAVA_STILL, SPR_LAVA_STILL, SPR_LAVA_STILL, 100.0f, 500.0f, 15, 0, false, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, true);
        registerBlock(12, "Sand",             SHAPE_CUBE, SPR_SAND, SPR_SAND, SPR_SAND, 0.5f, 0.5f, 0, 15, true, true, true, TOOL_SHOVEL, 0, SND_SAND);
        registerBlock(13, "Gravel",           SHAPE_CUBE, SPR_GRAVEL, SPR_GRAVEL, SPR_GRAVEL, 0.6f, 0.6f, 0, 15, true, true, true, TOOL_SHOVEL, 0, SND_GRAVEL);
        registerBlock(14, "Gold Ore",         SHAPE_CUBE, SPR_GOLD_ORE, SPR_GOLD_ORE, SPR_GOLD_ORE, 3.0f, 5.0f, 0, 15, true, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(15, "Iron Ore",         SHAPE_CUBE, SPR_IRON_ORE, SPR_IRON_ORE, SPR_IRON_ORE, 3.0f, 5.0f, 0, 15, true, true, true, TOOL_PICKAXE, 2, SND_STONE);
        registerBlock(16, "Coal Ore",         SHAPE_CUBE, SPR_COAL_ORE, SPR_COAL_ORE, SPR_COAL_ORE, 3.0f, 5.0f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(17, "Wood",             SHAPE_CUBE, SPR_LOG_TOP, SPR_LOG_OAK_SIDE, SPR_LOG_TOP, 2.0f, 2.0f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(18, "Leaves",           SHAPE_CUBE, SPR_LEAVES_FANCY, SPR_LEAVES_FANCY, SPR_LEAVES_FANCY, 0.2f, 0.2f, 0, 1, true, false, false, TOOL_SHEARS, 0, SND_GRASS, TINT_FOLIAGE, false, true);
        registerBlock(19, "Sponge",           SHAPE_CUBE, SPR_SPONGE, SPR_SPONGE, SPR_SPONGE, 0.6f, 0.6f, 0, 15, true, true, true, TOOL_NONE, 0, SND_GRASS);

        // IDs 20..49: Vitreous, Lapis, Sandstone, Rails, Pneumatics, Wool, Flora, Bricks, Obsidian
        registerBlock(20, "Glass",            SHAPE_CUBE, SPR_GLASS, SPR_GLASS, SPR_GLASS, 0.3f, 0.3f, 0, 0, true, false, false, TOOL_NONE, 0, SND_GLASS);
        registerBlock(21, "Lapis Lazuli Ore", SHAPE_CUBE, SPR_LAPIS_ORE, SPR_LAPIS_ORE, SPR_LAPIS_ORE, 3.0f, 5.0f, 0, 15, true, true, true, TOOL_PICKAXE, 2, SND_STONE);
        registerBlock(22, "Lapis Lazuli Block",SHAPE_CUBE, SPR_LAPIS_BLOCK, SPR_LAPIS_BLOCK, SPR_LAPIS_BLOCK, 3.0f, 5.0f, 0, 15, true, true, true, TOOL_PICKAXE, 2, SND_STONE);
        registerBlock(23, "Dispenser",        SHAPE_CUBE, SPR_FURNACE_TOP, SPR_FURNACE_SIDE, SPR_FURNACE_TOP, 3.5f, 3.5f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        setFrontTexture(23, SPR_DISPENSER_FRONT);
        registerBlock(24, "Sandstone",        SHAPE_CUBE, SPR_SANDSTONE_TOP, SPR_SANDSTONE_SIDE, SPR_SANDSTONE_BOTTOM, 0.8f, 0.8f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(25, "Note Block",       SHAPE_CUBE, SPR_JUKEBOX_SIDE, SPR_JUKEBOX_SIDE, SPR_JUKEBOX_SIDE, 0.8f, 0.8f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(26, "Bed",              SHAPE_BED, SPR_BED_HEAD_TOP, SPR_BED_HEAD_SIDE, SPR_PLANKS, 0.2f, 0.2f, 0, 0, true, false, false, TOOL_NONE, 0, SND_WOOD, TINT_NONE, false, false, true);
        registerBlock(27, "Powered Rail",     SHAPE_RAIL, SPR_RAIL_POWERED_OFF, SPR_RAIL_POWERED_OFF, SPR_RAIL_POWERED_OFF, 0.7f, 0.7f, 0, 0, false, false, false, TOOL_PICKAXE, 0, SND_METAL);
        registerBlock(28, "Detector Rail",    SHAPE_RAIL, SPR_RAIL_DETECTOR, SPR_RAIL_DETECTOR, SPR_RAIL_DETECTOR, 0.7f, 0.7f, 0, 0, false, false, false, TOOL_PICKAXE, 0, SND_METAL);
        registerBlock(29, "Sticky Piston",    SHAPE_PISTON, SPR_PISTON_HEAD_STICKY, SPR_PISTON_SIDE, SPR_PISTON_BACK, 0.5f, 0.5f, 0, 0, true, false, false, TOOL_PICKAXE, 0, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(30, "Cobweb",           SHAPE_CROSS, SPR_COBWEB, SPR_COBWEB, SPR_COBWEB, 4.0f, 4.0f, 0, 1, false, false, false, TOOL_SWORD, 0, SND_CLOTH);
        registerBlock(31, "Tall Grass",       SHAPE_CROSS, SPR_TALL_GRASS, SPR_TALL_GRASS, SPR_TALL_GRASS, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS, TINT_GRASS, true, true);
        registerBlock(32, "Dead Bush",        SHAPE_CROSS, SPR_DEAD_BUSH, SPR_DEAD_BUSH, SPR_DEAD_BUSH, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS, TINT_NONE, true, true);
        registerBlock(33, "Piston",           SHAPE_PISTON, SPR_PISTON_HEAD_NORMAL, SPR_PISTON_SIDE, SPR_PISTON_BACK, 0.5f, 0.5f, 0, 0, true, false, false, TOOL_PICKAXE, 0, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(34, "Piston Extension", SHAPE_PISTON_HEAD, SPR_PISTON_HEAD_NORMAL, SPR_PISTON_SIDE, SPR_PISTON_SIDE, 0.5f, 0.5f, 0, 0, true, false, false, TOOL_NONE, 0, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(35, "Wool",             SHAPE_CUBE, SPR_WOOL_WHITE, SPR_WOOL_WHITE, SPR_WOOL_WHITE, 0.8f, 0.8f, 0, 15, true, true, true, TOOL_SHEARS, 0, SND_CLOTH, TINT_NONE, false, true);
        registerBlock(36, "Piston Moving",    SHAPE_NONE, SPR_PISTON_SIDE, SPR_PISTON_SIDE, SPR_PISTON_SIDE, -1.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 99, SND_STONE);
        registerBlock(37, "Dandelion",        SHAPE_CROSS, SPR_DANDELION, SPR_DANDELION, SPR_DANDELION, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(38, "Rose",             SHAPE_CROSS, SPR_ROSE, SPR_ROSE, SPR_ROSE, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(39, "Brown Mushroom",   SHAPE_CROSS, SPR_MUSHROOM_BROWN, SPR_MUSHROOM_BROWN, SPR_MUSHROOM_BROWN, 0.0f, 0.0f, 1, 0, false, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(40, "Red Mushroom",     SHAPE_CROSS, SPR_MUSHROOM_RED, SPR_MUSHROOM_RED, SPR_MUSHROOM_RED, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(41, "Block of Gold",    SHAPE_CUBE, SPR_GOLD_BLOCK, SPR_GOLD_BLOCK, SPR_GOLD_BLOCK, 3.0f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 3, SND_METAL);
        registerBlock(42, "Block of Iron",    SHAPE_CUBE, SPR_IRON_BLOCK, SPR_IRON_BLOCK, SPR_IRON_BLOCK, 5.0f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 2, SND_METAL);
        registerBlock(43, "Double Slab",      SHAPE_CUBE, SPR_DOUBLE_SLAB_TOP, SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_TOP, 2.0f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(44, "Slab",             SHAPE_SLAB, SPR_DOUBLE_SLAB_TOP, SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_TOP, 2.0f, 10.0f, 0, 0, true, false, false, TOOL_PICKAXE, 1, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(45, "Bricks",           SHAPE_CUBE, SPR_BRICKS, SPR_BRICKS, SPR_BRICKS, 2.0f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(46, "TNT",              SHAPE_CUBE, SPR_TNT_TOP, SPR_TNT_SIDE, SPR_TNT_BOTTOM, 0.0f, 0.0f, 0, 15, true, true, true, TOOL_NONE, 0, SND_GRASS, TINT_NONE, false, true);
        registerBlock(47, "Bookshelf",        SHAPE_CUBE, SPR_PLANKS, SPR_BOOKSHELF, SPR_PLANKS, 1.5f, 1.5f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(48, "Moss Stone",       SHAPE_CUBE, SPR_MOSSY_COBBLESTONE, SPR_MOSSY_COBBLESTONE, SPR_MOSSY_COBBLESTONE, 2.0f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(49, "Obsidian",         SHAPE_CUBE, SPR_OBSIDIAN, SPR_OBSIDIAN, SPR_OBSIDIAN, 50.0f, 2000.0f, 0, 15, true, true, true, TOOL_PICKAXE, 4, SND_STONE);

        // IDs 50..79: Luminaries, Spawner, Stairs, Storage, Circuitry, Agriculture, Furnaces, Mechanics
        registerBlock(50, "Torch",            SHAPE_TORCH, SPR_TORCH, SPR_TORCH, SPR_TORCH, 0.0f, 0.0f, 14, 0, false, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(51, "Fire",             SHAPE_FIRE, SPR_FIRE_0, SPR_FIRE_0, SPR_FIRE_0, 0.0f, 0.0f, 15, 0, false, false, false, TOOL_NONE, 0, SND_WOOD, TINT_NONE, true);
        registerBlock(52, "Monster Spawner",  SHAPE_CUBE, SPR_MOB_SPAWNER, SPR_MOB_SPAWNER, SPR_MOB_SPAWNER, 5.0f, 5.0f, 0, 0, true, false, false, TOOL_PICKAXE, 1, SND_METAL);
        registerBlock(53, "Wooden Stairs",    SHAPE_STAIR, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 2.0f, 5.0f, 0, 0, true, false, false, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true, true);
        registerBlock(54, "Chest",            SHAPE_CUBE, SPR_CHEST_TOP, SPR_CHEST_SIDE, SPR_CHEST_TOP, 2.5f, 2.5f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(54, SPR_CHEST_FRONT);
        registerBlock(55, "Redstone Wire",    SHAPE_REDSTONE_WIRE, SPR_REDSTONE_CROSS, SPR_REDSTONE_CROSS, SPR_REDSTONE_CROSS, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_STONE, TINT_REDSTONE);
        registerBlock(56, "Diamond Ore",      SHAPE_CUBE, SPR_DIAMOND_ORE, SPR_DIAMOND_ORE, SPR_DIAMOND_ORE, 3.0f, 5.0f, 0, 15, true, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(57, "Block of Diamond", SHAPE_CUBE, SPR_DIAMOND_BLOCK, SPR_DIAMOND_BLOCK, SPR_DIAMOND_BLOCK, 5.0f, 10.0f, 0, 15, true, true, true, TOOL_PICKAXE, 3, SND_METAL);
        registerBlock(58, "Crafting Table",   SHAPE_CUBE, SPR_CRAFTING_TOP, SPR_CRAFTING_SIDE_1, SPR_PLANKS, 2.5f, 2.5f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(58, SPR_CRAFTING_SIDE_2);
        registerBlock(59, "Wheat Crops",      SHAPE_CROP, SPR_WHEAT_7, SPR_WHEAT_7, SPR_WHEAT_7, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(60, "Farmland",         SHAPE_CUBE, SPR_FARMLAND_DRY, SPR_DIRT, SPR_DIRT, 0.6f, 0.6f, 0, 0, true, false, false, TOOL_SHOVEL, 0, SND_GRAVEL, TINT_NONE, false, false, true);
        registerBlock(61, "Furnace",          SHAPE_CUBE, SPR_FURNACE_TOP, SPR_FURNACE_SIDE, SPR_FURNACE_TOP, 3.5f, 3.5f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        setFrontTexture(61, SPR_FURNACE_FRONT_OFF);
        registerBlock(62, "Burning Furnace",  SHAPE_CUBE, SPR_FURNACE_TOP, SPR_FURNACE_SIDE, SPR_FURNACE_TOP, 3.5f, 3.5f, 13, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        setFrontTexture(62, SPR_FURNACE_FRONT_ON);
        registerBlock(63, "Standing Sign",    SHAPE_SLAB, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 1.0f, 1.0f, 0, 0, false, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(64, "Wooden Door",      SHAPE_DOOR, SPR_DOOR_WOOD_TOP, SPR_DOOR_WOOD_TOP, SPR_DOOR_WOOD_BOTTOM, 3.0f, 3.0f, 0, 0, true, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(65, "Ladder",           SHAPE_LADDER, SPR_LADDER, SPR_LADDER, SPR_LADDER, 0.4f, 0.4f, 0, 0, false, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(66, "Rail",             SHAPE_RAIL, SPR_RAIL_STRAIGHT, SPR_RAIL_STRAIGHT, SPR_RAIL_STRAIGHT, 0.7f, 0.7f, 0, 0, false, false, false, TOOL_PICKAXE, 0, SND_METAL);
        registerBlock(67, "Cobblestone Stairs",SHAPE_STAIR, SPR_COBBLESTONE, SPR_COBBLESTONE, SPR_COBBLESTONE, 2.0f, 10.0f, 0, 0, true, false, false, TOOL_PICKAXE, 1, SND_STONE, TINT_NONE, false, false, true);
        registerBlock(68, "Wall Sign",        SHAPE_LADDER, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 1.0f, 1.0f, 0, 0, false, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(69, "Lever",            SHAPE_LEVER, SPR_LEVER, SPR_LEVER, SPR_COBBLESTONE, 0.5f, 0.5f, 0, 0, false, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(70, "Stone Pressure Plate", SHAPE_PRESSURE_PLATE, SPR_STONE, SPR_STONE, SPR_STONE, 0.5f, 0.5f, 0, 0, false, false, false, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(71, "Iron Door",        SHAPE_DOOR, SPR_DOOR_IRON_TOP, SPR_DOOR_IRON_TOP, SPR_DOOR_IRON_BOTTOM, 5.0f, 5.0f, 0, 0, true, false, false, TOOL_PICKAXE, 1, SND_METAL);
        registerBlock(72, "Wooden Pressure Plate", SHAPE_PRESSURE_PLATE, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 0.5f, 0.5f, 0, 0, false, false, false, TOOL_AXE, 0, SND_WOOD);
        registerBlock(73, "Redstone Ore",     SHAPE_CUBE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, 3.0f, 5.0f, 0, 15, true, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(74, "Glowing Redstone Ore", SHAPE_CUBE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, SPR_REDSTONE_ORE, 3.0f, 5.0f, 9, 15, true, true, true, TOOL_PICKAXE, 3, SND_STONE);
        registerBlock(75, "Redstone Torch (Off)", SHAPE_TORCH, SPR_REDSTONE_TORCH_OFF, SPR_REDSTONE_TORCH_OFF, SPR_REDSTONE_TORCH_OFF, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(76, "Redstone Torch (On)",  SHAPE_TORCH, SPR_REDSTONE_TORCH_ON, SPR_REDSTONE_TORCH_ON, SPR_REDSTONE_TORCH_ON, 0.0f, 0.0f, 7, 0, false, false, false, TOOL_NONE, 0, SND_WOOD);
        registerBlock(77, "Stone Button",     SHAPE_BUTTON, SPR_STONE, SPR_STONE, SPR_STONE, 0.5f, 0.5f, 0, 0, false, false, false, TOOL_NONE, 0, SND_STONE);
        registerBlock(78, "Snow",             SHAPE_SNOW_LAYER, SPR_SNOW, SPR_SNOW, SPR_SNOW, 0.1f, 0.1f, 0, 0, false, false, false, TOOL_SHOVEL, 1, SND_SNOW, TINT_NONE, true);
        registerBlock(79, "Ice",              SHAPE_CUBE, SPR_ICE, SPR_ICE, SPR_ICE, 0.5f, 0.5f, 0, 3, true, false, false, TOOL_PICKAXE, 0, SND_GLASS, TINT_NONE, false, false, false, 0.98f);

        // IDs 80..96: Cryo Blocks, Xerophytes, Nether Stratum, Cake, Repeaters, Trapdoors
        registerBlock(80, "Snow Block",       SHAPE_CUBE, SPR_SNOW, SPR_SNOW, SPR_SNOW, 0.2f, 0.2f, 0, 15, true, true, true, TOOL_SHOVEL, 1, SND_SNOW);
        registerBlock(81, "Cactus",           SHAPE_CACTUS, SPR_CACTUS_TOP, SPR_CACTUS_SIDE, SPR_CACTUS_BOTTOM, 0.4f, 0.4f, 0, 0, true, false, false, TOOL_NONE, 0, SND_CLOTH);
        registerBlock(82, "Clay",             SHAPE_CUBE, SPR_CLAY, SPR_CLAY, SPR_CLAY, 0.6f, 0.6f, 0, 15, true, true, true, TOOL_SHOVEL, 0, SND_GRAVEL);
        registerBlock(83, "Sugar Canes",      SHAPE_CROSS, SPR_SUGAR_CANE, SPR_SUGAR_CANE, SPR_SUGAR_CANE, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_GRASS);
        registerBlock(84, "Jukebox",          SHAPE_CUBE, SPR_JUKEBOX_TOP, SPR_JUKEBOX_SIDE, SPR_JUKEBOX_SIDE, 2.0f, 10.0f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD);
        registerBlock(85, "Fence",            SHAPE_FENCE, SPR_PLANKS, SPR_PLANKS, SPR_PLANKS, 2.0f, 5.0f, 0, 0, true, false, false, TOOL_AXE, 0, SND_WOOD, TINT_NONE, false, true);
        registerBlock(86, "Pumpkin",          SHAPE_CUBE, SPR_PUMPKIN_TOP, SPR_PUMPKIN_SIDE, SPR_PUMPKIN_TOP, 1.0f, 1.0f, 0, 15, true, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(86, SPR_PUMPKIN_FRONT);
        registerBlock(87, "Netherrack",       SHAPE_CUBE, SPR_NETHERRACK, SPR_NETHERRACK, SPR_NETHERRACK, 0.4f, 0.4f, 0, 15, true, true, true, TOOL_PICKAXE, 1, SND_STONE);
        registerBlock(88, "Soul Sand",        SHAPE_CUBE, SPR_SOUL_SAND, SPR_SOUL_SAND, SPR_SOUL_SAND, 0.5f, 0.5f, 0, 15, true, true, true, TOOL_SHOVEL, 0, SND_SAND);
        registerBlock(89, "Glowstone",        SHAPE_CUBE, SPR_GLOWSTONE, SPR_GLOWSTONE, SPR_GLOWSTONE, 0.3f, 0.3f, 15, 15, true, true, true, TOOL_NONE, 0, SND_GLASS);
        registerBlock(90, "Portal",           SHAPE_PORTAL, SPR_PORTAL, SPR_PORTAL, SPR_PORTAL, -1.0f, 0.0f, 11, 0, false, false, false, TOOL_NONE, 99, SND_GLASS);
        registerBlock(91, "Jack o'Lantern",   SHAPE_CUBE, SPR_PUMPKIN_TOP, SPR_PUMPKIN_SIDE, SPR_PUMPKIN_TOP, 1.0f, 1.0f, 15, 15, true, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(91, SPR_JACK_O_LANTERN);
        registerBlock(92, "Cake",             SHAPE_CAKE, SPR_CAKE_TOP, SPR_CAKE_SIDE, SPR_CAKE_SIDE, 0.5f, 0.5f, 0, 0, true, false, false, TOOL_NONE, 0, SND_CLOTH);
        registerBlock(93, "Redstone Repeater (Off)", SHAPE_REPEATER, SPR_REPEATER_OFF, SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_SIDE, 0.0f, 0.0f, 0, 0, false, false, false, TOOL_NONE, 0, SND_STONE);
        registerBlock(94, "Redstone Repeater (On)",  SHAPE_REPEATER, SPR_REPEATER_ON,  SPR_DOUBLE_SLAB_SIDE, SPR_DOUBLE_SLAB_SIDE, 0.0f, 0.0f, 9, 0, false, false, false, TOOL_NONE, 0, SND_STONE);
        registerBlock(95, "Locked Chest",     SHAPE_CUBE, SPR_CHEST_TOP, SPR_CHEST_SIDE, SPR_CHEST_TOP, 0.0f, 0.0f, 15, 15, true, true, true, TOOL_AXE, 0, SND_WOOD);
        setFrontTexture(95, SPR_CHEST_FRONT);
        registerBlock(96, "Trapdoor",         SHAPE_TRAPDOOR, SPR_TRAPDOOR, SPR_TRAPDOOR, SPR_TRAPDOOR, 3.0f, 3.0f, 0, 0, true, false, false, TOOL_AXE, 0, SND_WOOD);
    }
};