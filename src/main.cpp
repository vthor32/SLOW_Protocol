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
#include <fstream>
#include "slow_packet.hpp"

constexpr uint16_t DEFAULT_WINDOW_SIZE = 1440;

void print_sid(const std::array<uint8_t, 16>& sid) {
    std::cout << std::hex << std::setfill('0');
    for (size_t i = 0; i < sid.size(); ++i) {
        std::cout << std::setw(2) << static_cast<int>(sid[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) std::cout << "-";
    }
    std::cout << std::dec << std::setfill(' ');
}

void print_bytes(const std::vector<uint8_t>& buf) {
    std::cout << "Serialized (" << buf.size() << " bytes): ";
    for (auto b : buf)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << ' ';
    std::cout << std::dec << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== SLOW Peripheral v1.3 (handshake corrigido) ===" << std::endl;
    if (argc < 2 || argc > 3) {
        std::cerr << "Uso: " << argv[0] << " <host> [porta]" << std::endl;
        return 1;
    }

    const char* host = argv[1];
    const char* port = (argc == 3 ? argv[2] : "7033");

    struct addrinfo hints{}, *res, *rp;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo");
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

    // --- Montagem do Pacote CONNECT (usando a flag correta do enum corrigido) ---
    SLOWPacket connect_pkt{};
    connect_pkt.sid = {}; // UUID nulo
    connect_pkt.flags = FLAG_CONNECT; // Usa o valors (16) do enum
    connect_pkt.sttl = 0;
    connect_pkt.seqnum = 0;
    connect_pkt.acknum = 0;
    connect_pkt.window = DEFAULT_WINDOW_SIZE;
    connect_pkt.fid = 0;
    connect_pkt.fo = 0;

    auto buf = connect_pkt.serialize();

    std::cout << "Resolvendo para " << host << ":" << port << std::endl;

    const int max_retries = 3;
    bool accepted = false;
    std::array<uint8_t, 16> session_sid;
    uint32_t session_sttl = 0;
    uint32_t next_seqnum = 0;
    uint32_t last_acknum = 0;

    // --- PASSO 1 e 2 DO HANDSHAKE: Enviar CONNECT e esperar ACCEPT ---
    for (int attempt = 0; attempt < max_retries && !accepted; ++attempt) {
        print_bytes(buf);
        std::cout << "Enviando CONNECT (tentativa " << (attempt + 1) << ")..." << std::endl;
        sendto(sock, buf.data(), buf.size(), 0, rp->ai_addr, rp->ai_addrlen);

        std::vector<uint8_t> rbuf(1472);
        ssize_t len = recvfrom(sock, rbuf.data(), rbuf.size(), 0, nullptr, nullptr);

        if (len > 0) {
            rbuf.resize(len);
            std::cout << "Resposta recebida:" << std::endl;
            try {
                auto resp = SLOWPacket::deserialize(rbuf);
                // Verifica se é uma resposta de ACCEPT válida
                if (resp.flags & FLAG_ACCEPT_REJECT) {
                    std::cout << "Conexão ACEITA pelo Central (passo 2/3 do handshake)." << std::endl;
                    session_sid = resp.sid;
                    session_sttl = resp.sttl;
                    // O primeiro seqnum da sessão é o que o servidor nos deu.
                    // O próximo que enviaremos será ele + 1.
                    // O acknum que devemos enviar é o seqnum que recebemos.
                    next_seqnum = resp.seqnum + 1;
                    last_acknum = resp.seqnum;
                    accepted = true;
                    std::cout << "  > Session ID: ";
                    print_sid(session_sid);
                    std::cout << "\n  > Session STTL: " << session_sttl << " ms" << std::endl;
                } else {
                    std::cerr << "Conexão REJEITADA pelo Central (flags=" << (int)resp.flags << ")" << std::endl;
                    if (!resp.data.empty()) {
                        std::string msg(resp.data.begin(), resp.data.end());
                        std::cerr << "  > Mensagem do servidor: " << msg << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Erro ao decodificar resposta: " << e.what() << std::endl;
            }
        } else {
            perror("recvfrom (Timeout)");
        }
    }

    if (!accepted) {
        std::cerr << "Falha ao estabelecer conexão após " << max_retries << " tentativas." << std::endl;
        close(sock);
        return 1;
    }

    // --- PASSO 3 DO HANDSHAKE: Enviar confirmação ACK para finalizar ---
    std::cout << "\nEnviando confirmação ACK para finalizar o handshake (passo 3/3)..." << std::endl;
    SLOWPacket confirm_pkt{};
    confirm_pkt.sid = session_sid;
    confirm_pkt.sttl = session_sttl;
    confirm_pkt.flags = FLAG_ACK;
    confirm_pkt.seqnum = next_seqnum; // seqnum do nosso pacote de confirmação
    confirm_pkt.acknum = last_acknum; // acknum confirmando o pacote ACCEPT do servidor
    confirm_pkt.window = DEFAULT_WINDOW_SIZE;

    auto confirm_buf = confirm_pkt.serialize();
    print_bytes(confirm_buf);
    sendto(sock, confirm_buf.data(), confirm_buf.size(), 0, rp->ai_addr, rp->ai_addrlen);
    
    // Neste ponto, a especificação considera a conexão estabelecida.
    // Poderíamos esperar um ACK para nosso ACK, mas isso levaria a um loop infinito.
    std::cout << "\nConexão estabelecida com sucesso! Pronto para transmitir dados." << std::endl;

    freeaddrinfo(res);
    close(sock);
    return 0;
}