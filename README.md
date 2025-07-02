# Implementação do Protocolo SLOW (Peripheral)
Este trabalho implementa o protocolo SLOW, um protocolo ad hoc na camada de transporte para controle de fluxo de dados, mais especificamente o periférico.

O SLOW possui algumas semelhanças com o QUIC: quando os responsáveis ​​pela implementação do QUIC o planejaram, enfrentaram o desafio de fazer com que os principais sistemas operacionais, como Linux, Windows e Mac, implementassem um novo protocolo de transporte no nível do kernel, já que sem suporte ao nível do kernel seria impossível utilizá-lo. Portanto, eles decidiram usar o UDP como infraestrutura para o QUIC, por ser um protocolo leve e já implementado em quase todos os kernels.
O SLOW também utiliza o UDP como infraestrutura para troca de mensagens e adiciona funcionalidades a ele.
Este trabalho implementa o periférico em C++.


## Autor

  * **Vitor Marçal Brasil** (N°USP: 12822653)

## Estrutura do Código

O código está organizado de modo a separar as responsabilidades:

  * `slow_packet.hpp`: Define a estrutura de um pacote SLOW (`struct SLOWPacket`) e a `enum` de flags. Contém toda a lógica de **serialização** (converter a struct para bytes para envio) e **desserialização** (converter bytes recebidos de volta para a struct).
  * `main.cpp`: Contém a lógica principal da aplicação. É responsável por configurar o socket UDP, gerenciar o estado da sessão e orquestrar o fluxo do protocolo:
    1.  Estabelecer a conexão (handshake de 3 vias).
    2.  Transmitir um bloco de dados de teste.
    3.  Encerrar a conexão.

## Funcionalidades Implementadas

O peripheral implementa todas as funcionalidades exigidas pela especificação:

  - **Conexão com Handshake de 3 Vias:** Estabelece uma sessão com o servidor de forma confiável.
  - **Transmissão de Dados Confiável:** Garante a entrega de dados através de confirmações (ACKs) e retransmissão em caso de timeouts.
  - **Fragmentação de Pacotes:** Divide mensagens maiores que 1440 bytes em múltiplos pacotes, utilizando os campos `fid`, `fo` e a flag `More Bits`.
  - **Controle de Fluxo com Janela Deslizante:** Respeita a janela de recepção informada pelo servidor para evitar congestionamento.
  - **Desconexão Limpa:** Envia um pacote de `Disconnect` para encerrar a sessão de forma apropriada.

## Como Compilar

O projeto utiliza `CMake` para gerar os arquivos de compilação. Assumindo que você tenha `g++`, `cmake` e `make` instalados, siga os passos a partir do diretório raiz do projeto:

```shell
# 1. Crie um diretório de build (se não existir) e entre nele
mkdir -p build && cd build

# 2. Gere os Makefiles com o CMake (executado a partir do diretório 'build')
cmake ..

# 3. Compile o projeto com o make
make
```

O executável `slow_peripheral` será criado dentro do diretório `build`.

## Como Utilizar

Após a compilação, o programa pode ser executado a partir do diretório `build` para se conectar ao servidor de testes oficial.

**Comando:**

```shell
./slow_peripheral slow.gmelodie.com 7033
```

### Exemplo de Saída de Sucesso

Uma execução bem-sucedida do programa terá uma saída semelhante a esta:

```
└─$ ./slow_peripheral slow.gmelodie.com 7033
=== SLOW Peripheral v2.0 ===
Resolvendo para slow.gmelodie.com:7033
Serialized (32 bytes): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 00 00 00 00 00 00 00 00 00 00 00 a0 05 00 00 
Enviando CONNECT (tentativa 1)...
Resposta recebida:
Conexão ACEITA pelo Central (passo 2/3 do handshake).
  > Session ID: e085fb30-baa8-86e4-ac25-9a6bc9aa36fb
  > Session STTL: 599 ms
  > Janela inicial do servidor: 1024 bytes

Enviando confirmação ACK para finalizar o handshake (passo 3/3)...
Serialized (32 bytes): e0 85 fb 30 ba a8 86 e4 ac 25 9a 6b c9 aa 36 fb e4 4a 00 00 48 0d 00 00 48 0d 00 00 a0 05 00 00 

Conexão estabelecida com sucesso! Pronto para transmitir dados.

--- INICIANDO TRANSMISSÃO DE DADOS ---
Enviando 15000 bytes...
  > ACK recebido para Seqnum <== 3400. Janela do servidor: 1024 bytes.
  ... (vários ACKs) ...
  > ACK recebido para Seqnum <== 3414. Janela do servidor: 1024 bytes.
## TRANSMISSÃO DE DADOS CONCLUÍDA ##

## ENCERRANDO SESSÃO ##
Disconnect enviado => Sessão encerrada!

### TESTANDO 0-WAY CONNECT ==> REVIVE ###

```