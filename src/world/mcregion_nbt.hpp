#pragma once
#include "miniz.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <algorithm>

// ============================================================================
//   - Storage Architecture:
//       * Big-Endian Binary NBT Reader/Writer (Tags 0..11) with safe empty
//         TAG_List (elemType == 0) handling and bounds-checked payload skips.
//       * Gzip-compressed level.dat (version = 19132, RandomSeed, LevelName,
//         SpawnX/Y/Z, Time, LastPlayed, raining/rainTime, thundering/thunderTime,
//         and full Player compound with Dimension, Health, Pos, Rotation, and
//         Inventory including Armor slots 100..103).
//       * Thread-safe 8KB Region Header (4KB sector location table + 4KB
//         Unix timestamp table), 4096-byte sector alignment/padding, dynamic
//         free-sector bitmap reuse (zero file bloat), and dual Gzip (1) /
//         Zlib (2) decompression support.
//       * Exact XZY 16x128x16 column arrays (Blocks 32768, Data 16384,
//         SkyLight 16384, BlockLight 16384, HeightMap 256), Entities list,
//         and full TileEntities (Chest, Furnace, Sign Text1..4, MobSpawner).
// ============================================================================

struct SavedSlotNBT {
    int8_t   slot   = 0;
    int16_t  id     = 0;
    int8_t   count  = 0;
    int16_t  damage = 0;
};

struct SavedTileEntityNBT {
    std::string               id; // "Chest", "Furnace", "Sign", "MobSpawner", "RecordPlayer", "Trap"
    int32_t                   x = 0, y = 0, z = 0;
    int16_t                   burnTime    = 0;
    int16_t                   maxBurnTime = 0;
    int16_t                   cookTime    = 0;
    std::vector<SavedSlotNBT> items;
    std::string               signLines[4];
    std::string               spawnerEntityId = "Pig";
    int16_t                   spawnerDelay    = 20;
};

struct SavedEntityNBT {
    std::string id; // "Zombie", "Creeper", "Skeleton", "Spider", "Pig", "Sheep", "Cow", "Ghast", "Item"
    double      x = 0.0, y = 0.0, z = 0.0;
    double      vx = 0.0, vy = 0.0, vz = 0.0;
    float       yaw = 0.0f, pitch = 0.0f;
    int16_t     health   = 20;
    int16_t     fire     = -1;
    int16_t     age      = 0;
    bool        sheared  = false;
    int8_t      color    = 0;
    SavedSlotNBT item{};
};

struct McRegionChunkData {
    int32_t                         xPos             = 0;
    int32_t                         zPos             = 0;
    int64_t                         lastUpdate       = 0;
    bool                            terrainPopulated = true;
    uint8_t                         blocks[32768]{};     // XZY: (x * 16 + z) * 128 + y
    uint8_t                         data[16384]{};       // 4-bit metadata nibble
    uint8_t                         skyLight[16384]{};   // 4-bit sky light nibble
    uint8_t                         blockLight[16384]{}; // 4-bit block light nibble
    uint8_t                         heightMap[256]{};    // z * 16 + x
    std::vector<SavedEntityNBT>     entities;
    std::vector<SavedTileEntityNBT> tileEntities;
};

struct BetaLevelData {
    std::string               levelName   = "New World";
    int64_t                   seed        = 123456789LL;
    int64_t                   time        = 0;
    int64_t                   lastPlayed  = 0;
    int32_t                   spawnX      = 0;
    int32_t                   spawnY      = 64;
    int32_t                   spawnZ      = 0;
    int32_t                   dimension   = 0;
    double                    playerX     = 0.5;
    double                    playerY     = 66.0;
    double                    playerZ     = 0.5;
    float                     playerYaw   = 0.0f;
    float                     playerPitch = 0.0f;
    int16_t                   health      = 20;
    bool                      raining     = false;
    int32_t                   rainTime    = 24000;
    bool                      thundering  = false;
    int32_t                   thunderTime = 24000;
    std::vector<SavedSlotNBT> inventory;
};

class NbtWriter {
public:
    std::vector<uint8_t> buf;

    void writeU8(uint8_t v)  { buf.push_back(v); }
    void writeI8(int8_t v)   { buf.push_back(static_cast<uint8_t>(v)); }
    void writeI16(int16_t v) {
        uint16_t u = static_cast<uint16_t>(v);
        buf.push_back(static_cast<uint8_t>(u >> 8));
        buf.push_back(static_cast<uint8_t>(u & 0xFF));
    }
    void writeI32(int32_t v) {
        uint32_t u = static_cast<uint32_t>(v);
        buf.push_back(static_cast<uint8_t>(u >> 24));
        buf.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(u & 0xFF));
    }
    void writeI64(int64_t v) {
        uint64_t u = static_cast<uint64_t>(v);
        for (int s = 56; s >= 0; s -= 8) {
            buf.push_back(static_cast<uint8_t>((u >> s) & 0xFF));
        }
    }
    void writeF32(float v) {
        uint32_t u = 0;
        std::memcpy(&u, &v, 4);
        writeI32(static_cast<int32_t>(u));
    }
    void writeF64(double v) {
        uint64_t u = 0;
        std::memcpy(&u, &v, 8);
        writeI64(static_cast<int64_t>(u));
    }
    void writeStringRaw(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), 32767));
        writeI16(static_cast<int16_t>(len));
        buf.insert(buf.end(), s.begin(), s.begin() + len);
    }
    void beginTag(uint8_t tagType, const std::string& name) {
        writeU8(tagType);
        writeStringRaw(name);
    }
    void writeByte(const std::string& name, int8_t v)   { beginTag(1, name); writeI8(v); }
    void writeShort(const std::string& name, int16_t v) { beginTag(2, name); writeI16(v); }
    void writeInt(const std::string& name, int32_t v)   { beginTag(3, name); writeI32(v); }
    void writeLong(const std::string& name, int64_t v)  { beginTag(4, name); writeI64(v); }
    void writeFloat(const std::string& name, float v)   { beginTag(5, name); writeF32(v); }
    void writeDouble(const std::string& name, double v) { beginTag(6, name); writeF64(v); }
    void writeByteArray(const std::string& name, const uint8_t* data, int32_t len) {
        beginTag(7, name);
        writeI32(len);
        if (data && len > 0) {
            buf.insert(buf.end(), data, data + len);
        }
    }
    void writeString(const std::string& name, const std::string& s) {
        beginTag(8, name);
        writeStringRaw(s);
    }
    void writeListHeader(const std::string& name, uint8_t elemType, int32_t count) {
        beginTag(9, name);
        writeU8(elemType);
        writeI32(count);
    }
    void writeIntArray(const std::string& name, const int32_t* data, int32_t count) {
        beginTag(11, name);
        writeI32(count);
        for (int32_t i = 0; i < count; ++i) writeI32(data[i]);
    }
    void endCompound() { writeU8(0); }
};

class NbtReader {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;

public:
    NbtReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {}

    bool ok(size_t need = 1) const { return data != nullptr && pos + need <= size; }
    uint8_t readU8() { return ok(1) ? data[pos++] : 0; }
    int8_t  readI8() { return static_cast<int8_t>(readU8()); }
    int16_t readI16() {
        if (!ok(2)) { pos = size; return 0; }
        uint16_t v = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        return static_cast<int16_t>(v);
    }
    int32_t readI32() {
        if (!ok(4)) { pos = size; return 0; }
        uint32_t v = (static_cast<uint32_t>(data[pos]) << 24) |
                     (static_cast<uint32_t>(data[pos + 1]) << 16) |
                     (static_cast<uint32_t>(data[pos + 2]) << 8) |
                      static_cast<uint32_t>(data[pos + 3]);
        pos += 4;
        return static_cast<int32_t>(v);
    }
    int64_t readI64() {
        if (!ok(8)) { pos = size; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | data[pos++];
        return static_cast<int64_t>(v);
    }
    float readF32() {
        uint32_t u = static_cast<uint32_t>(readI32());
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    double readF64() {
        uint64_t u = static_cast<uint64_t>(readI64());
        double d = 0.0;
        std::memcpy(&d, &u, 8);
        return d;
    }
    std::string readString() {
        int16_t len = readI16();
        if (len <= 0) return {};
        size_t ulen = static_cast<size_t>(len);
        if (!ok(ulen)) { pos = size; return {}; }
        std::string s(reinterpret_cast<const char*>(data + pos), ulen);
        pos += ulen;
        return s;
    }

    void skipBytes(size_t count) {
        if (pos + count > size) pos = size;
        else pos += count;
    }

    void skipTagPayload(uint8_t tagType, int depth = 0) {
        if (depth > 64 || !ok(0)) { pos = size; return; }
        switch (tagType) {
            case 0: break; // TAG_End (can appear as elemType of an empty TAG_List in Beta 1.7.3!)
            case 1: skipBytes(1); break;
            case 2: skipBytes(2); break;
            case 3: skipBytes(4); break;
            case 4: skipBytes(8); break;
            case 5: skipBytes(4); break;
            case 6: skipBytes(8); break;
            case 7: {
                int32_t n = readI32();
                if (n < 0) pos = size;
                else skipBytes(static_cast<size_t>(n));
                break;
            }
            case 8: {
                int16_t n = readI16();
                if (n < 0) pos = size;
                else skipBytes(static_cast<size_t>(n));
                break;
            }
            case 9: {
                uint8_t elemType = readU8();
                int32_t count    = readI32();
                if (count < 0) { pos = size; break; }
                for (int32_t i = 0; i < count && ok(); ++i) {
                    skipTagPayload(elemType, depth + 1);
                }
                break;
            }
            case 10: {
                while (ok()) {
                    uint8_t t = readU8();
                    if (t == 0) break;
                    readString();
                    skipTagPayload(t, depth + 1);
                }
                break;
            }
            case 11: {
                int32_t n = readI32();
                if (n < 0) pos = size;
                else skipBytes(static_cast<size_t>(n) * 4);
                break;
            }
            default:
                pos = size;
                break;
        }
    }
};

class McRegionStorage {
public:
    static bool saveLevelDat(const std::string& saveDir, const BetaLevelData& lvl) {
        std::lock_guard<std::mutex> ioLock(getRegionIoMutex());
        std::error_code ec;
        std::filesystem::create_directories(saveDir, ec);

        int64_t nowMs = static_cast<int64_t>(std::time(nullptr)) * 1000LL;
        int64_t playedTimestamp = (lvl.lastPlayed > 0) ? lvl.lastPlayed : nowMs;

        NbtWriter w;
        w.beginTag(10, "");
        w.beginTag(10, "Data");
        w.writeString("LevelName", lvl.levelName);
        w.writeLong("RandomSeed", lvl.seed);
        w.writeLong("Time", lvl.time);
        w.writeLong("LastPlayed", playedTimestamp);
        w.writeInt("SpawnX", lvl.spawnX);
        w.writeInt("SpawnY", lvl.spawnY);
        w.writeInt("SpawnZ", lvl.spawnZ);
        w.writeInt("version", 19132); // Official McRegion format version ID (Beta 1.3 - 1.7.3)
        w.writeLong("SizeOnDisk", 0);
        w.writeByte("raining", lvl.raining ? 1 : 0);
        w.writeInt("rainTime", lvl.rainTime);
        w.writeByte("thundering", lvl.thundering ? 1 : 0);
        w.writeInt("thunderTime", lvl.thunderTime);

        w.beginTag(10, "Player");
        w.writeInt("Dimension", lvl.dimension);
        w.writeShort("Health", lvl.health);
        w.writeShort("HurtTime", 0);
        w.writeShort("DeathTime", 0);
        w.writeShort("Air", 300);
        w.writeShort("Fire", -20);
        w.writeFloat("FallDistance", 0.0f);
        w.writeByte("OnGround", 1);

        w.writeListHeader("Pos", 6, 3);
        w.writeF64(lvl.playerX);
        w.writeF64(lvl.playerY);
        w.writeF64(lvl.playerZ);

        w.writeListHeader("Motion", 6, 3);
        w.writeF64(0.0);
        w.writeF64(0.0);
        w.writeF64(0.0);

        w.writeListHeader("Rotation", 5, 2);
        w.writeF32(lvl.playerYaw);
        w.writeF32(lvl.playerPitch);

        w.writeListHeader("Inventory", 10, static_cast<int32_t>(lvl.inventory.size()));
        for (const auto& sl : lvl.inventory) {
            w.writeByte("Slot", sl.slot);
            w.writeShort("id", sl.id);
            w.writeByte("Count", sl.count);
            w.writeShort("Damage", sl.damage);
            w.endCompound();
        }
        w.endCompound(); // Player
        w.endCompound(); // Data
        w.endCompound(); // Root

        // Gzip wrap for level.dat (RFC 1952 header + raw deflate + CRC32 + ISIZE)
        mz_ulong compBound = mz_compressBound(static_cast<mz_ulong>(w.buf.size())) + 64;
        std::vector<uint8_t> gz(10 + compBound + 8, 0);
        gz[0] = 0x1F;
        gz[1] = 0x8B;
        gz[2] = 0x08;
        gz[9] = 0x03;

        mz_stream strm{};
        if (mz_deflateInit2(&strm, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK) {
            return false;
        }
        strm.next_in   = w.buf.data();
        strm.avail_in  = static_cast<unsigned int>(w.buf.size());
        strm.next_out  = gz.data() + 10;
        strm.avail_out = static_cast<unsigned int>(compBound);
        mz_deflate(&strm, MZ_FINISH);
        size_t deflatedLen = strm.total_out;
        mz_deflateEnd(&strm);

        mz_ulong crc = mz_crc32(MZ_CRC32_INIT, w.buf.data(), w.buf.size());
        uint32_t crc32Val = static_cast<uint32_t>(crc);
        uint32_t uSize    = static_cast<uint32_t>(w.buf.size());
        std::memcpy(gz.data() + 10 + deflatedLen, &crc32Val, 4);
        std::memcpy(gz.data() + 14 + deflatedLen, &uSize, 4);
        gz.resize(18 + deflatedLen);

        // Atomic write via level.dat_new (exactSaveHandlerOld.saveWorldInfo behavior)
        std::string tmpPath   = saveDir + "/level.dat_new";
        std::string finalPath = saveDir + "/level.dat";
        std::string oldPath   = saveDir + "/level.dat_old";

        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            out.write(reinterpret_cast<const char*>(gz.data()), static_cast<std::streamsize>(gz.size()));
            if (!out.good()) return false;
        }

        if (std::filesystem::exists(finalPath, ec)) {
            std::filesystem::remove(oldPath, ec);
            std::filesystem::rename(finalPath, oldPath, ec);
        }
        std::filesystem::rename(tmpPath, finalPath, ec);
        return true;
    }

    static bool loadLevelDat(const std::string& saveDir, BetaLevelData& out) {
        std::lock_guard<std::mutex> ioLock(getRegionIoMutex());
        std::ifstream f(saveDir + "/level.dat", std::ios::binary);
        if (!f.is_open()) {
            f.open(saveDir + "/level.dat_old", std::ios::binary);
            if (!f.is_open()) return false;
        }

        std::vector<uint8_t> gz((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<uint8_t> raw;
        if (!inflateGzipStream(gz, raw) || raw.empty()) return false;

        NbtReader r(raw.data(), raw.size());
        if (r.readU8() != 10) return false;
        r.readString();

        while (r.ok()) {
            uint8_t t = r.readU8();
            if (t == 0) break;
            std::string name = r.readString();
            if (t == 10 && name == "Data") {
                parseDataCompound(r, out);
            } else {
                r.skipTagPayload(t);
            }
        }
        return true;
    }

    // ========================================================================
    //RegionFile.java Exact 4KB Sector Bitmap & Timestamp McRegion Writer
    // ========================================================================
    static bool saveChunk(const std::string& saveDir, const McRegionChunkData& chunk) {
        std::lock_guard<std::mutex> ioLock(getRegionIoMutex());
        std::string regionDir = saveDir + "/region";
        std::error_code ec;
        std::filesystem::create_directories(regionDir, ec);

        int32_t rx = chunk.xPos >> 5;
        int32_t rz = chunk.zPos >> 5;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/r.%d.%d.mcr", regionDir.c_str(), rx, rz);

        // 1. Serialize Chunk NBT ("Level" compound)
        NbtWriter w;
        w.beginTag(10, "");
        w.beginTag(10, "Level");
        w.writeInt("xPos", chunk.xPos);
        w.writeInt("zPos", chunk.zPos);
        w.writeLong("LastUpdate", chunk.lastUpdate);
        w.writeByte("TerrainPopulated", chunk.terrainPopulated ? 1 : 0);
        w.writeByteArray("Blocks", chunk.blocks, 32768);
        w.writeByteArray("Data", chunk.data, 16384);
        w.writeByteArray("SkyLight", chunk.skyLight, 16384);
        w.writeByteArray("BlockLight", chunk.blockLight, 16384);
        w.writeByteArray("HeightMap", chunk.heightMap, 256);

        // Entities list
        if (chunk.entities.empty()) {
            w.writeListHeader("Entities", 10, 0);
        } else {
            w.writeListHeader("Entities", 10, static_cast<int32_t>(chunk.entities.size()));
            for (const auto& ent : chunk.entities) {
                w.writeString("id", ent.id);
                w.writeListHeader("Pos", 6, 3);
                w.writeF64(ent.x); w.writeF64(ent.y); w.writeF64(ent.z);
                w.writeListHeader("Motion", 6, 3);
                w.writeF64(ent.vx); w.writeF64(ent.vy); w.writeF64(ent.vz);
                w.writeListHeader("Rotation", 5, 2);
                w.writeF32(ent.yaw); w.writeF32(ent.pitch);
                w.writeShort("Health", ent.health);
                w.writeShort("Fire", ent.fire);
                if (ent.id == "Sheep") {
                    w.writeByte("Sheared", ent.sheared ? 1 : 0);
                    w.writeByte("Color", ent.color);
                } else if (ent.id == "Item") {
                    w.writeShort("Age", ent.age);
                    w.beginTag(10, "Item");
                    w.writeShort("id", ent.item.id);
                    w.writeByte("Count", ent.item.count);
                    w.writeShort("Damage", ent.item.damage);
                    w.endCompound();
                }
                w.endCompound();
            }
        }

        // TileEntities list (Chest, Furnace, Sign, MobSpawner)
        if (chunk.tileEntities.empty()) {
            w.writeListHeader("TileEntities", 10, 0);
        } else {
            w.writeListHeader("TileEntities", 10, static_cast<int32_t>(chunk.tileEntities.size()));
            for (const auto& te : chunk.tileEntities) {
                w.writeString("id", te.id);
                w.writeInt("x", te.x);
                w.writeInt("y", te.y);
                w.writeInt("z", te.z);
                if (te.id == "Furnace") {
                    w.writeShort("BurnTime", te.burnTime);
                    w.writeShort("CookTime", te.cookTime);
                } else if (te.id == "Sign") {
                    w.writeString("Text1", te.signLines[0]);
                    w.writeString("Text2", te.signLines[1]);
                    w.writeString("Text3", te.signLines[2]);
                    w.writeString("Text4", te.signLines[3]);
                } else if (te.id == "MobSpawner") {
                    w.writeString("EntityId", te.spawnerEntityId);
                    w.writeShort("Delay", te.spawnerDelay);
                }
                if (!te.items.empty()) {
                    w.writeListHeader("Items", 10, static_cast<int32_t>(te.items.size()));
                    for (const auto& it : te.items) {
                        w.writeByte("Slot", it.slot);
                        w.writeShort("id", it.id);
                        w.writeByte("Count", it.count);
                        w.writeShort("Damage", it.damage);
                        w.endCompound();
                    }
                }
                w.endCompound();
            }
        }
        w.endCompound(); // Level
        w.endCompound(); // Root

        // 2. Zlib compress chunk NBT payload
        mz_ulong compLen = mz_compressBound(static_cast<mz_ulong>(w.buf.size()));
        std::vector<uint8_t> zbuf(compLen);
        if (mz_compress(zbuf.data(), &compLen, w.buf.data(), static_cast<mz_ulong>(w.buf.size())) != MZ_OK) {
            return false;
        }
        zbuf.resize(compLen);

        // 3. Read existing .mcr region file & pad to 4096-byte sector boundary
        std::vector<uint8_t> regionBytes;
        {
            std::ifstream in(path, std::ios::binary);
            if (in.is_open()) {
                regionBytes.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }
        }
        if (regionBytes.size() < 8192) {
            regionBytes.resize(8192, 0);
        }
        size_t rem = regionBytes.size() & 4095u;
        if (rem != 0) {
            regionBytes.resize(regionBytes.size() + (4096u - rem), 0);
        }

        // 4. BuildRegionFile sectorFree bitmap
        uint32_t totalSectors = static_cast<uint32_t>(regionBytes.size() / 4096);
        std::vector<uint8_t> sectorFree(totalSectors, 1);
        sectorFree[0] = 0; // Sector 0: Chunk offset table
        sectorFree[1] = 0; // Sector 1: Chunk timestamp table

        for (int i = 0; i < 1024; ++i) {
            uint32_t loc = (static_cast<uint32_t>(regionBytes[i * 4 + 0]) << 24) |
                           (static_cast<uint32_t>(regionBytes[i * 4 + 1]) << 16) |
                           (static_cast<uint32_t>(regionBytes[i * 4 + 2]) << 8) |
                            static_cast<uint32_t>(regionBytes[i * 4 + 3]);
            if (loc != 0) {
                uint32_t off = loc >> 8;
                uint32_t cnt = loc & 0xFFu;
                for (uint32_t s = 0; s < cnt && (off + s) < totalSectors; ++s) {
                    sectorFree[off + s] = 0;
                }
            }
        }

        int localIdx = (chunk.xPos & 31) + (chunk.zPos & 31) * 32;
        uint32_t oldLoc = (static_cast<uint32_t>(regionBytes[localIdx * 4 + 0]) << 24) |
                          (static_cast<uint32_t>(regionBytes[localIdx * 4 + 1]) << 16) |
                          (static_cast<uint32_t>(regionBytes[localIdx * 4 + 2]) << 8) |
                           static_cast<uint32_t>(regionBytes[localIdx * 4 + 3]);
        uint32_t oldSectorOffset = oldLoc >> 8;
        uint32_t oldSectorCount  = oldLoc & 0xFFu;

        uint32_t payloadLen    = static_cast<uint32_t>(zbuf.size() + 1); // +1 for version byte (2 = Zlib)
        uint32_t neededSectors = (payloadLen + 4 + 4095) / 4096;
        if (neededSectors >= 256) return false; // McRegion single-chunk limit (1MB)

        uint32_t targetOffset = 0;
        if (oldSectorOffset >= 2 && oldSectorCount == neededSectors && (oldSectorOffset + neededSectors) <= totalSectors) {
            targetOffset = oldSectorOffset;
        } else {
            // Free previously occupied sectors
            for (uint32_t s = 0; s < oldSectorCount && (oldSectorOffset + s) < totalSectors; ++s) {
                sectorFree[oldSectorOffset + s] = 1;
            }

            // First-fit contiguous free sector search
            uint32_t runStart = 0;
            uint32_t runLen   = 0;
            for (uint32_t s = 2; s < totalSectors; ++s) {
                if (sectorFree[s]) {
                    if (runLen == 0) runStart = s;
                    if (++runLen >= neededSectors) {
                        targetOffset = runStart;
                        break;
                    }
                } else {
                    runLen = 0;
                }
            }

            // Append new 4KB sectors at EOF if no contiguous gap fits
            if (targetOffset == 0) {
                targetOffset = totalSectors;
                regionBytes.resize(static_cast<size_t>(targetOffset + neededSectors) * 4096, 0);
            }
        }

        // 5. Write chunk length + compression byte (2) + Zlib data into target sector
        size_t bytePos = static_cast<size_t>(targetOffset) * 4096;
        regionBytes[bytePos + 0] = static_cast<uint8_t>((payloadLen >> 24) & 0xFF);
        regionBytes[bytePos + 1] = static_cast<uint8_t>((payloadLen >> 16) & 0xFF);
        regionBytes[bytePos + 2] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
        regionBytes[bytePos + 3] = static_cast<uint8_t>(payloadLen & 0xFF);
        regionBytes[bytePos + 4] = 2; // Compression type 2 = Zlib
        std::memcpy(regionBytes.data() + bytePos + 5, zbuf.data(), zbuf.size());

        // 6. Update Sector 0 Offset Table & Sector 1 Timestamp Table
        uint32_t newLoc = (targetOffset << 8) | (neededSectors & 0xFFu);
        regionBytes[localIdx * 4 + 0] = static_cast<uint8_t>((newLoc >> 24) & 0xFF);
        regionBytes[localIdx * 4 + 1] = static_cast<uint8_t>((newLoc >> 16) & 0xFF);
        regionBytes[localIdx * 4 + 2] = static_cast<uint8_t>((newLoc >> 8) & 0xFF);
        regionBytes[localIdx * 4 + 3] = static_cast<uint8_t>(newLoc & 0xFF);

        uint32_t ts = static_cast<uint32_t>(std::time(nullptr));
        size_t tsOff = 4096 + static_cast<size_t>(localIdx) * 4;
        regionBytes[tsOff + 0] = static_cast<uint8_t>((ts >> 24) & 0xFF);
        regionBytes[tsOff + 1] = static_cast<uint8_t>((ts >> 16) & 0xFF);
        regionBytes[tsOff + 2] = static_cast<uint8_t>((ts >> 8) & 0xFF);
        regionBytes[tsOff + 3] = static_cast<uint8_t>(ts & 0xFF);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(regionBytes.data()), static_cast<std::streamsize>(regionBytes.size()));
        return out.good();
    }

    static bool loadChunk(const std::string& saveDir, int32_t cx, int32_t cz, McRegionChunkData& out) {
        std::lock_guard<std::mutex> ioLock(getRegionIoMutex());
        int32_t rx = cx >> 5;
        int32_t rz = cz >> 5;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/region/r.%d.%d.mcr", saveDir.c_str(), rx, rz);

        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;

        int localIdx = (cx & 31) + (cz & 31) * 32;
        f.seekg(localIdx * 4, std::ios::beg);
        uint8_t loc[4]{};
        f.read(reinterpret_cast<char*>(loc), 4);
        if (!f.good()) return false;

        uint32_t sectorOffset = (static_cast<uint32_t>(loc[0]) << 16) |
                                (static_cast<uint32_t>(loc[1]) << 8) |
                                 static_cast<uint32_t>(loc[2]);
        uint32_t sectorCount  = static_cast<uint32_t>(loc[3]);
        if (sectorOffset < 2 || sectorCount == 0) return false;

        f.seekg(static_cast<std::streamoff>(sectorOffset) * 4096, std::ios::beg);
        uint8_t hdr[5]{};
        f.read(reinterpret_cast<char*>(hdr), 5);
        if (!f.good()) return false;

        uint32_t len = (static_cast<uint32_t>(hdr[0]) << 24) |
                       (static_cast<uint32_t>(hdr[1]) << 16) |
                       (static_cast<uint32_t>(hdr[2]) << 8) |
                        static_cast<uint32_t>(hdr[3]);
        uint8_t compType = hdr[4];
        if (len <= 1 || len > sectorCount * 4096 || (compType != 1 && compType != 2)) {
            return false;
        }

        std::vector<uint8_t> comp(len - 1);
        f.read(reinterpret_cast<char*>(comp.data()), static_cast<std::streamsize>(comp.size()));
        if (!f.good()) return false;

        std::vector<uint8_t> raw;
        if (compType == 2) {
            if (!inflateZlibStream(comp, raw)) return false;
        } else {
            if (!inflateGzipStream(comp, raw)) return false;
        }
        if (raw.empty()) return false;

        NbtReader r(raw.data(), raw.size());
        if (r.readU8() != 10) return false;
        r.readString();

        while (r.ok()) {
            uint8_t t = r.readU8();
            if (t == 0) break;
            std::string name = r.readString();
            if (t == 10 && name == "Level") {
                parseChunkLevelCompound(r, out);
            } else {
                r.skipTagPayload(t);
            }
        }
        return true;
    }

private:
    static std::mutex& getRegionIoMutex() {
        static std::mutex mtx;
        return mtx;
    }

    static bool inflateZlibStream(const std::vector<uint8_t>& comp, std::vector<uint8_t>& out) {
        if (comp.empty()) return false;
        out.resize(131072);
        mz_stream strm{};
        if (mz_inflateInit(&strm) != MZ_OK) return false;
        strm.next_in  = comp.data();
        strm.avail_in = static_cast<unsigned int>(comp.size());

        while (true) {
            if (strm.total_out >= out.size()) {
                if (out.size() >= 16 * 1024 * 1024) { mz_inflateEnd(&strm); return false; }
                out.resize(out.size() * 2);
            }
            strm.next_out  = out.data() + strm.total_out;
            strm.avail_out = static_cast<unsigned int>(out.size() - strm.total_out);
            int res = mz_inflate(&strm, MZ_NO_FLUSH);
            if (res == MZ_STREAM_END) {
                out.resize(strm.total_out);
                mz_inflateEnd(&strm);
                return true;
            }
            if (res != MZ_OK) {
                mz_inflateEnd(&strm);
                return false;
            }
        }
    }

    static bool inflateGzipStream(const std::vector<uint8_t>& gz, std::vector<uint8_t>& out) {
        if (gz.size() < 18 || gz[0] != 0x1F || gz[1] != 0x8B) return false;
        size_t hdr = 10;
        uint8_t flg = gz[3];
        if (flg & 0x04) {
            if (hdr + 2 > gz.size()) return false;
            hdr += 2 + (static_cast<size_t>(gz[hdr]) | (static_cast<size_t>(gz[hdr + 1]) << 8));
        }
        if (flg & 0x08) { while (hdr < gz.size() && gz[hdr++] != 0) {} }
        if (flg & 0x10) { while (hdr < gz.size() && gz[hdr++] != 0) {} }
        if (flg & 0x02) { hdr += 2; }
        if (hdr + 8 >= gz.size()) return false;

        out.resize(65536);
        mz_stream strm{};
        if (mz_inflateInit2(&strm, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
        strm.next_in  = gz.data() + hdr;
        strm.avail_in = static_cast<unsigned int>(gz.size() - hdr - 8);

        while (true) {
            if (strm.total_out >= out.size()) {
                if (out.size() >= 16 * 1024 * 1024) { mz_inflateEnd(&strm); return false; }
                out.resize(out.size() * 2);
            }
            strm.next_out  = out.data() + strm.total_out;
            strm.avail_out = static_cast<unsigned int>(out.size() - strm.total_out);
            int res = mz_inflate(&strm, MZ_NO_FLUSH);
            if (res == MZ_STREAM_END) {
                out.resize(strm.total_out);
                mz_inflateEnd(&strm);
                return true;
            }
            if (res != MZ_OK) {
                mz_inflateEnd(&strm);
                return false;
            }
        }
    }

    static void parseDataCompound(NbtReader& r, BetaLevelData& out) {
        while (r.ok()) {
            uint8_t t = r.readU8();
            if (t == 0) break;
            std::string name = r.readString();
            if (t == 8 && name == "LevelName")        out.levelName   = r.readString();
            else if (t == 4 && name == "RandomSeed")  out.seed        = r.readI64();
            else if (t == 4 && name == "Time")        out.time        = r.readI64();
            else if (t == 4 && name == "LastPlayed")  out.lastPlayed  = r.readI64();
            else if (t == 3 && name == "SpawnX")      out.spawnX      = r.readI32();
            else if (t == 3 && name == "SpawnY")      out.spawnY      = r.readI32();
            else if (t == 3 && name == "SpawnZ")      out.spawnZ      = r.readI32();
            else if (t == 1 && name == "raining")     out.raining     = r.readI8() != 0;
            else if (t == 3 && name == "rainTime")    out.rainTime    = r.readI32();
            else if (t == 1 && name == "thundering")  out.thundering  = r.readI8() != 0;
            else if (t == 3 && name == "thunderTime") out.thunderTime = r.readI32();
            else if (t == 10 && name == "Player") {
                while (r.ok()) {
                    uint8_t pt = r.readU8();
                    if (pt == 0) break;
                    std::string pn = r.readString();
                    if (pt == 3 && pn == "Dimension")  out.dimension = r.readI32();
                    else if (pt == 2 && pn == "Health")out.health    = r.readI16();
                    else if (pt == 9 && pn == "Pos") {
                        uint8_t et = r.readU8(); int32_t n = r.readI32();
                        if (et == 6 && n >= 3) {
                            out.playerX = r.readF64();
                            out.playerY = r.readF64();
                            out.playerZ = r.readF64();
                            for (int32_t k = 3; k < n; ++k) r.skipTagPayload(et);
                        } else {
                            for (int32_t k = 0; k < n && r.ok(); ++k) r.skipTagPayload(et);
                        }
                    } else if (pt == 9 && pn == "Rotation") {
                        uint8_t et = r.readU8(); int32_t n = r.readI32();
                        if (et == 5 && n >= 2) {
                            out.playerYaw   = r.readF32();
                            out.playerPitch = r.readF32();
                            for (int32_t k = 2; k < n; ++k) r.skipTagPayload(et);
                        } else {
                            for (int32_t k = 0; k < n && r.ok(); ++k) r.skipTagPayload(et);
                        }
                    } else if (pt == 9 && pn == "Inventory") {
                        uint8_t et = r.readU8(); int32_t n = r.readI32();
                        out.inventory.clear();
                        if (et == 10 && n > 0) {
                            for (int32_t i = 0; i < n && r.ok(); ++i) {
                                SavedSlotNBT sl;
                                while (r.ok()) {
                                    uint8_t st = r.readU8();
                                    if (st == 0) break;
                                    std::string sn = r.readString();
                                    if (st == 1 && sn == "Slot")        sl.slot   = r.readI8();
                                    else if (st == 2 && sn == "id")     sl.id     = r.readI16();
                                    else if (st == 1 && sn == "Count")  sl.count  = r.readI8();
                                    else if (st == 2 && sn == "Damage") sl.damage = r.readI16();
                                    else r.skipTagPayload(st);
                                }
                                out.inventory.push_back(sl);
                            }
                        } else {
                            for (int32_t i = 0; i < n && r.ok(); ++i) r.skipTagPayload(et);
                        }
                    } else {
                        r.skipTagPayload(pt);
                    }
                }
            } else {
                r.skipTagPayload(t);
            }
        }
    }

    static void parseChunkLevelCompound(NbtReader& r, McRegionChunkData& out) {
        while (r.ok()) {
            uint8_t t = r.readU8();
            if (t == 0) break;
            std::string name = r.readString();
            if (t == 3 && name == "xPos")                  out.xPos = r.readI32();
            else if (t == 3 && name == "zPos")             out.zPos = r.readI32();
            else if (t == 4 && name == "LastUpdate")       out.lastUpdate = r.readI64();
            else if (t == 1 && name == "TerrainPopulated") out.terrainPopulated = r.readI8() != 0;
            else if (t == 7 && name == "Blocks") {
                int32_t n = r.readI32();
                if (n == 32768) for (int i = 0; i < 32768; ++i) out.blocks[i] = r.readU8();
                else if (n > 0) r.skipBytes(static_cast<size_t>(n));
            } else if (t == 7 && name == "Data") {
                int32_t n = r.readI32();
                if (n == 16384) for (int i = 0; i < 16384; ++i) out.data[i] = r.readU8();
                else if (n > 0) r.skipBytes(static_cast<size_t>(n));
            } else if (t == 7 && name == "SkyLight") {
                int32_t n = r.readI32();
                if (n == 16384) for (int i = 0; i < 16384; ++i) out.skyLight[i] = r.readU8();
                else if (n > 0) r.skipBytes(static_cast<size_t>(n));
            } else if (t == 7 && name == "BlockLight") {
                int32_t n = r.readI32();
                if (n == 16384) for (int i = 0; i < 16384; ++i) out.blockLight[i] = r.readU8();
                else if (n > 0) r.skipBytes(static_cast<size_t>(n));
            } else if (t == 7 && name == "HeightMap") {
                int32_t n = r.readI32();
                if (n == 256) for (int i = 0; i < 256; ++i) out.heightMap[i] = r.readU8();
                else if (n > 0) r.skipBytes(static_cast<size_t>(n));
            } else if (t == 9 && name == "Entities") {
                uint8_t et = r.readU8(); int32_t count = r.readI32();
                out.entities.clear();
                if (et == 10 && count > 0) {
                    for (int32_t i = 0; i < count && r.ok(); ++i) {
                        SavedEntityNBT ent;
                        while (r.ok()) {
                            uint8_t tt = r.readU8();
                            if (tt == 0) break;
                            std::string tn = r.readString();
                            if (tt == 8 && tn == "id")          ent.id = r.readString();
                            else if (tt == 2 && tn == "Health") ent.health = r.readI16();
                            else if (tt == 2 && tn == "Fire")   ent.fire = r.readI16();
                            else if (tt == 2 && tn == "Age")    ent.age = r.readI16();
                            else if (tt == 1 && tn == "Sheared")ent.sheared = r.readI8() != 0;
                            else if (tt == 1 && tn == "Color")  ent.color = r.readI8();
                            else if (tt == 9 && tn == "Pos") {
                                uint8_t lt = r.readU8(); int32_t ln = r.readI32();
                                if (lt == 6 && ln >= 3) {
                                    ent.x = r.readF64(); ent.y = r.readF64(); ent.z = r.readF64();
                                    for (int32_t k = 3; k < ln; ++k) r.skipTagPayload(lt);
                                } else {
                                    for (int32_t k = 0; k < ln; ++k) r.skipTagPayload(lt);
                                }
                            } else if (tt == 9 && tn == "Motion") {
                                uint8_t lt = r.readU8(); int32_t ln = r.readI32();
                                if (lt == 6 && ln >= 3) {
                                    ent.vx = r.readF64(); ent.vy = r.readF64(); ent.vz = r.readF64();
                                    for (int32_t k = 3; k < ln; ++k) r.skipTagPayload(lt);
                                } else {
                                    for (int32_t k = 0; k < ln; ++k) r.skipTagPayload(lt);
                                }
                            } else if (tt == 9 && tn == "Rotation") {
                                uint8_t lt = r.readU8(); int32_t ln = r.readI32();
                                if (lt == 5 && ln >= 2) {
                                    ent.yaw = r.readF32(); ent.pitch = r.readF32();
                                    for (int32_t k = 2; k < ln; ++k) r.skipTagPayload(lt);
                                } else {
                                    for (int32_t k = 0; k < ln; ++k) r.skipTagPayload(lt);
                                }
                            } else if (tt == 10 && tn == "Item") {
                                while (r.ok()) {
                                    uint8_t it = r.readU8();
                                    if (it == 0) break;
                                    std::string in = r.readString();
                                    if (it == 2 && in == "id")          ent.item.id = r.readI16();
                                    else if (it == 1 && in == "Count")  ent.item.count = r.readI8();
                                    else if (it == 2 && in == "Damage") ent.item.damage = r.readI16();
                                    else r.skipTagPayload(it);
                                }
                            } else {
                                r.skipTagPayload(tt);
                            }
                        }
                        out.entities.push_back(std::move(ent));
                    }
                } else {
                    for (int32_t i = 0; i < count && r.ok(); ++i) r.skipTagPayload(et);
                }
            } else if (t == 9 && name == "TileEntities") {
                uint8_t et = r.readU8(); int32_t count = r.readI32();
                out.tileEntities.clear();
                if (et == 10 && count > 0) {
                    for (int32_t i = 0; i < count && r.ok(); ++i) {
                        SavedTileEntityNBT te;
                        while (r.ok()) {
                            uint8_t tt = r.readU8();
                            if (tt == 0) break;
                            std::string tn = r.readString();
                            if (tt == 8 && tn == "id")            te.id = r.readString();
                            else if (tt == 3 && tn == "x")        te.x = r.readI32();
                            else if (tt == 3 && tn == "y")        te.y = r.readI32();
                            else if (tt == 3 && tn == "z")        te.z = r.readI32();
                            else if (tt == 2 && tn == "BurnTime") te.burnTime = r.readI16();
                            else if (tt == 2 && tn == "CookTime") te.cookTime = r.readI16();
                            else if (tt == 8 && tn == "Text1")    te.signLines[0] = r.readString();
                            else if (tt == 8 && tn == "Text2")    te.signLines[1] = r.readString();
                            else if (tt == 8 && tn == "Text3")    te.signLines[2] = r.readString();
                            else if (tt == 8 && tn == "Text4")    te.signLines[3] = r.readString();
                            else if (tt == 8 && tn == "EntityId") te.spawnerEntityId = r.readString();
                            else if (tt == 2 && tn == "Delay")    te.spawnerDelay = r.readI16();
                            else if (tt == 9 && tn == "Items") {
                                uint8_t lt = r.readU8(); int32_t ic = r.readI32();
                                if (lt == 10 && ic > 0) {
                                    for (int32_t k = 0; k < ic && r.ok(); ++k) {
                                        SavedSlotNBT sl;
                                        while (r.ok()) {
                                            uint8_t st = r.readU8();
                                            if (st == 0) break;
                                            std::string sn = r.readString();
                                            if (st == 1 && sn == "Slot")        sl.slot   = r.readI8();
                                            else if (st == 2 && sn == "id")     sl.id     = r.readI16();
                                            else if (st == 1 && sn == "Count")  sl.count  = r.readI8();
                                            else if (st == 2 && sn == "Damage") sl.damage = r.readI16();
                                            else r.skipTagPayload(st);
                                        }
                                        te.items.push_back(sl);
                                    }
                                } else {
                                    for (int32_t k = 0; k < ic && r.ok(); ++k) r.skipTagPayload(lt);
                                }
                            } else {
                                r.skipTagPayload(tt);
                            }
                        }
                        out.tileEntities.push_back(std::move(te));
                    }
                } else {
                    for (int32_t i = 0; i < count && r.ok(); ++i) r.skipTagPayload(et);
                }
            } else {
                r.skipTagPayload(t);
            }
        }
    }
};