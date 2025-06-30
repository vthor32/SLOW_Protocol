#pragma once

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cstring>

enum SlowFlags : uint8_t {
    FLAG_CONNECT       = 1 << 0,
    FLAG_REVIVE        = 1 << 1,
    FLAG_ACK           = 1 << 2,
    FLAG_ACCEPT_REJECT = 1 << 3,
    FLAG_MORE_BITS     = 1 << 4
};

struct SLOWPacket {
    std::array<uint8_t, 16> sid;
    uint8_t  flags;
    uint32_t sttl;
    uint32_t seqnum;
    uint32_t acknum;
    uint16_t window;
    uint8_t  fid;
    uint8_t  fo;
    std::vector<uint8_t> data;

    std::vector<uint8_t> serialize() const {
        if (data.size() > 1440) { // [cite: 17]
            throw std::runtime_error("O campo 'data' excede o limite máximo de 1440 bytes.");
        }

        std::vector<uint8_t> buf;
        buf.reserve(32 + data.size());

        buf.insert(buf.end(), sid.begin(), sid.end());

        // --- CORREÇÃO APLICADA AQUI ---
        // Conforme o diagrama, flags estão nos bits 0-4 e sttl nos bits 5-31.
        uint32_t sttl_flags = (sttl << 5) | (flags & 0x1F);
        for (int i = 0; i < 4; ++i) {
            buf.push_back((sttl_flags >> (i * 8)) & 0xFF);
        }

        for (int i = 0; i < 4; ++i) {
            buf.push_back((seqnum >> (i * 8)) & 0xFF);
        }

        for (int i = 0; i < 4; ++i) {
            buf.push_back((acknum >> (i * 8)) & 0xFF);
        }

        // Esta parte já estava correta, pois window está nos bits menos significativos.
        uint32_t win_fid_fo = (static_cast<uint32_t>(fo) << 24) | (static_cast<uint32_t>(fid) << 16) | window;
        for (int i = 0; i < 4; ++i) {
            buf.push_back((win_fid_fo >> (i * 8)) & 0xFF);
        }

        buf.insert(buf.end(), data.begin(), data.end());

        return buf;
    }

    static SLOWPacket deserialize(const std::vector<uint8_t>& buf) {
        if (buf.size() < 32) {
            throw std::runtime_error("Buffer muito pequeno para o cabeçalho SLOW");
        }

        SLOWPacket pkt;
        size_t offset = 0;

        std::copy_n(buf.begin(), 16, pkt.sid.begin());
        offset += 16;

        // --- CORREÇÃO APLICADA AQUI ---
        uint32_t sttl_flags = 0;
        std::memcpy(&sttl_flags, &buf[offset], 4);
        pkt.flags = sttl_flags & 0x1F;
        pkt.sttl = (sttl_flags >> 5) & 0x07FFFFFF;
        offset += 4;

        std::memcpy(&pkt.seqnum, &buf[offset], 4);
        offset += 4;

        std::memcpy(&pkt.acknum, &buf[offset], 4);
        offset += 4;

        uint32_t win_fid_fo = 0;
        std::memcpy(&win_fid_fo, &buf[offset], 4);
        pkt.window = win_fid_fo & 0xFFFF;
        pkt.fid = (win_fid_fo >> 16) & 0xFF;
        pkt.fo = (win_fid_fo >> 24) & 0xFF;
        offset += 4;

        pkt.data.assign(buf.begin() + offset, buf.end());

        return pkt;
    }
};