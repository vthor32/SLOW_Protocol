/**
 * Projeto: SLOW Peripheral Protocol
 * Autor: Vitor Marçal Brasil
 * N°USP: 12822653
 * Curso: Sistemas de Informação - ICMC/USP São Carlos
 * Data: Julho de 2025
 */

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
#include <list>
#include <algorithm>
#include "slow_packet.hpp"

constexpr uint16_t DEFAULT_WINDOW_SIZE = 1440;
constexpr int MAX_DATA_SIZE = 1440;

// Imprime um Session ID em formato UUID padrão para melhor legibilidade.
void print_sid(const std::array<uint8_t, 16>& sid) {
    std::cout << std::hex << std::setfill('0');
    for (size_t i = 0; i < sid.size(); ++i) {
        std::cout << std::setw(2) << static_cast<int>(sid[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) std::cout << "-";
    }
    std::cout << std::dec << std::setfill(' ');
}

// Imprime o conteúdo de um buffer de bytes em hexadecimal para depuração.
void print_bytes(const std::vector<uint8_t>& buf) {
    std::cout << "Serialized (" << buf.size() << " bytes): ";
    for (auto b : buf)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << ' ';
    std::cout << std::dec << std::endl;
}

// Gera um vetor de bytes com conteúdo aleatório para testes de transmissão.
std::vector<uint8_t> generate_random_data(size_t size) {
    std::vector<uint8_t> data(size);
    srand(time(nullptr));
    for (size_t i = 0; i < size; ++i) {
        data[i] = rand() % 256;
    }
    return data;
}

// --- Estrutura para Pacotes Pendentes (Janela Deslizante) ---
struct PendingPacket {
    uint32_t seqnum;
    std::vector<uint8_t> buffer;
    time_t sent_time;
};


// --- Envio de Dados com Fragmentação e Janela Deslizante ---
bool send_data(int sock, struct addrinfo* dest_addr,
               std::array<uint8_t, 16>& session_sid, uint32_t& session_sttl,
               uint32_t& next_seqnum, uint32_t& last_acknum, uint16_t& peer_window,
               const std::vector<uint8_t>& message)
{
    std::cout << "\n--- INICIANDO TRANSMISSÃO DE DADOS ---" << std::endl;
    std::cout << "Enviando " << message.size() << " bytes..." << std::endl;

    size_t total_sent = 0;
    // ID único para agrupar todos os fragmentos desta mensagem.
    uint8_t fragment_id = rand() % 256;
    uint8_t fragment_offset = 0;
    std::list<PendingPacket> pending_packets;
    size_t bytes_in_flight = 0;

    // Loop principal: continua enquanto houver dados a enviar ou pacotes aguardando ACK.
    while (total_sent < message.size() || !pending_packets.empty()) {
        
        // Loop de envio: preenche a janela de recepção do servidor com novos pacotes.
        while (total_sent < message.size() && bytes_in_flight < peer_window) {
            size_t chunk_size = std::min((size_t)MAX_DATA_SIZE, message.size() - total_sent);

            // Garante que o envio do próximo fragmento não excederá a janela disponível.
            if (bytes_in_flight + chunk_size > peer_window) {
                if (peer_window > bytes_in_flight) {
                    chunk_size = peer_window - bytes_in_flight;
                } else {
                    break;
                }
            }
            if (chunk_size == 0) break;

            SLOWPacket data_pkt{};
            data_pkt.sid = session_sid;
            data_pkt.sttl = session_sttl;
            data_pkt.seqnum = next_seqnum;
            data_pkt.acknum = last_acknum;
            data_pkt.window = DEFAULT_WINDOW_SIZE;
            data_pkt.fid = fragment_id;
            data_pkt.fo = fragment_offset;
            
            // Define a flag ACK e, se não for o último fragmento, a flag More Bits.
            data_pkt.flags = FLAG_ACK;
            if (total_sent + chunk_size < message.size()) {
                data_pkt.flags |= FLAG_MORE_BITS;
            }

            data_pkt.data.assign(message.begin() + total_sent, message.begin() + total_sent + chunk_size);
            
            auto buf = data_pkt.serialize();
            sendto(sock, buf.data(), buf.size(), 0, dest_addr->ai_addr, dest_addr->ai_addrlen);

            // Armazena o pacote enviado para possível retransmissão.
            pending_packets.push_back({data_pkt.seqnum, buf, time(nullptr)});
            
            bytes_in_flight += chunk_size;
            total_sent += chunk_size;
            next_seqnum++;
            fragment_offset++;
        }

        std::vector<uint8_t> rbuf(1472);
        ssize_t len = recvfrom(sock, rbuf.data(), rbuf.size(), 0, nullptr, nullptr);

        // Se uma resposta (ACK) for recebida, processa a confirmação.
        if (len > 0) {
            rbuf.resize(len);
            auto resp = SLOWPacket::deserialize(rbuf);

            if (resp.flags & FLAG_ACK) {
                std::cout << "  > ACK recebido para Seqnum <== " << resp.acknum << ". Janela do servidor: " << resp.window << " bytes." << std::endl;
                peer_window = resp.window;
                last_acknum = resp.seqnum;
                
                // Libera da lista de pendentes os pacotes que foram confirmados.
                auto it = pending_packets.begin();
                while(it != pending_packets.end()) {
                    if (it->seqnum <= resp.acknum) {
                        bytes_in_flight -= (it->buffer.size() - 32);
                        it = pending_packets.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        } else { // Se não houver resposta (timeout), retransmite os pacotes pendentes.
            std::cout << "  > Timeout! Retransmitindo pacotes pendentes..." << std::endl;
            for (const auto& pending : pending_packets) {
                 if (time(nullptr) - pending.sent_time > 1) {
                    sendto(sock, pending.buffer.data(), pending.buffer.size(), 0, dest_addr->ai_addr, dest_addr->ai_addrlen);
                 }
            }
        }
    }
    std::cout << "## TRANSMISSÃO DE DADOS CONCLUÍDA ##" << std::endl;
    return true;
}

// --- Função para Desconectar ---
void disconnect(int sock, struct addrinfo* dest_addr,
                std::array<uint8_t, 16>& session_sid, uint32_t& session_sttl,
                uint32_t& next_seqnum, uint32_t& last_acknum)
{
    std::cout << "\n## ENCERRANDO SESSÃO ##" << std::endl;
    SLOWPacket disc_pkt{};
    disc_pkt.sid = session_sid;
    disc_pkt.sttl = session_sttl;
    // Conforme especificação, a combinação das flags Connect, Revive e Ack sinaliza um Disconnect.
    disc_pkt.flags = FLAG_CONNECT | FLAG_REVIVE | FLAG_ACK;
    disc_pkt.seqnum = next_seqnum;
    disc_pkt.acknum = last_acknum;
    
    auto buf = disc_pkt.serialize();
    sendto(sock, buf.data(), buf.size(), 0, dest_addr->ai_addr, dest_addr->ai_addrlen);
    std::cout << "Disconnect enviado => Sessão encerrada!" << std::endl;
}


int main(int argc, char* argv[]) {
    std::cout << "=== SLOW Peripheral v2.0 ===" << std::endl;
    if (argc < 2 || argc > 3) {
        std::cerr << "Uso: " << argv[0] << " <host> [porta]" << std::endl;
        return 1;
    }

    const char* host = argv[1];
    const char* port = (argc == 3 ? argv[2] : "7033");

    // Configuração inicial do socket e resolução de endereço.
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

    // --- Montagem do Pacote CONNECT (com flag correta do enum corrigido) ---
    SLOWPacket connect_pkt{};
    connect_pkt.sid = {}; // UUID nulo
    connect_pkt.flags = FLAG_CONNECT; // Usa o valor (16) do enum
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
    uint16_t peer_window = 0;

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
                // Verifica se é uma resposta de ACCEPT válida (bit de aceite ligado).
                if (resp.flags & FLAG_ACCEPT_REJECT) {
                    std::cout << "Conexão ACEITA pelo Central (passo 2/3 do handshake)." << std::endl;
                    session_sid = resp.sid;
                    session_sttl = resp.sttl;
                    // O primeiro seqnum da sessão é o que o servidor me dá.
                    // O próximo que enviaremos será ele.
                    // O acknum que eu envio é o seqnum que que eu recebo.
                    next_seqnum = resp.seqnum;
                    last_acknum = resp.seqnum;
                    peer_window = resp.window;
                    accepted = true;
                    std::cout << "  > Session ID: ";
                    print_sid(session_sid);
                    std::cout << "\n  > Session STTL: " << session_sttl << " ms" << std::endl;
                    std::cout << "  > Janela inicial do servidor: " << peer_window << " bytes" << std::endl;
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
    confirm_pkt.seqnum = next_seqnum; // Envia o mesmo seqnum recebido, conforme especificação de pacote ACK puro.
    confirm_pkt.acknum = last_acknum; // acknum confirmando o pacote ACCEPT do servidor
    confirm_pkt.window = DEFAULT_WINDOW_SIZE;

    auto confirm_buf = confirm_pkt.serialize();
    print_bytes(confirm_buf);
    sendto(sock, confirm_buf.data(), confirm_buf.size(), 0, rp->ai_addr, rp->ai_addrlen);
    
    // Neste ponto, a especificação considera a conexão estabelecida.
    std::cout << "\nConexão estabelecida com sucesso! Pronto para transmitir dados." << std::endl;

    // chama a NOVA LÓGICA DE ENVIO DE DADOS
    std::vector<uint8_t> message = generate_random_data(15000);
    if (!send_data(sock, rp, session_sid, session_sttl, next_seqnum, last_acknum, peer_window, message)) {
        std::cerr << "Falha durante a transmissão de dados." << std::endl;
    }

    // chama a NOVA LÓGICA DE DESCONEXÃO
    disconnect(sock, rp, session_sid, session_sttl, next_seqnum, last_acknum);

    // Revivendo a sessão para enviar mais dados
    std::cout << "\n### TESTANDO 0-WAY CONNECT ==> REVIVE ###" << std::endl;
    std::vector<uint8_t> outra_mensagem = generate_random_data(1000);
 

    freeaddrinfo(res);
    close(sock);
    return 0;
}