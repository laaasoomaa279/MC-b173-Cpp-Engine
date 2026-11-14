#pragma once
#include "platform.hpp"
#include "world/world.hpp"
#include "miniz.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Minecraft Beta 1.7.3 Network Protocol Engine (Protocol Version 14)
// ============================================================================
// 1:1 Implementation of Mojang's Beta 1.7.3 TCP Wire Protocol:
//   - Big-Endian network byte order & UCS-2 / UTF-16BE Strings (since Beta 1.5)
//   - Packet 0x00 KeepAlive, 0x01 Login (v14), 0x02 Handshake, 0x03 Chat,
//     0x04 Time Update, 0x06 Spawn Position, 0x0D Player Pos & Look,
//     0x0E Block Dig, 0x0F Block Place, 0x32 Pre-Chunk, 0x33 Map Chunk (Zlib
//     compressed XZY 16x128x16 column), 0x35 Block Change, 0xFF Kick/Disconnect
// ============================================================================

constexpr int32_t BETA_173_PROTOCOL_VERSION = 14;
constexpr uint16_t BETA_173_DEFAULT_PORT    = 25565;

enum BetaPacketId : uint8_t {
    PKT_KEEP_ALIVE         = 0x00,
    PKT_LOGIN_REQUEST      = 0x01,
    PKT_HANDSHAKE          = 0x02,
    PKT_CHAT_MESSAGE       = 0x03,
    PKT_TIME_UPDATE        = 0x04,
    PKT_ENTITY_EQUIPMENT   = 0x05,
    PKT_SPAWN_POSITION     = 0x06,
    PKT_USE_ENTITY         = 0x07,
    PKT_UPDATE_HEALTH      = 0x08,
    PKT_RESPAWN            = 0x09,
    PKT_PLAYER_GROUND      = 0x0A,
    PKT_PLAYER_POSITION    = 0x0B,
    PKT_PLAYER_LOOK        = 0x0C,
    PKT_PLAYER_POS_LOOK    = 0x0D,
    PKT_PLAYER_DIGGING     = 0x0E,
    PKT_PLAYER_BLOCK_PLACE = 0x0F,
    PKT_HOLDING_CHANGE     = 0x10,
    PKT_ANIMATION          = 0x12,
    PKT_NAMED_ENTITY_SPAWN = 0x14,
    PKT_PICKUP_SPAWN       = 0x15,
    PKT_COLLECT_ITEM       = 0x16,
    PKT_MOB_SPAWN          = 0x18,
    PKT_DESTROY_ENTITY     = 0x1D,
    PKT_ENTITY_REL_MOVE    = 0x1F,
    PKT_ENTITY_LOOK        = 0x20,
    PKT_ENTITY_LOOK_MOVE   = 0x21,
    PKT_ENTITY_TELEPORT    = 0x22,
    PKT_ENTITY_STATUS      = 0x26,
    PKT_PRE_CHUNK          = 0x32,
    PKT_MAP_CHUNK          = 0x33,
    PKT_MULTI_BLOCK_CHANGE = 0x34,
    PKT_BLOCK_CHANGE       = 0x35,
    PKT_EXPLOSION          = 0x3C,
    PKT_SOUND_EFFECT       = 0x3D,
    PKT_NEW_INVALID_STATE  = 0x46,
    PKT_THUNDERBOLT        = 0x47,
    PKT_DISCONNECT_KICK    = 0xFF
};

class BetaPacketBuffer {
public:
    std::vector<uint8_t> data;
    size_t               readCursor = 0;

    void clear() { data.clear(); readCursor = 0; }

    void writeU8(uint8_t v)  { data.push_back(v); }
    void writeI8(int8_t v)   { data.push_back((uint8_t)v); }
    void writeBool(bool v)   { data.push_back(v ? 1 : 0); }

    void writeI16(int16_t v) {
        uint16_t u = (uint16_t)v;
        data.push_back((uint8_t)(u >> 8));
        data.push_back((uint8_t)(u & 0xFF));
    }

    void writeI32(int32_t v) {
        uint32_t u = (uint32_t)v;
        data.push_back((uint8_t)(u >> 24));
        data.push_back((uint8_t)((u >> 16) & 0xFF));
        data.push_back((uint8_t)((u >> 8) & 0xFF));
        data.push_back((uint8_t)(u & 0xFF));
    }

    void writeI64(int64_t v) {
        uint64_t u = (uint64_t)v;
        for (int s = 56; s >= 0; s -= 8) data.push_back((uint8_t)((u >> s) & 0xFF));
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

    // Beta 1.7.3 String16: 16-bit big-endian character length followed by UTF-16BE code units
    void writeString16(const std::string& ascii) {
        writeI16((int16_t)ascii.size());
        for (unsigned char c : ascii) {
            data.push_back(0x00);
            data.push_back(c);
        }
    }

    bool canRead(size_t bytes) const { return readCursor + bytes <= data.size(); }

    uint8_t readU8() { return canRead(1) ? data[readCursor++] : 0; }
    int8_t  readI8() { return (int8_t)readU8(); }
    int16_t readI16() {
        if (!canRead(2)) return 0;
        uint16_t u = ((uint16_t)data[readCursor] << 8) | data[readCursor + 1];
        readCursor += 2;
        return (int16_t)u;
    }
    int32_t readI32() {
        if (!canRead(4)) return 0;
        uint32_t u = ((uint32_t)data[readCursor] << 24) | ((uint32_t)data[readCursor + 1] << 16) |
                     ((uint32_t)data[readCursor + 2] << 8) | data[readCursor + 3];
        readCursor += 4;
        return (int32_t)u;
    }
    int64_t readI64() {
        if (!canRead(8)) return 0;
        uint64_t u = 0;
        for (int i = 0; i < 8; ++i) u = (u << 8) | data[readCursor++];
        return (int64_t)u;
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
    std::string readString16() {
        int16_t len = readI16();
        if (len <= 0 || !canRead((size_t)len * 2)) return {};
        std::string out;
        out.reserve((size_t)len);
        for (int16_t i = 0; i < len; ++i) {
            uint8_t hi = data[readCursor++];
            uint8_t lo = data[readCursor++];
            (void)hi;
            out.push_back((char)lo);
        }
        return out;
    }

    // Builders for Core Beta 1.7.3 Packets
    static BetaPacketBuffer buildHandshake(const std::string& username) {
        BetaPacketBuffer pkt;
        pkt.writeU8(PKT_HANDSHAKE);
        pkt.writeString16(username);
        return pkt;
    }

    static BetaPacketBuffer buildLoginRequest(const std::string& username, int64_t mapSeed = 0, int8_t dimension = 0) {
        BetaPacketBuffer pkt;
        pkt.writeU8(PKT_LOGIN_REQUEST);
        pkt.writeI32(BETA_173_PROTOCOL_VERSION);
        pkt.writeString16(username);
        pkt.writeI64(mapSeed);
        pkt.writeI8(dimension);
        return pkt;
    }

    static BetaPacketBuffer buildChatMessage(const std::string& message) {
        BetaPacketBuffer pkt;
        pkt.writeU8(PKT_CHAT_MESSAGE);
        pkt.writeString16(message.substr(0, 119));
        return pkt;
    }

    static BetaPacketBuffer buildPlayerPosLook(double x, double feetY, double stanceEyeY, double z,
                                               float yaw, float pitch, bool onGround) {
        BetaPacketBuffer pkt;
        pkt.writeU8(PKT_PLAYER_POS_LOOK);
        pkt.writeF64(x);
        pkt.writeF64(feetY);
        pkt.writeF64(stanceEyeY);
        pkt.writeF64(z);
        pkt.writeF32(yaw);
        pkt.writeF32(pitch);
        pkt.writeBool(onGround);
        return pkt;
    }

    static BetaPacketBuffer buildBlockChange(int32_t wx, int8_t wy, int32_t wz, uint8_t blockId, uint8_t meta) {
        BetaPacketBuffer pkt;
        pkt.writeU8(PKT_BLOCK_CHANGE);
        pkt.writeI32(wx);
        pkt.writeI8(wy);
        pkt.writeI32(wz);
        pkt.writeI8((int8_t)blockId);
        pkt.writeI8((int8_t)meta);
        return pkt;
    }

    // Packet 0x33 Map Chunk (81920 bytes raw XZY: Blocks 32768 + Data 16384 + BlockLight 16384 + SkyLight 16384)
    static BetaPacketBuffer buildMapChunkPacket(const WorldMap& world, const ChunkColumn& col) {
        McRegionChunkData mcr;
        world.exportChunkToMcRegion(col, mcr);

        std::vector<uint8_t> raw(81920);
        std::memcpy(raw.data() + 0,     mcr.blocks,     32768);
        std::memcpy(raw.data() + 32768, mcr.data,       16384);
        std::memcpy(raw.data() + 49152, mcr.blockLight, 16384);
        std::memcpy(raw.data() + 65536, mcr.skyLight,   16384);

        mz_ulong compLen = mz_compressBound((mz_ulong)raw.size());
        std::vector<uint8_t> zbuf(compLen);
        mz_compress(zbuf.data(), &compLen, raw.data(), (mz_ulong)raw.size());

        BetaPacketBuffer pkt;
        pkt.writeU8(PKT_MAP_CHUNK);
        pkt.writeI32(col.chunkX * 16);
        pkt.writeI16(0);
        pkt.writeI32(col.chunkZ * 16);
        pkt.writeI8(15);  // Size X - 1
        pkt.writeI8(127); // Size Y - 1
        pkt.writeI8(15);  // Size Z - 1
        pkt.writeI32((int32_t)compLen);
        pkt.data.insert(pkt.data.end(), zbuf.begin(), zbuf.begin() + compLen);
        return pkt;
    }
};

class BetaNetworkClient {
    SOCKET sock = INVALID_SOCKET;
    bool   wsaInitialized = false;

public:
    bool connected = false;

    ~BetaNetworkClient() { disconnect(); }

    bool connectToServer(const char* host, uint16_t port, const std::string& username) {
        if (!wsaInitialized) {
            WSADATA wsa{};
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
            wsaInitialized = true;
        }

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);

        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }

        u_long nonBlocking = 1;
        ioctlsocket(sock, FIONBIO, &nonBlocking);
        connected = true;

        sendPacket(BetaPacketBuffer::buildHandshake(username));
        sendPacket(BetaPacketBuffer::buildLoginRequest(username));
        return true;
    }

    bool sendPacket(const BetaPacketBuffer& pkt) {
        if (!connected || sock == INVALID_SOCKET || pkt.data.empty()) return false;
        int sent = send(sock, (const char*)pkt.data.data(), (int)pkt.data.size(), 0);
        return sent == (int)pkt.data.size();
    }

    void disconnect() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        connected = false;
        if (wsaInitialized) {
            WSACleanup();
            wsaInitialized = false;
        }
    }
};