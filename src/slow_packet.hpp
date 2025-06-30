#pragma once

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cstring>

// Define as flags do protocolo SLOW, conforme especificação no PDF.
enum SlowFlags : uint8_t {
    FLAG_CONNECT       = 1 << 0, // C - Connect: pacote de início de 3-way connect
    FLAG_REVIVE        = 1 << 1, // R - Revive: pacote de 0-way connect
    FLAG_ACK           = 1 << 2, // ACK - Ack: confirma o recebimento de um pacote
    FLAG_ACCEPT_REJECT = 1 << 3, // A/R - Accept/Reject: aceita ou rejeita uma conexão
    FLAG_MORE_BITS     = 1 << 4  // MB - More Bits: indica que o pacote foi fragmentado
};

// Representa um pacote SLOW, seguindo o diagrama da especificação.
// O cabeçalho completo tem 32 bytes.
struct SLOWPacket {
    std::array<uint8_t, 16> sid; // 128 bits
    uint8_t  flags;              // 5 bits
    uint32_t sttl;               // 27 bits
    uint32_t seqnum;             // 32 bits
    uint32_t acknum;             // 32 bits
    uint16_t window;             // 16 bits
    uint8_t  fid;                // 8 bits
    uint8_t  fo;                 // 8 bits
    std::vector<uint8_t> data;   // até 1440 bytes

    // Serializa o pacote para um buffer de bytes em little-endian.
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf;
        buf.reserve(32 + data.size());

        // 1. SID (128 bits / 16 bytes)
        buf.insert(buf.end(), sid.begin(), sid.end());

        // 2. Flags (5 bits) + STTL (27 bits) empacotados em 32 bits
        uint32_t flags_sttl = (sttl << 5) | (flags & 0x1F);
        for(int i = 0; i < 4; ++i) {
            buf.push_back((flags_sttl >> (i * 8)) & 0xFF); // Little-endian
        }

        // 3. Seqnum (32 bits)
        for(int i = 0; i < 4; ++i) {
            buf.push_back((seqnum >> (i * 8)) & 0xFF); // Little-endian
        }

        // 4. Acknum (32 bits)
        for(int i = 0; i < 4; ++i) {
            buf.push_back((acknum >> (i * 8)) & 0xFF); // Little-endian
        }

        // 5. Window (16 bits) + FID (8 bits) + FO (8 bits) empacotados em 32 bits
        uint32_t win_fid_fo = (static_cast<uint32_t>(window) << 16) | (static_cast<uint32_t>(fid) << 8) | fo;
        for(int i = 0; i < 4; ++i) {
            buf.push_back((win_fid_fo >> (i * 8)) & 0xFF); // Little-endian
        }

        // 6. Data
        buf.insert(buf.end(), data.begin(), data.end());

        return buf;
    }

    // Desserializa um buffer de bytes para um objeto SLOWPacket.
    static SLOWPacket deserialize(const std::vector<uint8_t>& buf) {
        if (buf.size() < 32) {
            throw std::runtime_error("Buffer muito pequeno para o cabeçalho SLOW (mínimo 32 bytes)");
        }
        
        SLOWPacket pkt;
        size_t offset = 0;

        // 1. SID
        std::copy_n(buf.begin(), 16, pkt.sid.begin());
        offset += 16;

        // 2. Flags + STTL
        uint32_t flags_sttl = 0;
        std::memcpy(&flags_sttl, &buf[offset], 4);
        pkt.flags = flags_sttl & 0x1F;
        pkt.sttl = flags_sttl >> 5;
        offset += 4;

        // 3. Seqnum
        std::memcpy(&pkt.seqnum, &buf[offset], 4);
        offset += 4;

        // 4. Acknum
        std::memcpy(&pkt.acknum, &buf[offset], 4);
        offset += 4;

        // 5. Window + FID + FO
        uint32_t win_fid_fo = 0;
        std::memcpy(&win_fid_fo, &buf[offset], 4);
        pkt.fo = win_fid_fo & 0xFF;
        pkt.fid = (win_fid_fo >> 8) & 0xFF;
        pkt.window = (win_fid_fo >> 16) & 0xFFFF;
        offset += 4;

        // 6. Data
        pkt.data.assign(buf.begin() + offset, buf.end());

        return pkt;
    }
};