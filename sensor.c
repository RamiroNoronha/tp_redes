#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // Para inet_pton
#include "common.h"    // Usaremos MAX_MSG_SIZE daqui

// Função para auxiliar na exibição de erros e sair
void error_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    // 1. Validar e processar argumentos de linha de comando
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s <server_ipv4> <server_client_listen_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    printf("Cliente (sensor) iniciando...\n");
    printf("Tentando conectar ao servidor %s na porta %d\n", server_ip, server_port);

    // 2. Criar o socket do cliente
    int client_fd; // File descriptor para o socket do cliente
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        error_exit("Erro ao criar socket do cliente");
    }
    printf("Socket do cliente criado (fd: %d).\n", client_fd);

    // 3. Configurar o endereço do servidor e conectar
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    // Converter o endereço IP de string para formato de rede
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        error_exit("Erro ao converter endereço IP do servidor");
    }

    printf("Conectando ao servidor...\n");
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        error_exit("Erro ao conectar ao servidor");
    }
    printf("Conectado ao servidor!\n");

    // 4. Enviar uma mensagem para o servidor
    const char *message_to_server = "Ola Servidor, aqui eh o Sensor!";
    printf("Enviando mensagem para o servidor: '%s'\n", message_to_server);
    if (send(client_fd, message_to_server, strlen(message_to_server), 0) == -1)
    {
        error_exit("Erro ao enviar mensagem para o servidor");
    }
    printf("Mensagem enviada.\n");

    // 5. Esperar e receber uma mensagem de resposta do servidor
    char buffer[MAX_MSG_SIZE];
    memset(buffer, 0, MAX_MSG_SIZE);
    ssize_t bytes_received;

    printf("Aguardando resposta do servidor...\n");
    if ((bytes_received = recv(client_fd, buffer, MAX_MSG_SIZE - 1, 0)) == -1)
    {
        error_exit("Erro ao receber dados do servidor");
    }
    else if (bytes_received == 0)
    {
        printf("Servidor desconectou (recv retornou 0).\n");
    }
    else
    {
        buffer[bytes_received] = '\0'; // Garantir terminação nula da string
        printf("Mensagem recebida do servidor: '%s' (%zd bytes)\n", buffer, bytes_received);
    }

    // 7. Fechar o socket
    printf("Fechando socket do cliente (fd: %d).\n", client_fd);
    close(client_fd);

    printf("Cliente (sensor) encerrado.\n");
    return 0;
}