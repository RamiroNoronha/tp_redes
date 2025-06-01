#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
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
    // Args validation
    if (argc < 4)
    {
        fprintf(stderr, "Uso: %s <peer_ipv4> <p2p_port> <client_listen_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *peer_target_ip = argv[1];
    int common_p2p_port = atoi(argv[2]); // Esta é a porta P2P comum
    int client_listen_port = atoi(argv[3]);

    // end args validation

    // P2P configuration
    int p2p_fd = -1;        // Socket para comunicação P2P estabelecida
    int p2p_listen_fd = -1; // Socket para escutar conexões P2P se a ativa falhar

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

    // Variables for Select
    fd_set master_set, read_fds; // Master set of file descriptors and temporary set for select
    int fd_max;                  // Max file descriptor number for select

    // Cleaning sets before using to avoid undefined behavior
    FD_ZERO(&master_set);
    FD_ZERO(&read_fds);

    // Adding the listening socket to the master socket set
    // This allows us to monitor incoming connections on this socket
    // This is the socket that will accept new client connections
    // It will be the first socket to be monitored by select
    FD_SET(listen_fd, &master_set);

    fd_max = listen_fd; // Initialize fd_max with the listening socket

    while (1)
    {
        read_fds = master_set; // Copy master set to read_fds for select

        printf("Aguardando conexões...\n");

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == SOCKET_ERROR)
        {
            error_exit("Erro ao chamar select");
        }

        printf("Atividade detectada!\n");

        for (int i = 0; i <= fd_max; i++)
        {
            int has_activity = FD_ISSET(i, &read_fds);
            if (!has_activity)
                continue;

            if (i == listen_fd)
            {
                struct sockaddr_in new_client_addr;
                socklen_t new_client_addr_len = sizeof(new_client_addr);

                int new_client_fd = accept(listen_fd, (struct sockaddr *)&new_client_addr, &new_client_addr_len);
                if (new_client_fd == SOCKET_ERROR)
                {
                    perror("Erro ao aceitar nova conexão de cliente");
                    continue;
                }

                FD_SET(new_client_fd, &master_set); // Add the new client socket to the master set

                if (new_client_fd > fd_max)
                    fd_max = new_client_fd; // Update fd_max if necessary

                char client_ip[INET_ADDRSTRLEN];

                if (inet_ntop(AF_INET, &new_client_addr.sin_addr, &client_ip, sizeof(client_ip)) == NULL)
                {
                    perror("Erro ao converter endereço IP do cliente");
                    close(new_client_fd);
                    continue;
                }
                printf("Novo cliente conectado: %s:%d (fd: %d)\n",
                       client_ip, ntohs(new_client_addr.sin_port), new_client_fd);
                continue;
            }

            char buffer[MAX_MSG_SIZE];
            memset(buffer, 0, MAX_MSG_SIZE);
            ssize_t bytes_received;

            if ((bytes_received = recv(i, buffer, MAX_MSG_SIZE - 1, 0)) <= 0)
            {
                // Erro ou conexão fechada pelo cliente 'i'
                if (bytes_received == 0)
                    printf("Socket %d (cliente) desconectou.\n", i);
                else
                    perror("Erro no recv() do cliente");

                close(i);
                FD_CLR(i, &master_set);
                continue;
            }

            buffer[bytes_received] = '\0'; // Null-terminate the received data
            printf("Recebido do cliente (fd %d): %s\n", i, buffer);

            char reply_msg[MAX_MSG_SIZE + 60];

            snprintf(reply_msg, sizeof(reply_msg), "Mensagem recebida: %s", buffer);
            ssize_t bytes_sent = send(i, reply_msg, strlen(reply_msg), 0);
            if (bytes_sent == SOCKET_ERROR)
            {
                perror("Erro ao enviar resposta ao cliente");
                close(i);
                FD_CLR(i, &master_set);
                continue;
            }
            printf("Resposta enviada ao cliente (fd %d): %s\n", i, reply_msg);
        }
    }
}