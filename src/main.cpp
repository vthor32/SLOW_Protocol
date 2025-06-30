#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <array>
#include "slow_packet.hpp"

// Função auxiliar para imprimir o SID no formato UUID
void print_sid(const std::array<uint8_t, 16>& sid) {
    std::cout << std::hex << std::setfill('0');
    for (size_t i = 0; i < sid.size(); ++i) {
        std::cout << std::setw(2) << static_cast<int>(sid[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            std::cout << "-";
        }
    }
    std::cout << std::dec << std::setfill(' ');
}

void print_bytes(const std::vector<uint8_t>& buf) {
    std::cout << "Serialized (" << buf.size() << " bytes): ";
    for (auto b : buf) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << ' ';
    std::cout << std::dec << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== SLOW Peripheral v1.0 ===" << std::endl;
    if (argc < 2 || argc > 3) {
        std::cerr << "Uso: " << argv[0] << " <host> [porta]" << std::endl;
        return 1;
    }
    const char* host = argv[1];
    const char* port = (argc == 3 ? argv[2] : "7033");

    struct addrinfo hints{}, *res, *rp;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    int gai_err;
    if ((gai_err = getaddrinfo(host, port, &hints, &res)) != 0) {
        std::cerr << "Erro ao resolver '" << host << ":" << port << "': " << gai_strerror(gai_err) << std::endl;
        return 1;
    }

    int sock = -1;
    for (rp = res; rp != nullptr; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock >= 0) break;
    }
    if (sock < 0) {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    struct timeval tv{2, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Montando o pacote CONNECT para o 3-way connect
    SLOWPacket connect_pkt{};
    connect_pkt.sid = {};
    connect_pkt.flags = FLAG_CONNECT;
    connect_pkt.sttl = 0;
    connect_pkt.seqnum = 0;
    connect_pkt.acknum = 0;
    connect_pkt.window = 4096;
    connect_pkt.fid = 0;
    connect_pkt.fo = 0;
    connect_pkt.data = {};

    auto buf = connect_pkt.serialize();

    const int max_retries = 5;
    int attempts = 0;
    bool connected = false;
    std::vector<uint8_t> rbuf(1472);
    struct sockaddr_storage src;
    socklen_t srclen = sizeof(src);

    // Variáveis para guardar o estado da sessão
    std::array<uint8_t, 16> session_sid;
    uint32_t session_sttl;

    char addrstr[INET6_ADDRSTRLEN];
    void* addrptr = (rp->ai_family == AF_INET)
                     ? (void*)&((struct sockaddr_in*)rp->ai_addr)->sin_addr
                     : (void*)&((struct sockaddr_in6*)rp->ai_addr)->sin6_addr;
    inet_ntop(rp->ai_family, addrptr, addrstr, sizeof(addrstr));
    std::cout << "Resolvendo para " << addrstr << ":" << port << std::endl;

    while (attempts < max_retries && !connected) {
        print_bytes(buf);
        std::cout << "Enviando CONNECT (tentativa " << attempts + 1 << ")..." << std::endl;
        if (sendto(sock, buf.data(), buf.size(), 0, rp->ai_addr, rp->ai_addrlen) < 0) {
            perror("sendto");
            break;
        }

        ssize_t len = recvfrom(sock, rbuf.data(), rbuf.size(), 0, reinterpret_cast<struct sockaddr*>(&src), &srclen);
        if (len < 0) {
            perror("recvfrom");
            std::cerr << "Timeout aguardando resposta do Central." << std::endl;
            attempts++;
        } else {
            rbuf.resize(len);
            std::cout << "Resposta recebida:" << std::endl;
            try {
                auto resp = SLOWPacket::deserialize(rbuf);
                // A resposta de Setup deve ter a flag Accept (A/R) ligada
                if (resp.flags & FLAG_ACCEPT_REJECT) {
                    std::cout << "Conexão ACEITA pelo Central." << std::endl;
                    session_sid = resp.sid;
                    session_sttl = resp.sttl;
                    std::cout << "  > Session ID: ";
                    print_sid(session_sid);
                    std::cout << std::endl << "  > Session STTL: " << session_sttl << " ms" << std::endl;
                    connected = true;
                } else {
                    std::cerr << "Conexão REJEITADA pelo Central (flags=" << (int)resp.flags << ")" << std::endl;
                    break;
                }
            } catch (const std::exception& e) {
                std::cerr << "Erro ao decodificar resposta: " << e.what() << std::endl;
                break;
            }
        }
    }

    if (!connected) {
        std::cerr << "Falha ao estabelecer conexão após " << max_retries << " tentativas." << std::endl;
    } else {
        std::cout << "\nConexão estabelecida com sucesso. Próximos passos a implementar:" << std::endl;
        std::cout << "1. Enviar pacote de dados (Data) para confirmar o Setup." << std::endl;
        std::cout << "2. Implementar a lógica de envio e recebimento de dados com ACKs." << std::endl;
        std::cout << "3. Implementar a mensagem de Disconnect." << std::endl;
    }

    freeaddrinfo(res);
    close(sock);
    return 0;
}