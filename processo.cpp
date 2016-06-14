#define _WIN32_WINNT 0x601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>

using namespace std;

/*
###############################################################################

NÃO ESQUECA DE:

- LINKAR COM A BIBLIOTECA Ws2_32. Incluir essa opção no compilador
No CodeBlocs: botão direito no nome do projeto, Build options, Linker settings,
adicionar biblioteca Ws2_32

- Alterar o #define DEFAULT_IP_SERVER para o IP da maquina do servidor

###############################################################################
*/

/**     DEFINIÇÃO DE CONSTANTES, TIPOS DE VARIÁVEIS E ESTRUTURAS DE DADOS,
        VARIÁVEIS GLOBAIS E FUNÇÕES AUXILIARES     **/

// CONSTANTES
#define DEFAULT_PORT "27015"
#define DEFAULT_IP_SERVER "192.168.1.6"
#define SIZE_PACK_DADOS 8
#define SIZE_PACK_CMD 6


// TIPOS
enum STATUS
{
    STATUS_ATIVO = 1,
    STATUS_SUSPENSO = 0,
    STATUS_ENCERRAR = -1
};

enum COMANDO
{
    CMD_ENCERRAR = 0xFFFF,
    CMD_SUSPENDER = 0xEEEE,
    CMD_ATIVAR = 0x1111
};

// ESTRUTURAS DE DADOS
struct Dado{
    uint16_t temp;
    uint16_t prod;
};

struct pacoteDado{
    uint8_t cabecalho;
    uint16_t ID;
    Dado d;
    uint8_t rodape;
    void fromBytes(const char *buff);
    void toBytes(char *buff) const;
};

struct pacoteComando{
    uint8_t cabecalho;
    uint16_t ID;
    uint16_t cmd;
    uint8_t rodape;
    void fromBytes(const char *buff);
    void toBytes(char *buff) const;
};

// VARIÁVEIS GLOBAIS
SOCKET ConnectSocket;
STATUS Status;
uint16_t MyID;
Dado Ultimo_dado;

// FUNÇÕES AUXILIARES
inline float uint16ToFloat(uint16_t n) {return 100.0*n/65535.0;}

void uint16ToBytes(uint16_t n, char *buff){
    buff[0] = uint8_t(n/256);
    buff[1] = uint8_t(n%256);
}

uint16_t bytesToUint16(const char *buff){
    uint16_t n = 256*uint8_t(buff[0])+uint8_t(buff[1]);
    return n;
}

void pacoteDado::fromBytes(const char *buff){
    cabecalho = buff[0];    // 0
    ID = bytesToUint16(buff+1);  // 1 e 2
    d.temp = bytesToUint16(buff+3);  // 3 e 4
    d.prod = bytesToUint16(buff+5);  // 5 e 6
    rodape = buff[7];
}

void pacoteDado::toBytes(char *buff) const{
    buff[0] = cabecalho;    // 0
    uint16ToBytes(ID, buff+1);  // 1 e 2
    uint16ToBytes(d.temp, buff+3);  // 3 e 4
    uint16ToBytes(d.prod, buff+5);  // 5 e 6
    buff[7] = rodape;
}

void pacoteComando::fromBytes(const char *buff){
    cabecalho = buff[0];    // 0
    ID = bytesToUint16(buff+1);  // 1 e 2
    cmd = bytesToUint16(buff+3);  // 3 e 4
    rodape = buff[5];
}

void pacoteComando::toBytes(char *buff) const
{
    buff[0] = cabecalho;    // 0
    uint16ToBytes(ID, buff+1);  // 1 e 2
    uint16ToBytes(cmd, buff+3);  // 3 e 4
    buff[5] = rodape;
}

ostream &operator<<(ostream &O, const Dado &x)
{
    O << "T=" << fixed << setprecision(1) << uint16ToFloat(x.temp) << "C;   ";
    O << "P=" << uint16ToFloat(x.prod) << '%';
    return O;
}


/**------------- THREAD PARA SIMULAÇÃO DE VALORES ------------**/
DWORD WINAPI threadSimulacao(LPVOID lpParameter){
    pacoteDado pac;
    char buff[SIZE_PACK_DADOS];
    int iResult;
    int deltaT;

    srand(time(NULL));

    // Preenche a parte constante (que não muda) do pacote a ser enviado
    pac.cabecalho = 0x55;
    pac.rodape = 0xAA;
    pac.ID = MyID;

    while (Status!=STATUS_ENCERRAR){
        // Calcula um tempo de espera entre 30s (30.000 ms) e 3min (180.000 ms)
        deltaT = (int)round(30000.0 + 150000.0*float(rand())/RAND_MAX);

        // Simula os dados do processo
        Ultimo_dado.prod = (uint16_t)round(65535.0*float(rand())/RAND_MAX);
        Ultimo_dado.temp = (uint16_t)round(65535.0*float(rand())/RAND_MAX);

        // Se estiver ATIVO, envia os dados
        if (Status == STATUS_ATIVO){

            // Completa o pacote a ser enviado, acrescentando a parte dinamica (variavel)
            pac.d = Ultimo_dado;

            iResult = send(ConnectSocket, buff, SIZE_PACK_DADOS, 0);
            if ( iResult == SOCKET_ERROR ){
                cerr << "Falha de envio - erro: " << WSAGetLastError() << endl;
                cout << "Cliente desconectado\n";
                Status = STATUS_ENCERRAR;
            } else if (iResult != SIZE_PACK_DADOS){
                cerr << "Transmissao invalida. Bytes transmitidos: " << iResult << endl;
            }
        }

        if (Status == STATUS_ENCERRAR){
            deltaT = (int)round(3000.0 + 15000.0*float(rand())/RAND_MAX);
            Sleep(deltaT);
        }
    }

    if (ConnectSocket != INVALID_SOCKET) closesocket(ConnectSocket);
    ConnectSocket = INVALID_SOCKET;

    return 0;
}

/**------------- THREAD PARA RECEPÇÃO DE VALORES DO SUPERVISÓRIO ------------**/
DWORD WINAPI threadRecepcao(LPVOID lpParameter){
    pacoteComando comandoRecebido;
    char buff[SIZE_PACK_CMD];
    int iResult;

    while (Status != STATUS_ENCERRAR){

        cout << "Aguardando comando do supervisorio...\n";

        iResult = recv(ConnectSocket, buff, SIZE_PACK_CMD, 0);
        if ( iResult == SOCKET_ERROR ){
            cerr << "Falha na recepcao - erro: " << WSAGetLastError() << endl;
            Status = STATUS_ENCERRAR;
        }

        if (iResult == 0){
            cout << "Servidor desconectado\n";
            Status = STATUS_ENCERRAR;
        } else{
            comandoRecebido.fromBytes(buff);

            if (comandoRecebido.cabecalho!=0x55 || comandoRecebido.rodape!=0xAA
                || comandoRecebido.ID!=MyID){
                cerr << "Comando invalido. Descartando...\n";
            } else {
                cout << "Comando recebido: " << comandoRecebido.cmd << endl;
                switch (comandoRecebido.cmd){
                case CMD_ENCERRAR:
                    Status = STATUS_ENCERRAR;
                    break;
                case CMD_ATIVAR:
                    Status = STATUS_ATIVO;
                    break;
                case CMD_SUSPENDER:
                    Status = STATUS_SUSPENSO;
                    break;
                default:
                    cerr << "Comando desconhecido (descartando)... \n";
                    break;
                }
            }
        }
    }

    if (ConnectSocket != INVALID_SOCKET) closesocket(ConnectSocket);
    ConnectSocket = INVALID_SOCKET;

    return 0;
}


int main(){

    WSADATA wsaData;
    struct addrinfo hints, *result = NULL;  // para getaddrinfo, bind
    HANDLE  hThreadArray[2];    // Para armazenar os descritores das threads
    char buff[16];   // IP do servidor
    int iResult;
    int opcao;

    cout << "++++++++     PROGRAMA PROCESSO     ++++++++++" << endl;

    cout << "Iniciando o cliente...\n";
    Status = STATUS_ATIVO;
    Ultimo_dado.prod = 0;
    Ultimo_dado.temp = 0;

    cout << "Digite o IP do servidor(###.###.###.###, max 15 caracteres): ";
    cin.getline(buff,16);
    cout << "Digite a ID do processo: ";
    cin >> MyID;

    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        cerr << "WSAStartup falhou: " << iResult << endl;
        return 1;
    }

    cout << "Iniciando o socket...\n";
    memset(&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo(buff, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        cerr << "getaddrinfo falhou: " << iResult << endl;
        WSACleanup();
        return 1;
    }

    cout << "Criando o socket...\n";
    ConnectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ConnectSocket == INVALID_SOCKET) {
        cerr << "Erro de socket: " << WSAGetLastError() << endl;
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    cout << "Conectando o socket...\n";
    cout << "Conectando-se ao servidor no IP " << buff << " ...\n";

    iResult = connect( ConnectSocket, result->ai_addr, (int)result->ai_addrlen);

    freeaddrinfo(result);

    if (iResult == SOCKET_ERROR) {
        cerr << "Conexao falhou - erro: " << WSAGetLastError() << endl;
        Status = STATUS_ENCERRAR;
    }

    // Enviar a ID
    if (Status == STATUS_ATIVO){
        cout << "Enviando a ID... " << MyID << endl;
        uint16ToBytes(MyID, buff);
        iResult = send(ConnectSocket, buff, 2, 0);
        if ( iResult == SOCKET_ERROR || iResult != 2)
        {
            cerr << "Transmissao invalida da ID. Cliente desconectado\n";
            Status = STATUS_ENCERRAR;
        }
    }

    //Resposta do supervisório quanto à validação
    if (Status == STATUS_ATIVO){
        struct fd_set sock_read;    // Para select
        struct timeval timeout;     // Para select

        cout << "Esperando a confirmacao da conexao pelo servidor...\n";
        FD_ZERO(&sock_read);
        FD_SET(ConnectSocket, &sock_read);
        timeout.tv_sec = 30;    // 30 segundos
        timeout.tv_usec = 0;    // + 0 microssegundos
        iResult = select(0, &sock_read, NULL, NULL, &timeout);
        FD_ZERO(&sock_read);
        if ( iResult==SOCKET_ERROR || iResult==0 )  // Se iResult==0 saiu por timeout
        {
            // OK nao recebido. Descarta tentativa de conexao
            cout << "Servidor nao respondeu...\n";
            Status = STATUS_ENCERRAR;
        }
    }

    // Le a resposta enviada
    if (Status == STATUS_ATIVO)    {
        iResult = recv(ConnectSocket, buff, 2, 0);
        if ( iResult == SOCKET_ERROR || iResult != 2)
        {
            cout << "Nao foi possivel ler resposta do servidor\n";
            Status = STATUS_ENCERRAR;
        }
    }

    // Testa a natureza da resposta do servidor
    if (Status == STATUS_ATIVO){
        if (buff[0]!='O' || buff[1]!='K')
        {
            cout << "Conexao recusada pelo servidor!!  Houve algum erro!\n";
            Status = STATUS_ENCERRAR;
        }
    }

    // Encerra o programa se houve algum pb no estabelecimento da conexao
    if (Status != STATUS_ATIVO){
        if (ConnectSocket != INVALID_SOCKET) closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    //Criação das threads - coloca nos Handlers;
    hThreadArray[0] = CreateThread(NULL, 0, threadRecepcao, NULL , 0, NULL);
    hThreadArray[1] = CreateThread(NULL, 0, threadSimulacao, NULL , 0, NULL);


    /**     Selecao de opcoes    **/
    cout << "Aguardando comandos...\n\n";
    while (Status!=STATUS_ENCERRAR){
        do{
            cout << "OPCOES \n1-Imprimir; \n2-Ativar; \n3-Suspender; \n0-Terminar]: ";
            cin >> opcao;
        } while (opcao<0 || opcao>3);
        switch (opcao){
        case 0:
            Status==STATUS_ENCERRAR;
            break;
        case 1:
            cout << "STATUS " << (Status == STATUS_ATIVO ? "ATIVO" : (Status == STATUS_SUSPENSO ? "SUSPENSO" : "ENCERRANDO"));
            cout << "\tULTIMO DADO: " << Ultimo_dado << endl;
            break;
        case 2:
            Status==STATUS_ATIVO;
            break;
        case 3:
            Status==STATUS_SUSPENSO;
            break;
        default:
            cerr << "Opcao invalida\n";
            break;
        }
    }

    // Espera pelo termino das outras threads ateh 5 segundos
    WaitForMultipleObjects(2, hThreadArray, TRUE, 5000);

    // Fecha o socket
    if (ConnectSocket != INVALID_SOCKET) closesocket(ConnectSocket);
    ConnectSocket = INVALID_SOCKET;

    WSACleanup();

    return 0;
}
