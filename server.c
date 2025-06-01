#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h" // Usaremos MAX_MSG_SIZE daqui

#define SOCKET_ERROR -1 // Definindo um macro para erro de socket

// Função para auxiliar na exibição de erros e sair
void error_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    // 1. Validar e processar argumentos de linha de comando
    if (argc < 4)
    {
        fprintf(stderr, "Uso: %s <peer_ipv4> <p2p_port> <client_listen_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // char *peer_ip = argv[1]; // Usaremos mais tarde
    // int p2p_port = atoi(argv[2]); // Usaremos mais tarde
    int client_listen_port = atoi(argv[3]);

    printf("Servidor iniciando...\n");
    // printf("Peer IP: %s, P2P Port: %d, Client Listen Port: %d\n", peer_ip, p2p_port, client_listen_port);

    // 2. Criar o socket de escuta para clientes
    int listen_fd; // File descriptor para o socket de escuta
    // AF_INET indica que usaremos IPv4
    // SOCKET_STREAM indica que usaremos TCP
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR)
    {
        error_exit("Erro ao criar socket de escuta");
    }
    printf("Socket de escuta para clientes criado (fd: %d).\n", listen_fd);

    // 3. Configurar o socket para reutilizar o endereço (opcional, mas útil)
    int optval = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == SOCKET_ERROR)
    {
        error_exit("Erro ao configurar SO_REUSEADDR");
    }

    // 4. Vincular (bind) o socket à porta de cliente
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Escutar em todas as interfaces
    server_addr.sin_port = htons(client_listen_port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        error_exit("Erro ao fazer bind na porta do cliente");
    }
    printf("Socket vinculado à porta %d.\n", client_listen_port);

    // 5. Colocar o socket em modo de escuta
    int max_clients = 5; // Número máximo de conexões pendentes
    if (listen(listen_fd, max_clients) == SOCKET_ERROR)
    { // O '5' é o backlog, o número de conexões pendentes
        error_exit("Erro ao colocar socket em modo de escuta");
    }
    printf("Servidor escutando na porta %d para conexões de clientes...\n", client_listen_port);

    // 6. Aceitar uma conexão de cliente
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd; // File descriptor para a conexão com o cliente

    printf("Aguardando conexão do cliente...\n");
    if ((client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == SOCKET_ERROR)
    {
        error_exit("Erro ao aceitar conexão do cliente");
    }
    // Exibir informações do cliente conectado
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    printf("Cliente conectado: IP %s, Porta %d (fd: %d).\n", client_ip_str, ntohs(client_addr.sin_port), client_fd);

    // 7. Receber uma mensagem do cliente
    char buffer[MAX_MSG_SIZE];
    memset(buffer, 0, MAX_MSG_SIZE);
    ssize_t bytes_received;

    printf("Aguardando mensagem do cliente...\n");
    if ((bytes_received = recv(client_fd, buffer, MAX_MSG_SIZE - 1, 0)) == SOCKET_ERROR)
    {
        error_exit("Erro ao receber dados do cliente");
    }
    else if (bytes_received == 0)
    {
        printf("Cliente desconectou (recv retornou 0).\n");
    }
    else
    {
        buffer[bytes_received] = '\0'; // Garantir terminação nula da string
        printf("Mensagem recebida do cliente: '%s' (%zd bytes)\n", buffer, bytes_received);

        // 8. Enviar uma mensagem de resposta ao cliente
        const char *reply_msg = "Ola Cliente, mensagem recebida!";
        printf("Enviando resposta para o cliente: '%s'\n", reply_msg);
        if (send(client_fd, reply_msg, strlen(reply_msg), 0) == -1)
        {
            error_exit("Erro ao enviar resposta para o cliente");
        }
        printf("Resposta enviada.\n");
    }

    // 9. Fechar os sockets
    printf("Fechando socket do cliente (fd: %d).\n", client_fd);
    close(client_fd);
    printf("Fechando socket de escuta (fd: %d).\n", listen_fd);
    close(listen_fd);

    printf("Servidor encerrado.\n");
    return 0;
}