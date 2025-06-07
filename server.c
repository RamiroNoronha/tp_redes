#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "common.h"
#include <stddef.h>

// SOCKET_ERROR is -1 because is the value returned by socket() and accept() when they fail
#define SOCKET_ERROR -1

typedef enum
{
    PARSE_ERROR_INVALID_FORMAT = -1,
    PARSE_SUCCESS_CODE_ONLY = 1,   // Only the code was read
    PARSE_SUCCESS_ONE_PAYLOAD = 2, // Code and payload1 were read
    PARSE_SUCCESS_TWO_PAYLOADS = 3 // Code, payload1 and payload2 were read
} ParseResultType;

// Auxiliary function to handle errors and exit the program
// This function prints the error message and exits the program with EXIT_FAILURE
void error_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

/**
 * @brief Function responsable for creating a destination message buffer
 *
 * @param buffer_destination A pointer for the destination buffer where the message will be built.
 * @param buffer_size The size of the destination buffer. It is used to avoid buffer overflow.
 * @param code The code number of the message.
 * @param payload1 The first payload (string). It's is nullable
 * @param payload2 The second payload (string). It's is nullable
 */
void build_message(char *buffer_destination, size_t buffer_size, int code,
                   const char *payload1, const char *payload2)
{
    // 1. Clean the buffer to ensure no garbage data remains.
    memset(buffer_destination, 0, buffer_size);

    int bytes_escritos = 0;

    // 2. Builds the message based on the provided code and payloads
    // snprintf is a function responsable for formmating the buffer message respecting the buffer size.
    if (payload1 != NULL && payload2 != NULL)
    {
        bytes_escritos = snprintf(buffer_destination, buffer_size, "%d %s %s", code, payload1, payload2);
    }
    else if (payload1 != NULL)
    {
        bytes_escritos = snprintf(buffer_destination, buffer_size, "%d %s", code, payload1);
    }
    else if (payload2 != NULL)
    {
        bytes_escritos = snprintf(buffer_destination, buffer_size, "%d %s", code, payload2);
    }
    else
    {
        bytes_escritos = snprintf(buffer_destination, buffer_size, "%d", code);
    }

    // 3. Verifying if the message was truncated
    if (bytes_escritos >= buffer_size)
    {
        fprintf(stderr, "AVISO: A mensagem construída foi truncada (limite de %zu bytes).\n", buffer_size);
    }
}

/**
 * @brief Analisa uma string de mensagem recebida e extrai o código e os payloads.
 *
 * @param buffer_origin The string message received from the client or peer.
 * @param destination_code Pointer to the int that will keep the code.
 * @param destination_payload1 Buffer to get the first payload.
 * @param destination_payload2 Buffer to get the socond payload.
 * @return The number of filds read from the message:
 */
ParseResultType parse_message(const char *buffer_origin, int *destination_code,
                              char *destination_payload1, char *destination_payload2)
{

    // Cleaning the destination buffers to ensure they start empty
    if (destination_payload1)
        destination_payload1[0] = '\0';
    if (destination_payload2)
        destination_payload2[0] = '\0';
    if (destination_code)
        *destination_code = -1;

    int read_items = 0;

    // Tryies to fit the message in one of the expected formats

    read_items = sscanf(buffer_origin, "%d %s %s", destination_code, destination_payload1, destination_payload2);
    if (read_items == 3)
    {
        return PARSE_SUCCESS_TWO_PAYLOADS;
    }

    read_items = sscanf(buffer_origin, "%d %s", destination_code, destination_payload1);
    if (read_items == 2)
    {
        return PARSE_SUCCESS_ONE_PAYLOAD;
    }

    read_items = sscanf(buffer_origin, "%d", destination_code);
    if (read_items == 1)
    {
        return PARSE_SUCCESS_CODE_ONLY;
    }

    // If we reach here, it means that the message format is not recognized
    fprintf(stderr, "Erro ao analisar mensagem: formato não reconhecido em \"%s\"\n", buffer_origin);
    return PARSE_ERROR_INVALID_FORMAT;
}

/**
 * @brief Function that centralizes the creation and configuration logic of a listening socket.
 *
 * @param port The port number to bind the socket. For example, if I want to bind the socket to port 8080, I should pass 8080.
 * @param backlog The max number of queue clients.
 * @return THe socket file descriptor of the listening socket if successful, or
 * SOCKET_ERROR if it fails (with perror already called).
 */
int create_and_configure_listening_socket(int port, int backlog)
{
    int new_listen_fd;
    struct sockaddr_in addr;
    int optval = 1;

    // 1. Create the socket, note that we are using IPv4 (AF_INET) and TCP (SOCK_STREAM)
    // If the socket creation fails, it will return SOCKET_ERROR and perror will be called
    // If the socket creation is successful, it will return a valid file descriptor (fd) for the socket
    // That I can use to bind, listen, accept, etc.
    // Note that I do not did the bind yet, just created the socket
    if ((new_listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR)
    {
        perror("Erro ao criar socket de escuta na função");
        return SOCKET_ERROR;
    }

    // 2. Here we set the SO_REUSEADDR option to allow the socket to be reused
    // This is useful to avoid the "Address already in use" error when restarting the server
    // It allows the socket to be bound to the same address and port even if it is already in use
    // If setsockopt fails, it will return SOCKET_ERROR
    if (setsockopt(new_listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == SOCKET_ERROR)
    {
        perror("Erro ao configurar SO_REUSEADDR na função");
        close(new_listen_fd);
        return SOCKET_ERROR;
    }

    // 3. Prepare the sockaddr_in structure for binding
    // This structure will hold the address and port information for the socket
    // We set the family to AF_INET (IPv4), the address to INADDR_ANY (to accept connections from any IP),
    // and the port to the specified port number (converted to network byte order with htons)
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    // 4. Now is the bind step, where we bind the socket to the address and port
    // If bind fails, it will return SOCKET_ERROR and perror will be called
    // If bind is successful, the socket is ready to listen for incoming connections is the address and port specified
    if (bind(new_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        perror("Erro ao fazer bind na porta na função");
        close(new_listen_fd);
        return SOCKET_ERROR;
    }

    // 5. This step puts the socket in listening mode
    // Without this step, the socket cannot accept incoming connections and is just using the address and port
    // If listen fails, it will return SOCKET_ERROR and perror will be called
    // If listen is successful, the socket is ready to accept incoming connections
    if (listen(new_listen_fd, backlog) == SOCKET_ERROR)
    {
        perror("Erro ao colocar socket em modo de escuta na função");
        close(new_listen_fd);
        return SOCKET_ERROR;
    }

    printf("Socket de escuta configurado e operando na porta %d (fd: %d).\n", port, new_listen_fd);
    return new_listen_fd;
}

/**
 * @brief This functions is responsible for initializing or reconfiguring the P2P link.
 *
 * @param peer_target_ip The IP address of the peer to connect to.
 * @param common_p2p_port The port that the socket responsable for listening p2p connection will be.
 * @param p2p_fd_ptr Pointer to the file description variable responsable for the p2p connection.
 * @param p2p_listen_fd_ptr Pointer responsable for the file description connection that listen news p2p connections.
 * @param master_set_ptr Pointer to the master socket set. It means, all the sockets that we are listening to.
 * @param fd_max_ptr Pointer to the max file descriptor.
 */
void initialize_p2p_link(const char *peer_target_ip, int common_p2p_port,
                         int *p2p_fd_ptr, int *p2p_listen_fd_ptr,
                         fd_set *master_set_ptr, int *fd_max_ptr)
{
    printf("\n--- Configurando/Reiniciando Conexão P2P ---\n");
    printf("Tentando conectar ao peer %s na porta %d...\n", peer_target_ip, common_p2p_port);

    // First of all, we verify if the p2p_fd different of is diferent of -1. If so, it indicates that we do have a P2P connection
    // And now we need to close it and reset the p2p_fd
    if (*p2p_fd_ptr != -1)
    {
        printf("Limpando p2p_fd existente: %d\n", *p2p_fd_ptr);
        FD_CLR(*p2p_fd_ptr, master_set_ptr);
        close(*p2p_fd_ptr);
        *p2p_fd_ptr = -1;
    }

    // If p2p_listen_fd is not -1, it means that we are listening for P2P connections
    // and we need to close it and reset the p2p_listen_fd
    // This is necessary because we are going to try to connect to a peer first
    if (*p2p_listen_fd_ptr != -1)
    {
        printf("Limpando p2p_listen_fd existente: %d\n", *p2p_listen_fd_ptr);
        FD_CLR(*p2p_listen_fd_ptr, master_set_ptr);
        close(*p2p_listen_fd_ptr);
        *p2p_listen_fd_ptr = -1;
    }

    // We create a temporary socket to try to connect to the peer before setting up a listening socket
    // Note that if I got a connection, I will set the p2p_fd to the new socket
    int temp_p2p_socket;

    // If the socket creation fails, it will return SOCKET_ERROR and finish the function
    if ((temp_p2p_socket = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR)
    {
        perror("Erro ao criar socket P2P para tentativa de conexão ativa");
        return;
    }

    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(common_p2p_port);

    // Convert the peer_target_ip string to a binary address
    // If inet_pton fails, it will return a negative value and finish the function
    if (inet_pton(AF_INET, peer_target_ip, &peer_addr.sin_addr) <= 0)
    {
        perror("Erro ao converter endereço IP do peer para P2P");
        close(temp_p2p_socket);
        return;
    }

    // Here I try to connect to the IP address and port of the peer in a active way. It means that I am trying to connect to the peer
    if (connect(temp_p2p_socket, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) == 0)
    {
        // The temporary file descriptor was successfully connected to the peer
        // And I can pointer to this value now using the p2p_fd_ptr
        *p2p_fd_ptr = temp_p2p_socket;
        FD_SET(*p2p_fd_ptr, master_set_ptr);
        if (*p2p_fd_ptr > *fd_max_ptr)
        {
            *fd_max_ptr = *p2p_fd_ptr;
        }
        printf("Conectado com sucesso ao servidor peer (fd: %d).\n", *p2p_fd_ptr);
        return;
    }

    // I just arrives here if the connect() failed
    // If the connection failed, I close the temporary socket
    close(temp_p2p_socket);
    perror("Falha ao conectar ativamente ao peer (ou peer não disponível)");

    printf("Iniciando escuta P2P passiva na porta %d...\n", common_p2p_port);
    // I can just me comunicate with one server at a time
    int p2p_listen_backlog = 1;

    // Now I create a listening socket for incoming P2P connections
    *p2p_listen_fd_ptr = create_and_configure_listening_socket(common_p2p_port, p2p_listen_backlog);

    // If the listening socket creation fails, it will return SOCKET_ERROR and finish the function
    if (*p2p_listen_fd_ptr == SOCKET_ERROR)
    {
        fprintf(stderr, "Falha crítica ao tentar configurar escuta P2P passiva. P2P não estará disponível.\n");
        return;
    }

    // If the listening socket was created successfully, we add it to the master set
    FD_SET(*p2p_listen_fd_ptr, master_set_ptr);
    if (*p2p_listen_fd_ptr > *fd_max_ptr)
    {
        *fd_max_ptr = *p2p_listen_fd_ptr;
    }
}

/**
 * @brief Functon responsable for accepting a new client connection, and adding it to master_set.
 *
 * @param main_listen_fd Socket file descriptor responsable for listening new connections.
 * @param master_set_ptr Pointer to master_set.
 * @param fd_max_ptr Pointer to the max file descriptor in the master set.
 */
void handle_new_client_connection(int main_listen_fd, fd_set *master_set_ptr, int *fd_max_ptr)
{
    struct sockaddr_in new_client_addr;
    socklen_t new_client_addr_len = sizeof(new_client_addr);
    int new_client_fd;

    // Accept a new client connection and if it fails, I finish the function
    if ((new_client_fd = accept(main_listen_fd, (struct sockaddr *)&new_client_addr, &new_client_addr_len)) == SOCKET_ERROR)
    {
        perror("Erro ao aceitar nova conexão de cliente");
        return;
    }

    // Put the new client socket in the master set
    FD_SET(new_client_fd, master_set_ptr);
    if (new_client_fd > *fd_max_ptr)
    {
        *fd_max_ptr = new_client_fd;
    }

    char client_ip[INET_ADDRSTRLEN];
    // Convert the new client's IP address to a string just for logging
    if (inet_ntop(AF_INET, &new_client_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL)
    {
        perror("Erro ao converter endereço IP do novo cliente");
        printf("Novo cliente conectado (fd: %d), erro ao obter IP.\n", new_client_fd);
        return;
    }

    printf("Novo cliente conectado: %s:%d (fd: %d)\n",
           client_ip, ntohs(new_client_addr.sin_port), new_client_fd);
}

/**
 * @brief Function responsable for accepting an incoming P2P connection on the listening socket and closing the listening socket to not accept new connections.
 *
 * @param current_p2p_listen_fd The P2P listening socket that had activity.
 * @param p2p_comm_fd_main_ptr Pointer to the main p2p_fd variable (to store the new communication FD).
 * @param p2p_listen_fd_main_ptr Pointer to the main p2p_listen_fd variable (to be reset to -1).
 * @param master_set_ptr Pointer to the master set of FDs.
 * @param fd_max_ptr Pointer to the highest FD in the master set.
 */
void handle_incoming_p2p_connection(int current_p2p_listen_fd, int *p2p_comm_fd_main_ptr,
                                    int *p2p_listen_fd_main_ptr,
                                    fd_set *master_set_ptr, int *fd_max_ptr)
{
    printf("Detectada tentativa de conexão no socket de escuta P2P (fd: %d).\n", current_p2p_listen_fd);
    struct sockaddr_in incoming_peer_addr;
    socklen_t incoming_peer_addr_len = sizeof(incoming_peer_addr);
    int accepted_p2p_comm_fd;

    // Accept the incoming P2P connection
    // If accept() fails finish the function
    if ((accepted_p2p_comm_fd = accept(current_p2p_listen_fd, (struct sockaddr *)&incoming_peer_addr, &incoming_peer_addr_len)) == SOCKET_ERROR)
    {
        perror("Erro ao aceitar conexão P2P entrante");
        return;
    }

    char peer_ip[INET_ADDRSTRLEN];
    // Convert the incoming peer's IP address to a string for logging
    if (inet_ntop(AF_INET, &incoming_peer_addr.sin_addr, peer_ip, sizeof(peer_ip)) == NULL)
    {
        perror("Erro ao converter endereço IP do peer (conexão P2P aceita)");
        printf("Conexão P2P aceita (fd: %d), erro ao obter IP do peer.\n", accepted_p2p_comm_fd);
    }
    else
    {
        printf("Conexão P2P aceita de %s:%d no socket %d\n",
               peer_ip, ntohs(incoming_peer_addr.sin_port), accepted_p2p_comm_fd);
    }

    // If p2p_comm_fd_main_ptr is not -1, it means that we already have a P2P connection established
    // In this case, we close the new accepted connection and keep the existing one
    // This is to ensure that we only have one active P2P connection at a time
    // If p2p_comm_fd_main_ptr is -1, it means that we do not have a P2P connection established
    // and we can set the new accepted connection as the active P2P communication socket
    if (*p2p_comm_fd_main_ptr != -1)
    {
        printf("AVISO: Conexão P2P principal já existente (fd: %d). Fechando nova tentativa de conexão P2P (fd: %d).\n",
               *p2p_comm_fd_main_ptr, accepted_p2p_comm_fd);
        close(accepted_p2p_comm_fd);
    }
    else
    {
        // If we reach here, it means that we successfully accepted a new P2P connection and we do not have a before connection
        // We set the accepted P2P communication socket as the main P2P communication socket
        *p2p_comm_fd_main_ptr = accepted_p2p_comm_fd;
        FD_SET(*p2p_comm_fd_main_ptr, master_set_ptr);
        if (*p2p_comm_fd_main_ptr > *fd_max_ptr)
        {
            *fd_max_ptr = *p2p_comm_fd_main_ptr;
        }
        printf("Socket de comunicação P2P estabelecido através de escuta (fd: %d).\n", *p2p_comm_fd_main_ptr);

        printf("Fechando socket de escuta P2P (original fd: %d) e removendo do master_set.\n", current_p2p_listen_fd);
        // Close the listening socket since we are now connected to a peer
        FD_CLR(current_p2p_listen_fd, master_set_ptr);
        close(current_p2p_listen_fd);
        *p2p_listen_fd_main_ptr = -1;
    }
}

/**
 * @brief Processes data received or disconnection on an established P2P communication socket.
 *
 * @param current_p2p_comm_fd The P2P communication socket that had activity.
 * @param p2p_comm_fd_main_ptr Pointer to the main p2p_fd variable (to be reset to -1 in case of disconnection).
 * @param master_set_ptr Pointer to the master set of FDs.
 */
void handle_p2p_communication(int current_p2p_comm_fd, int *p2p_comm_fd_main_ptr,
                              fd_set *master_set_ptr)
{
    char p2p_buffer[MAX_MSG_SIZE];
    memset(p2p_buffer, 0, MAX_MSG_SIZE);
    ssize_t p2p_bytes_received;

    // Attempt to receive data from the P2P communication socket
    // If recv() fails, it will return a negative value. If retuns 0, the other server disconnecte. In both case I handle the disconnection
    if ((p2p_bytes_received = recv(current_p2p_comm_fd, p2p_buffer, MAX_MSG_SIZE - 1, 0)) <= 0)
    {
        if (p2p_bytes_received == 0)
        {
            printf("Socket P2P (fd %d) desconectou (conexão fechada pelo peer).\n", current_p2p_comm_fd);
        }
        else
        {
            perror("Erro no recv() do P2P");
        }
        close(current_p2p_comm_fd);
        FD_CLR(current_p2p_comm_fd, master_set_ptr);
        *p2p_comm_fd_main_ptr = -1;

        printf("Conexão P2P (original fd: %d) terminada. Tentativa de restabelecimento ocorrerá no próximo ciclo.\n", current_p2p_comm_fd);
        return;
    }

    // If we reach here, it means that we successfully received data from the P2P communication socket
    p2p_buffer[p2p_bytes_received] = '\0';
    printf("Recebido do peer (fd %d): '%s'\n", current_p2p_comm_fd, p2p_buffer);
}

/**
 * @brief Processes data received or disconnection on an established client socket.
 *
 * @param client_socket_fd The client socket that had activity.
 * @param master_set_ptr Pointer to the master set of FDs.
 */
void handle_client_communication(int client_socket_fd, fd_set *master_set_ptr)
{
    char buffer[MAX_MSG_SIZE];
    memset(buffer, 0, MAX_MSG_SIZE);
    ssize_t bytes_received;

    if ((bytes_received = recv(client_socket_fd, buffer, MAX_MSG_SIZE - 1, 0)) <= 0)
    {
        if (bytes_received == 0)
        {
            printf("Socket %d (cliente) desconectou.\n", client_socket_fd);
        }
        else
        {
            perror("Erro no recv() do cliente");
        }

        // Close the client socket and remove it from the master set
        close(client_socket_fd);
        FD_CLR(client_socket_fd, master_set_ptr);
        return;
    }

    // If we reach here, it means that we successfully received data from the client socket
    buffer[bytes_received] = '\0';
    printf("Recebido do cliente (fd %d): %s\n", client_socket_fd, buffer);

    // Simple answer generated by AI for tests
    char reply_msg[MAX_MSG_SIZE + 60];
    snprintf(reply_msg, sizeof(reply_msg), "Servidor: Msg recebida do cliente %d: '%s'", client_socket_fd, buffer);

    ssize_t bytes_sent = send(client_socket_fd, reply_msg, strlen(reply_msg), 0);
    if (bytes_sent == SOCKET_ERROR)
    {
        perror("Erro ao enviar resposta ao cliente");
        // Considere fechar este cliente se o send falhar
        close(client_socket_fd);
        FD_CLR(client_socket_fd, master_set_ptr);
    }
    else
    {
        printf("Resposta enviada ao cliente (fd %d): '%s'\n", client_socket_fd, reply_msg);
    }
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

    // Socket that indicated the active P2P connection
    // When the connection is active, the value is != -1
    // When the value is -1, it means that we are not connected to a peer
    // Notice that if p2p_fd is -1, probably p2p_listen_fd is not -1, because we are listening for incoming P2P connections
    // And if p2p_fd is not -1, then p2p_listen_fd is -1, because we are already connected to a peer
    int p2p_fd = -1;

    // Socket responsable for listening incoming P2P connections
    // Whne this values is -1, it means that we are not listening for P2P connections. It happens when we are connected to a peer
    // That indicates that the connection is closed, because my server just connects to one server at a time
    // Notice that if p2p_fd is -1, probably p2p_listen_fd is not -1, because we are listening for incoming P2P connections
    // And if p2p_fd is not -1, then p2p_listen_fd is -1, because we are already connected to a peer
    int p2p_listen_fd = -1;

    printf("Servidor iniciando...\n");

    // Socket responsible for listening to clients
    // This socket is used to accept new client connections
    int listen_fd;

    // Backlog for clients
    // This values indicated that the maximum number of pending connections is 10
    int client_backlog = 10;

    printf("Configurando socket de escuta para clientes na porta %d...\n", client_listen_port);
    listen_fd = create_and_configure_listening_socket(client_listen_port, client_backlog);
    if (listen_fd == SOCKET_ERROR)
    {
        error_exit("Falha crítica ao configurar socket de escuta para clientes");
    }

    // Here we initialize the sockets file descriptor sets
    // [master_set] will contain all the sockets that we are listening to
    // [read_fds] will be used by select() to indicate which sockets have activity
    fd_set master_set, read_fds;

    // fd_max will be used by select() to indicate the maximum file descriptor that we are listening to
    // It is necessary to iterate through all the sockets in master_set
    int fd_max;

    FD_ZERO(&master_set);
    FD_ZERO(&read_fds);

    FD_SET(listen_fd, &master_set);
    fd_max = listen_fd;

    initialize_p2p_link(peer_target_ip, common_p2p_port,
                        &p2p_fd, &p2p_listen_fd,
                        &master_set, &fd_max);

    while (1)
    {
        if (p2p_fd == -1 && p2p_listen_fd == -1)
        {
            printf("Sem link P2P ativo ou escutando. Tentando restabelecer P2P...\n");
            initialize_p2p_link(peer_target_ip, common_p2p_port,
                                &p2p_fd, &p2p_listen_fd,
                                &master_set, &fd_max);
        }

        read_fds = master_set;
        printf("\nAguardando atividade nos sockets...\n");

        // The select() function blocks until one or more sockets in the master_set have activity
        // It will modify read_fds to indicate which sockets have activity
        // If select() returns, it means at least one socket has activity or an error occurred
        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == SOCKET_ERROR)
        {
            error_exit("Erro crítico no select");
        }
        printf("Atividade detectada!\n");

        // This loop just occurs when select() returns, meaning there is activity on at least one socket
        // We iterate through all possible file descriptors to check which ones have activity
        // fd_max is the highest file descriptor in the master_set, so we iterate from 0 to fd_max
        // This is necessary because select() does not return the specific file descriptors that have activity,
        // it only tells us that at least one of them has activity.
        for (int i = 0; i <= fd_max; i++)
        {
            // FD_ISSET verify if the i socket has activity
            // This information is stored in read_fds, which was modified by select()
            // If the socket i does not have activity, skip to the next iteration
            int has_i_socket_active = FD_ISSET(i, &read_fds);
            if (!has_i_socket_active)
                continue;

            // When the loop reaches here, it means that listen_fd
            // that is the main socket for clients
            // is receiving a new connection
            if (i == listen_fd)
            {
                handle_new_client_connection(listen_fd, &master_set, &fd_max);
            }
            // If p2p_listen_fd is not -1, it means that we are listening for P2P connections
            // and we check if the current socket is the P2P listen socket
            // I did the p2p_listen_fd != -1 first just to indicate that we are listening for P2P connections
            else if (p2p_listen_fd != -1 && i == p2p_listen_fd)
            {
                handle_incoming_p2p_connection(i, &p2p_fd, &p2p_listen_fd, &master_set, &fd_max);
            }
            // If p2p_fd is not -1, it means that we have an active P2P connection
            // and we check if the current socket is the P2P communication socket
            // If i == p2p_fd, it means that we have activity on the P2P communication socket and we can read data from it
            else if (p2p_fd != -1 && i == p2p_fd)
            {
                handle_p2p_communication(i, &p2p_fd, &master_set);
            }
            // The last scenario is when the we do not have a p2p connections either we have a new connection
            // That indicate that the client is just sedind data or disconnecting
            else
            {
                handle_client_communication(i, &master_set);
            }
        }
    }

    return 0;
}
