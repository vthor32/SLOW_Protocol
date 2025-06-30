/*
Project: SLOW Peripheral
Author: <Vitor Marçal Brasil>
Date: Junho 2025
*/

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include "slow_packet.hpp"

int main(int argc, char* argv[]) {
    std::cout << "=== SLOW Peripheral v0.1 ===" << std::endl;
    if (argc < 2 || argc > 3) {
        std::cerr << "Uso: " << argv[0] << " <host_do_central> [porta]" << std::endl;
        return 1;
    }
    const char* host = argv[1];
    const char* port = (argc == 3 ? argv[2] : "7033");

    // Resolves host e porta
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        std::cerr << "Falha ao resolver " << host << ":" << port << std::endl;
        return 1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    std::cout << "Enviando CONNECT para " << host << ":" << port << std::endl;
    SLOWPacket pkt;
    pkt.sid     = 0;
    pkt.flags   = FLAG_CONNECT;
    pkt.sttl    = 64;
    pkt.seq_num = 0;
    pkt.ack_num = 0;
    pkt.window  = 1024;
    pkt.fid     = 0;
    pkt.fo      = 0;

    auto buffer = pkt.serialize();
    
    // Envia buffer via sendto()
    ssize_t sent = sendto(sock, buffer.data(), buffer.size(), 0,
        res->ai_addr, res->ai_addrlen);
    if (sent < 0) perror("sendto");


    close(sock);
    freeaddrinfo(res);
    return 0;
}
