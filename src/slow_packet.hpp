#pragma once

#include <cstdint>
#include <vector>
#include <stdexcept>

// Constantes de flags
enum Flags : uint8_t {
    FLAG_CONNECT = 0x01,
    FLAG_DATA    = 0x02,
    FLAG_ACK     = 0x04,
    FLAG_CLOSE   = 0x08
};

// Estrutura de pacote SLOW com campos fixos e payload variável
struct SLOWPacket {
    uint16_t sid;       // Session ID (UUID truncado)
    uint8_t flags;      // Flags (CONNECT, DATA, ACK, etc.)
    uint8_t sttl;       // TTL (time-to-live)
    uint16_t seq_num;   // Número de sequência
    uint16_t ack_num;   // Acknowledgment number
    uint16_t window;    // Tamanho da janela
    uint32_t fid;       // File ID ou stream ID
    uint32_t fo;        // File offset ou payload offset
    std::vector<uint8_t> payload;

    // Serializa struct em buffer de bytes (big endian)
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf;
        buf.reserve(2+1+1+2+2+2+4+4 + payload.size());
        auto append16 = [&](uint16_t v){ buf.push_back(v >> 8); buf.push_back(v & 0xFF); };
        auto append32 = [&](uint32_t v){
            buf.push_back((v >> 24) & 0xFF);
            buf.push_back((v >> 16) & 0xFF);
            buf.push_back((v >> 8) & 0xFF);
            buf.push_back(v & 0xFF);
        };
        append16(sid);
        buf.push_back(flags);
        buf.push_back(sttl);
        append16(seq_num);
        append16(ack_num);
        append16(window);
        append32(fid);
        append32(fo);
        buf.insert(buf.end(), payload.begin(), payload.end());
        return buf;
    }

    // Converte buffer de bytes em SLOWPacket (big endian)
    static SLOWPacket deserialize(const std::vector<uint8_t>& buf) {
        if (buf.size() < 18) // cabeçalho mínimo
            throw std::runtime_error("Buffer muito pequeno para SLOWPacket");
        SLOWPacket pkt;
        size_t i = 0;
        auto read16 = [&](void)->uint16_t {
            uint16_t v = (buf[i] << 8) | buf[i+1]; i += 2; return v;
        };
        auto read32 = [&](void)->uint32_t {
            uint32_t v = (buf[i] << 24) | (buf[i+1] << 16) | (buf[i+2] << 8) | buf[i+3];
            i += 4; return v;
        };
        pkt.sid      = read16();
        pkt.flags    = buf[i++];
        pkt.sttl     = buf[i++];
        pkt.seq_num  = read16();
        pkt.ack_num  = read16();
        pkt.window   = read16();
        pkt.fid      = read32();
        pkt.fo       = read32();
        // restante é payload
        pkt.payload.assign(buf.begin() + i, buf.end());
        return pkt;
    }
};