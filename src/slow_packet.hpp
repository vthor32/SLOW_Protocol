#pragma once

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cstring>

// Enumeração das flags do protocolo, com os valores de bit corretos.
enum SlowFlags : uint8_t {
    FLAG_MORE_BITS     = 1 << 0, // 1
    FLAG_ACCEPT_REJECT = 1 << 1, // 2
    FLAG_ACK           = 1 << 2, // 4
    FLAG_REVIVE        = 1 << 3, // 8
    FLAG_CONNECT       = 1 << 4  // 16
};

// Estrutura que representa um pacote SLOW, espelhando a especificação.
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

    // Converte a struct para uma sequência de bytes pronta para transmissão.
    std::vector<uint8_t> serialize() const {
        if (data.size() > 1440) {
            throw std::runtime_error("O campo 'data' excede o limite máximo de 1440 bytes.");
        }

        std::vector<uint8_t> buf;
        // Otimização para pré-alocar a memória necessária (32 bytes de cabeçalho + dados).
        buf.reserve(32 + data.size());

        buf.insert(buf.end(), sid.begin(), sid.end());

        // enpacotamento: sttl nos bits altos, flags nos bits baixos.
        uint32_t sttl_flags = ((sttl & 0x07FFFFFF) << 5) | (flags & 0x1F);
        // Serializa o campo combinado em 4 bytes, respeitando o formato little-endian.
        for (int i = 0; i < 4; ++i) {
            buf.push_back((sttl_flags >> (i * 8)) & 0xFF);
        }

        for (int i = 0; i < 4; ++i) {
            buf.push_back((seqnum >> (i * 8)) & 0xFF);
        }

        for (int i = 0; i < 4; ++i) {
            buf.push_back((acknum >> (i * 8)) & 0xFF);
        }

        // Empacota os campos window, fid e fo em um único inteiro de 32 bits.
        uint32_t win_fid_fo = (static_cast<uint32_t>(fo) << 24)
                             | (static_cast<uint32_t>(fid) << 16)
                             | (window & 0xFFFF);
        for (int i = 0; i < 4; ++i) {
            buf.push_back((win_fid_fo >> (i * 8)) & 0xFF);
        }

        buf.insert(buf.end(), data.begin(), data.end());
        return buf;
    }

    // Converte uma sequência de bytes recebida da rede de volta para a struct.
    static SLOWPacket deserialize(const std::vector<uint8_t>& buf) {
        // Um pacote SLOW válido deve ter no mínimo o tamanho do cabeçalho.
        if (buf.size() < 32) {
            throw std::runtime_error("Buffer muito pequeno para o cabeçalho SLOW");
        }

        SLOWPacket pkt;
        size_t offset = 0;

        std::copy_n(buf.begin(), 16, pkt.sid.begin());
        offset += 16;

        // Usa memcpy para ler 4 bytes do buffer e reconstruir o inteiro de 32 bits.
        uint32_t sttl_flags = 0;
        std::memcpy(&sttl_flags, &buf[offset], 4);
        // Desempacota os campos sttl e flags a partir do inteiro lido.
        pkt.flags = sttl_flags & 0x1F;
        pkt.sttl  = sttl_flags >> 5;
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