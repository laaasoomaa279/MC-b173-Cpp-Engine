#pragma once
#include "miniz.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 McRegion (.mcr) & Big-Endian NBT Storage Engine
// ============================================================================
// 1:1 Reproduction of Mojang's Beta 1.3 - 1.7.3 McRegion Storage Format:
//   - Big-Endian Binary NBT Reader/Writer (Tags 0..11)
//   - Gzip-compressed level.dat (Spawn, Player Pos/Health, Inventory, Seed, Time)
//   - Zlib-compressed McRegion r.<rx>.<rz>.mcr files (8KB Header + 4KB Sectors)
//   - Exact XZY byte layout for 16x128x16 Columns:
//       Blocks[32768], Data[16384], SkyLight[16384], BlockLight[16384], HeightMap[256]
// ============================================================================

struct SavedSlotNBT {
    int8_t   slot   = 0;
    int16_t  id     = 0;
    int8_t   count  = 0;
    int16_t  damage = 0;
};

struct SavedTileEntityNBT {
    std::string               id; // "Chest", "Furnace", "Sign", "MobSpawner"
    int32_t                   x = 0, y = 0, z = 0;
    int16_t                   burnTime = 0;
    int16_t                   cookTime = 0;
    std::vector<SavedSlotNBT> items;
    std::string               signLines[4];
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
    void writeI8(int8_t v)   { buf.push_back((uint8_t)v); }
    void writeI16(int16_t v) {
        uint16_t u = (uint16_t)v;
        buf.push_back((uint8_t)(u >> 8));
        buf.push_back((uint8_t)(u & 0xFF));
    }
    void writeI32(int32_t v) {
        uint32_t u = (uint32_t)v;
        buf.push_back((uint8_t)(u >> 24));
        buf.push_back((uint8_t)((u >> 16) & 0xFF));
        buf.push_back((uint8_t)((u >> 8) & 0xFF));
        buf.push_back((uint8_t)(u & 0xFF));
    }
    void writeI64(int64_t v) {
        uint64_t u = (uint64_t)v;
        for (int s = 56; s >= 0; s -= 8) buf.push_back((uint8_t)((u >> s) & 0xFF));
    }
    void writeF32(float v) {
        uint32_t u = 0;
        std::memcpy(&u, &v, 4);
        writeI32((int32_t)u);
    }
    void writeF64(double v) {
        uint64_t u = 0;
        std::memcpy(&u, &v, 8);
        writeI64((int64_t)u);
    }
    void writeStringRaw(const std::string& s) {
        writeI16((int16_t)s.size());
        buf.insert(buf.end(), s.begin(), s.end());
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
        buf.insert(buf.end(), data, data + len);
    }
    void writeString(const std::string& name, const std::string& s) {
        beginTag(8, name);
        writeStringRaw(s);
    }
    void endCompound() { writeU8(0); }
};

class NbtReader {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;

public:
    NbtReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {}

    bool ok(size_t need = 1) const { return pos + need <= size; }
    uint8_t readU8() { return ok(1) ? data[pos++] : 0; }
    int8_t  readI8() { return (int8_t)readU8(); }
    int16_t readI16() {
        if (!ok(2)) { pos = size; return 0; }
        uint16_t v = ((uint16_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
        return (int16_t)v;
    }
    int32_t readI32() {
        if (!ok(4)) { pos = size; return 0; }
        uint32_t v = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos + 1] << 16) |
                     ((uint32_t)data[pos + 2] << 8) | data[pos + 3];
        pos += 4;
        return (int32_t)v;
    }
    int64_t readI64() {
        if (!ok(8)) { pos = size; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | data[pos++];
        return (int64_t)v;
    }
    float readF32() {
        uint32_t u = (uint32_t)readI32();
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    double readF64() {
        uint64_t u = (uint64_t)readI64();
        double d = 0.0;
        std::memcpy(&d, &u, 8);
        return d;
    }
    std::string readString() {
        int16_t len = readI16();
        if (len <= 0 || !ok((size_t)len)) return {};
        std::string s((const char*)(data + pos), (size_t)len);
        pos += (size_t)len;
        return s;
    }

    void skipTagPayload(uint8_t tagType) {
        switch (tagType) {
            case 1: pos += 1; break;
            case 2: pos += 2; break;
            case 3: pos += 4; break;
            case 4: pos += 8; break;
            case 5: pos += 4; break;
            case 6: pos += 8; break;
            case 7: { int32_t n = readI32(); if (n > 0) pos += (size_t)n; break; }
            case 8: { int16_t n = readI16(); if (n > 0) pos += (size_t)n; break; }
            case 9: {
                uint8_t elemType = readU8();
                int32_t count    = readI32();
                for (int32_t i = 0; i < count && ok(); ++i) skipTagPayload(elemType);
                break;
            }
            case 10: {
                while (ok()) {
                    uint8_t t = readU8();
                    if (t == 0) break;
                    readString();
                    skipTagPayload(t);
                }
                break;
            }
            case 11: { int32_t n = readI32(); if (n > 0) pos += (size_t)n * 4; break; }
            default: pos = size; break;
        }
        if (pos > size) pos = size;
    }
};

class McRegionStorage {
public:
    static bool saveLevelDat(const std::string& saveDir, const BetaLevelData& lvl) {
        std::filesystem::create_directories(saveDir);
        NbtWriter w;
        w.beginTag(10, "");
        w.beginTag(10, "Data");
        w.writeString("LevelName", lvl.levelName);
        w.writeLong("RandomSeed", lvl.seed);
        w.writeLong("Time", lvl.time);
        w.writeLong("LastPlayed", lvl.lastPlayed);
        w.writeInt("SpawnX", lvl.spawnX);
        w.writeInt("SpawnY", lvl.spawnY);
        w.writeInt("SpawnZ", lvl.spawnZ);
        w.writeInt("version", 19132); // Official McRegion format version ID!
        w.writeByte("raining", lvl.raining ? 1 : 0);
        w.writeInt("rainTime", lvl.rainTime);
        w.writeByte("thundering", lvl.thundering ? 1 : 0);
        w.writeInt("thunderTime", lvl.thunderTime);

        w.beginTag(10, "Player");
        w.writeInt("Dimension", lvl.dimension);
        w.writeShort("Health", lvl.health);

        w.beginTag(9, "Pos");
        w.writeU8(6); w.writeI32(3);
        w.writeF64(lvl.playerX); w.writeF64(lvl.playerY); w.writeF64(lvl.playerZ);

        w.beginTag(9, "Rotation");
        w.writeU8(5); w.writeI32(2);
        w.writeF32(lvl.playerYaw); w.writeF32(lvl.playerPitch);

        w.beginTag(9, "Inventory");
        w.writeU8(10); w.writeI32((int32_t)lvl.inventory.size());
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

        // Gzip wrap for level.dat
        mz_ulong compBound = mz_compressBound((mz_ulong)w.buf.size());
        std::vector<uint8_t> gz(10 + compBound + 8, 0);
        gz[0] = 0x1F; gz[1] = 0x8B; gz[2] = 0x08; gz[9] = 0x03;

        mz_stream strm{};
        mz_deflateInit2(&strm, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
        strm.next_in   = w.buf.data();
        strm.avail_in  = (unsigned int)w.buf.size();
        strm.next_out  = gz.data() + 10;
        strm.avail_out = (unsigned int)compBound;
        mz_deflate(&strm, MZ_FINISH);
        size_t deflatedLen = strm.total_out;
        mz_deflateEnd(&strm);

        mz_ulong crc = mz_crc32(MZ_CRC32_INIT, w.buf.data(), w.buf.size());
        uint32_t uSize = (uint32_t)w.buf.size();
        std::memcpy(gz.data() + 10 + deflatedLen, &crc, 4);
        std::memcpy(gz.data() + 14 + deflatedLen, &uSize, 4);
        gz.resize(18 + deflatedLen);

        std::ofstream out(saveDir + "/level.dat", std::ios::binary);
        if (!out.is_open()) return false;
        out.write((const char*)gz.data(), gz.size());
        return true;
    }

    static bool loadLevelDat(const std::string& saveDir, BetaLevelData& out) {
        std::ifstream f(saveDir + "/level.dat", std::ios::binary);
        if (!f.is_open()) return false;
        std::vector<uint8_t> gz((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (gz.size() < 18 || gz[0] != 0x1F || gz[1] != 0x8B) return false;

        size_t hdr = 10;
        uint8_t flg = gz[3];
        if (flg & 0x04) { hdr += 2 + (gz[hdr] | (gz[hdr + 1] << 8)); }
        if (flg & 0x08) { while (hdr < gz.size() && gz[hdr++] != 0) {} }
        if (flg & 0x10) { while (hdr < gz.size() && gz[hdr++] != 0) {} }
        if (flg & 0x02) { hdr += 2; }
        if (hdr + 8 >= gz.size()) return false;

        uint32_t rawSize = 0;
        std::memcpy(&rawSize, gz.data() + gz.size() - 4, 4);
        if (rawSize == 0 || rawSize > 16 * 1024 * 1024) rawSize = 65536;

        std::vector<uint8_t> raw(rawSize);
        mz_stream strm{};
        mz_inflateInit2(&strm, -MZ_DEFAULT_WINDOW_BITS);
        strm.next_in   = gz.data() + hdr;
        strm.avail_in  = (unsigned int)(gz.size() - hdr - 8);
        strm.next_out  = raw.data();
        strm.avail_out = (unsigned int)raw.size();
        int res = mz_inflate(&strm, MZ_FINISH);
        raw.resize(strm.total_out);
        mz_inflateEnd(&strm);
        if (res != MZ_STREAM_END && raw.empty()) return false;

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

    static bool saveChunk(const std::string& saveDir, const McRegionChunkData& chunk) {
        std::string regionDir = saveDir + "/region";
        std::filesystem::create_directories(regionDir);

        int32_t rx = chunk.xPos >> 5;
        int32_t rz = chunk.zPos >> 5;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/r.%d.%d.mcr", regionDir.c_str(), rx, rz);

        // Serialize chunk NBT
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

        w.beginTag(9, "Entities");
        w.writeU8(10); w.writeI32(0);

        w.beginTag(9, "TileEntities");
        w.writeU8(10); w.writeI32((int32_t)chunk.tileEntities.size());
        for (const auto& te : chunk.tileEntities) {
            w.writeString("id", te.id);
            w.writeInt("x", te.x);
            w.writeInt("y", te.y);
            w.writeInt("z", te.z);
            if (te.id == "Furnace") {
                w.writeShort("BurnTime", te.burnTime);
                w.writeShort("CookTime", te.cookTime);
            }
            if (!te.items.empty()) {
                w.beginTag(9, "Items");
                w.writeU8(10); w.writeI32((int32_t)te.items.size());
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
        w.endCompound(); // Level
        w.endCompound(); // Root

        mz_ulong compLen = mz_compressBound((mz_ulong)w.buf.size());
        std::vector<uint8_t> zbuf(compLen);
        if (mz_compress(zbuf.data(), &compLen, w.buf.data(), (mz_ulong)w.buf.size()) != MZ_OK) return false;
        zbuf.resize(compLen);

        std::vector<uint8_t> regionBytes;
        {
            std::ifstream in(path, std::ios::binary);
            if (in.is_open()) {
                regionBytes.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }
        }
        if (regionBytes.size() < 8192) regionBytes.resize(8192, 0);

        int localIdx = (chunk.xPos & 31) + (chunk.zPos & 31) * 32;
        uint32_t payloadLen = (uint32_t)(zbuf.size() + 1); // +1 for compression type byte (2 = Zlib)
        uint32_t neededSectors = (payloadLen + 4 + 4095) / 4096;

        uint32_t totalSectors = (uint32_t)(regionBytes.size() / 4096);
        uint32_t sectorOffset = totalSectors;

        // Reuse existing sector block if it fits
        uint32_t curLoc = ((uint32_t)regionBytes[localIdx * 4] << 16) |
                          ((uint32_t)regionBytes[localIdx * 4 + 1] << 8) |
                          (uint32_t)regionBytes[localIdx * 4 + 2];
        uint8_t curSecCount = regionBytes[localIdx * 4 + 3];
        if (curLoc >= 2 && curSecCount >= neededSectors) {
            sectorOffset = curLoc;
        } else {
            regionBytes.resize((size_t)(sectorOffset + neededSectors) * 4096, 0);
        }

        size_t bytePos = (size_t)sectorOffset * 4096;
        regionBytes[bytePos + 0] = (uint8_t)(payloadLen >> 24);
        regionBytes[bytePos + 1] = (uint8_t)((payloadLen >> 16) & 0xFF);
        regionBytes[bytePos + 2] = (uint8_t)((payloadLen >> 8) & 0xFF);
        regionBytes[bytePos + 3] = (uint8_t)(payloadLen & 0xFF);
        regionBytes[bytePos + 4] = 2; // Zlib
        std::memcpy(regionBytes.data() + bytePos + 5, zbuf.data(), zbuf.size());

        regionBytes[localIdx * 4 + 0] = (uint8_t)((sectorOffset >> 16) & 0xFF);
        regionBytes[localIdx * 4 + 1] = (uint8_t)((sectorOffset >> 8) & 0xFF);
        regionBytes[localIdx * 4 + 2] = (uint8_t)(sectorOffset & 0xFF);
        regionBytes[localIdx * 4 + 3] = (uint8_t)neededSectors;

        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) return false;
        out.write((const char*)regionBytes.data(), regionBytes.size());
        return true;
    }

    static bool loadChunk(const std::string& saveDir, int32_t cx, int32_t cz, McRegionChunkData& out) {
        int32_t rx = cx >> 5;
        int32_t rz = cz >> 5;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/region/r.%d.%d.mcr", saveDir.c_str(), rx, rz);

        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;

        int localIdx = (cx & 31) + (cz & 31) * 32;
        f.seekg(localIdx * 4, std::ios::beg);
        uint8_t loc[4]{};
        f.read((char*)loc, 4);
        uint32_t sectorOffset = ((uint32_t)loc[0] << 16) | ((uint32_t)loc[1] << 8) | loc[2];
        if (sectorOffset < 2) return false;

        f.seekg((std::streamoff)sectorOffset * 4096, std::ios::beg);
        uint8_t hdr[5]{};
        f.read((char*)hdr, 5);
        uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | hdr[3];
        if (len <= 1 || len > 1024 * 1024 || hdr[4] != 2) return false;

        std::vector<uint8_t> comp(len - 1);
        f.read((char*)comp.data(), comp.size());

        mz_ulong rawLen = 262144;
        std::vector<uint8_t> raw(rawLen);
        if (mz_uncompress(raw.data(), &rawLen, comp.data(), (mz_ulong)comp.size()) != MZ_OK) return false;
        raw.resize(rawLen);

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
                        r.readU8(); int32_t n = r.readI32();
                        if (n >= 3) { out.playerX = r.readF64(); out.playerY = r.readF64(); out.playerZ = r.readF64(); }
                    } else if (pt == 9 && pn == "Rotation") {
                        r.readU8(); int32_t n = r.readI32();
                        if (n >= 2) { out.playerYaw = r.readF32(); out.playerPitch = r.readF32(); }
                    } else if (pt == 9 && pn == "Inventory") {
                        r.readU8(); int32_t n = r.readI32();
                        out.inventory.clear();
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
                else for (int i = 0; i < n; ++i) r.readU8();
            } else if (t == 7 && name == "Data") {
                int32_t n = r.readI32();
                if (n == 16384) for (int i = 0; i < 16384; ++i) out.data[i] = r.readU8();
                else for (int i = 0; i < n; ++i) r.readU8();
            } else if (t == 7 && name == "SkyLight") {
                int32_t n = r.readI32();
                if (n == 16384) for (int i = 0; i < 16384; ++i) out.skyLight[i] = r.readU8();
                else for (int i = 0; i < n; ++i) r.readU8();
            } else if (t == 7 && name == "BlockLight") {
                int32_t n = r.readI32();
                if (n == 16384) for (int i = 0; i < 16384; ++i) out.blockLight[i] = r.readU8();
                else for (int i = 0; i < n; ++i) r.readU8();
            } else if (t == 7 && name == "HeightMap") {
                int32_t n = r.readI32();
                if (n == 256) for (int i = 0; i < 256; ++i) out.heightMap[i] = r.readU8();
                else for (int i = 0; i < n; ++i) r.readU8();
            } else if (t == 9 && name == "TileEntities") {
                r.readU8(); int32_t count = r.readI32();
                out.tileEntities.clear();
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
                        else if (tt == 9 && tn == "Items") {
                            r.readU8(); int32_t ic = r.readI32();
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
                            r.skipTagPayload(tt);
                        }
                    }
                    out.tileEntities.push_back(std::move(te));
                }
            } else {
                r.skipTagPayload(t);
            }
        }
    }
};