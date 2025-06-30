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
#include <random>
#include <fstream>
#include "slow_packet.hpp"

constexpr uint16_t DEFAULT_WINDOW_SIZE = 1440;

// (A função generate_uuid_v8() não é mais usada para o connect, mas pode ser útil depois)
std::array<uint8_t, 16> generate_uuid_v8() {
    std::array<uint8_t, 16> uuid;
    std::random_device rd;
    std::mt19937 gen(rd());

    for (auto& byte : uuid) {
        byte = static_cast<uint8_t>(gen() & 0xFF);
    }

    // UUIDv8 → byte 6 (índice 6): versão 8 (1000xxxx)
    uuid[6] &= 0x0F;
    uuid[6] |= 0x80;

    // Variante → byte 8 (índice 8): 10xxxxxx
    uuid[8] &= 0x3F;
    uuid[8] |= 0x80;

    return uuid;
}

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
    std::cout << "=== SLOW Peripheral v1.2 ===" << std::endl;
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

    // --- CORREÇÃO 1: Usar Nil UUID para o pacote CONNECT ---
    // A especificação exige um "Nil UUID" (todos os bytes 0) para o primeiro pacote de conexão.
    SLOWPacket connect_pkt{};
    std::array<uint8_t, 16> nil_sid{}; // Inicializa todos os 16 bytes com 0.
    connect_pkt.sid = {nil_sid};
    connect_pkt.flags = 16;
    connect_pkt.sttl = 0;
    connect_pkt.seqnum = 0;
    connect_pkt.acknum = 0;
    connect_pkt.window = DEFAULT_WINDOW_SIZE;
    connect_pkt.fid = 0;
    connect_pkt.fo = 0;
    connect_pkt.data = {};

    auto buf = connect_pkt.serialize();

    // ===================================================================
    // INICIO: Bloco de código para salvar o pacote em um arquivo
    // ===================================================================
    { // Usando chaves para limitar o escopo da variável debug_file
        std::cout << "Salvando pacote serializado em 'connect_packet.bin' para depuração..." << std::endl;
        std::ofstream debug_file("connect_packet.bin", std::ios::binary);
        if (debug_file.is_open()) {
            debug_file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
            debug_file.close();
            std::cout << "Arquivo 'connect_packet.bin' salvo com sucesso." << std::endl;
        } else {
            std::cerr << "Erro ao abrir o arquivo de depuração." << std::endl;
        }
    }
    // ===================================================================
    // FIM: Bloco de código para salvar o pacote
    // ===================================================================

    const int max_retries = 5;
    int attempts = 0;
    bool connected = false;
    std::vector<uint8_t> rbuf(1472);
    struct sockaddr_storage src;
    socklen_t srclen = sizeof(src);

    std::array<uint8_t, 16> session_sid;
    uint32_t session_sttl = 0;
    uint32_t session_seqnum = 0;
    uint32_t last_acknum = 0;

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
                if (resp.flags & FLAG_ACCEPT_REJECT) {
                    std::cout << "Conexão ACEITA pelo Central." << std::endl;
                    session_sid = resp.sid;
                    session_sttl = resp.sttl;
                    session_seqnum = resp.seqnum; // Este é o primeiro seqnum da sessão 
                    last_acknum = resp.seqnum;    // O último pacote recebido é o Setup, então seu seqnum é o que devemos confirmar

                    std::cout << "  > Session ID: ";
                    print_sid(session_sid);
                    std::cout << std::endl << "  > Session STTL: " << session_sttl << " ms" << std::endl;
                    std::cout << "  > Session initial seqnum: " << session_seqnum << std::endl;
                    connected = true;

                    // A terceira etapa do handshake é feita após este loop, enviando dados com a flag ACK.
                    // Não enviamos um ACK puro aqui.
                } else {
                    std::cerr << "Conexão REJEITADA pelo Central (flags=" << (int)resp.flags << ")" << std::endl;
                    
                    if (!resp.data.empty()) {
                        std::string error_msg(resp.data.begin(), resp.data.end());
                        std::cerr << "  > Mensagem do servidor: " << error_msg << std::endl;
                    }
                    
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
        std::cout << "\nConexão estabelecida com sucesso." << std::endl;
        
        // --- CORREÇÃO 2: Enviar pacote de DADOS com flag ACK para concluir o handshake ---
        // A especificação indica que o terceiro passo é um envio de dados, não um ACK puro.
        // Este pacote de dados também serve para confirmar o recebimento do pacote "Setup" do central.
        std::cout << "Enviando primeiro pacote de dados para concluir o 3-way handshake..." << std::endl;
        
        SLOWPacket data_pkt{};
        data_pkt.sid = session_sid;
        data_pkt.flags = FLAG_ACK; // Flag ACK para confirmar o pacote Setup 
        data_pkt.sttl = session_sttl;
        data_pkt.seqnum = session_seqnum + 1; // Usamos o "próximo número de sequência" 
        data_pkt.acknum = last_acknum; // Confirmamos o seqnum que recebemos do central 
        data_pkt.window = DEFAULT_WINDOW_SIZE;
        data_pkt.fid = 0;
        data_pkt.fo = 0;
        
        std::string message = "Olá Central, handshake concluído e dados enviados!";
        data_pkt.data.assign(message.begin(), message.end());

        auto data_buf = data_pkt.serialize();
        print_bytes(data_buf);
        if (sendto(sock, data_buf.data(), data_buf.size(), 0, reinterpret_cast<sockaddr*>(&src), srclen) < 0) {
            perror("sendto data_pkt");
        } else {
            std::cout << "Pacote de dados enviado. A sessão está ativa." << std::endl;
            // A partir daqui, o código continuaria a enviar e receber dados.
        }
    }

    freeaddrinfo(res);
    close(sock);
    return 0;
}