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
#include <time.h>
typedef enum
{
    ROLE_STATUS_SERVER,
    ROLE_LOCATION_SERVER
} ServerRole;

// Global state that indicate the role of the server (SS or SL)
ServerRole my_role;

SensorInfo *get_sensor_by_socket_fd(SensorInfo *sensors_array, int sensor_count, int socket_fd)
{
    for (int i = 0; i < sensor_count; i++)
        if (sensors_array[i].socket_fd == socket_fd)
            return &sensors_array[i];
    return NULL;
}

SensorInfo *get_sensor_by_id(SensorInfo *sensors_array, const char *sensor_id)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (sensors_array[i].is_active && strcmp(sensors_array[i].sensor_id_str, sensor_id) == 0)
            return &sensors_array[i];

    return NULL;
}

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
        fprintf(stderr, "[SERVER] AVISO: Truncated message (limite de %zu bytes).\n", buffer_size);
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

    // First of all, we verify if the p2p_fd different of is diferent of -1. If so, it indicates that we do have a P2P connection
    // And now we need to close it and reset the p2p_fd
    if (*p2p_fd_ptr != -1)
    {
        FD_CLR(*p2p_fd_ptr, master_set_ptr);
        close(*p2p_fd_ptr);
        *p2p_fd_ptr = -1;
    }

    // If p2p_listen_fd is not -1, it means that we are listening for P2P connections
    // and we need to close it and reset the p2p_listen_fd
    // This is necessary because we are going to try to connect to a peer first
    if (*p2p_listen_fd_ptr != -1)
    {
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

        char send_buffer[MAX_MSG_SIZE];
        build_message(send_buffer, MAX_MSG_SIZE, REQ_CONNPEER, NULL, NULL);
        printf("[P2P] REQ_CONNPEER \n");
        send(*p2p_fd_ptr, send_buffer, strlen(send_buffer), 0);
        return;
    }

    // I just arrives here if the connect() failed
    // If the connection failed, I close the temporary socket
    close(temp_p2p_socket);

    printf("No peer found, starting to listen...\n");
    // I can just me comunicate with one server at a time
    int p2p_listen_backlog = 1;

    // Now I create a listening socket for incoming P2P connections
    *p2p_listen_fd_ptr = create_and_configure_listening_socket(common_p2p_port, p2p_listen_backlog);

    // If the listening socket creation fails, it will return SOCKET_ERROR and finish the function
    if (*p2p_listen_fd_ptr == SOCKET_ERROR)
    {
        fprintf(stderr, "Critical error.\n");
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
    printf("[SERVER] New sensor");

    struct sockaddr_in new_client_addr;
    socklen_t new_client_addr_len = sizeof(new_client_addr);
    int new_client_fd;

    // Accept a new client connection and if it fails, I finish the function
    if ((new_client_fd = accept(main_listen_fd, (struct sockaddr *)&new_client_addr, &new_client_addr_len)) == SOCKET_ERROR)
    {
        printf("[SERVER] error adding new client");
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
    inet_ntop(AF_INET, &new_client_addr.sin_addr, client_ip, sizeof(client_ip));
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
    struct sockaddr_in incoming_peer_addr;
    socklen_t incoming_peer_addr_len = sizeof(incoming_peer_addr);
    int accepted_p2p_comm_fd;

    // Accept the incoming P2P connection
    // If accept() fails finish the function
    if ((accepted_p2p_comm_fd = accept(current_p2p_listen_fd, (struct sockaddr *)&incoming_peer_addr, &incoming_peer_addr_len)) == SOCKET_ERROR)
    {
        perror("[SERVER]Error in accepting p2p connection");
        return;
    }

    char peer_ip[INET_ADDRSTRLEN];
    // Convert the incoming peer's IP address to a string for logging
    inet_ntop(AF_INET, &incoming_peer_addr.sin_addr, peer_ip, sizeof(peer_ip));

    // If p2p_comm_fd_main_ptr is not -1, it means that we already have a P2P connection established
    // In this case, we close the new accepted connection and keep the existing one
    // This is to ensure that we only have one active P2P connection at a time
    // If p2p_comm_fd_main_ptr is -1, it means that we do not have a P2P connection established
    // and we can set the new accepted connection as the active P2P communication socket
    if (*p2p_comm_fd_main_ptr != -1)
    {
        printf("[SERVER] P2P limit is one\n");

        char send_buffer[MAX_MSG_SIZE];
        build_message(send_buffer, sizeof(send_buffer), MSG_ERROR, "1", NULL);
        send(accepted_p2p_comm_fd, send_buffer, strlen(send_buffer), 0);
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

        // Close the listening socket since we are now connected to a peer
        FD_CLR(current_p2p_listen_fd, master_set_ptr);
        close(current_p2p_listen_fd);
        *p2p_listen_fd_main_ptr = -1;
    }
}

void register_new_sensor(SensorInfo *sensors_array, int source_fd, char p1[256], long long *next_sensor_id_ptr, int *sensor_count_ptr, char send_buffer[500]);

void handle_error_adding_more_than_max_clients(int source_fd, char send_buffer[500], fd_set *master_set_ptr);

void handle_diagnose_command(char p2[256], SensorInfo *sensors_array, int source_fd, char send_buffer[500]);

void handle_check_failure_command(int source_fd, char p1[256], SensorInfo *sensors_array, int *p2p_fd_ptr, PendingRequest *pending_requests_array);

void handle_check_alert_request(SensorInfo *sensors_array, char p1[256], int source_fd, char send_buffer[500]);

void handle_disconnect_request(SensorInfo *sensors_array, char p1[256], char send_buffer[500], int source_fd, int *sensor_count_ptr, fd_set *master_set_ptr);

void handle_localte_method(SensorInfo *sensors_array, char p1[256], char send_buffer[500], int source_fd);

void handle_check_alert_response(int source_fd, const char *location_payload,
                                 SensorInfo *sensors_array, char *send_buffer,
                                 PendingRequest *pending_requests_array)
{
    printf("[SS] RES_CHECKALERT %s\n", location_payload);

    int pending_slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (pending_requests_array[i].is_active)
        {
            pending_slot = i;
            break;
        }

    if (pending_slot != -1)
    {
        int original_client_fd = pending_requests_array[pending_slot].original_client_fd;

        build_message(send_buffer, MAX_MSG_SIZE, RES_SENSSTATUS, location_payload, NULL);
        send(original_client_fd, send_buffer, strlen(send_buffer), 0);

        pending_requests_array[pending_slot].is_active = 0;
    }
    else
    {
        fprintf(stderr, "[SS] Failed client comunication.\n");
    }
}

/**
 * @brief Processes any incoming message, whether from a client or a peer.
 * This function acts as the protocol brain of the server.
 * @param source_fd The socket from which the message originated.
 * @param received_buffer The raw message string received.
 * @param p2p_handshake_complete_ptr Pointer to the P2P handshake completion flag (can be NULL for clients).
 * @param master_set_ptr Pointer to the master set of file descriptors.
 * @param p2p_fd_ptr Pointer to the P2P connection file descriptor.
 * @param sensors_array Array of connected sensors.
 * @param sensor_count_ptr Pointer to the current number of connected sensors.
 * @param next_sensor_id_ptr Pointer to the next sensor ID value.
 * @param pending_requests_array Array of pending P2P requests.
 */
void process_incoming_message(int source_fd, const char *received_buffer, int *p2p_handshake_complete_ptr,
                              fd_set *master_set_ptr, int *p2p_fd_ptr,
                              SensorInfo *sensors_array, int *sensor_count_ptr,
                              long long *next_sensor_id_ptr, PendingRequest *pending_requests_array)
{
    int received_code;
    char p1[256], p2[256];
    char send_buffer[MAX_MSG_SIZE];

    ParseResultType result = parse_message(received_buffer, &received_code, p1, p2);

    if (result == PARSE_ERROR_INVALID_FORMAT)
    {
        fprintf(stderr, "[SERVER] Invalid format message: \"%s\"\n", received_buffer);
        return;
    }

    switch (received_code)
    {
    case REQ_CONNSEN: // Código 23
        if (*sensor_count_ptr >= MAX_CLIENTS)
            handle_error_adding_more_than_max_clients(source_fd, send_buffer, master_set_ptr);
        else
            register_new_sensor(sensors_array, source_fd, p1, next_sensor_id_ptr, sensor_count_ptr, send_buffer);
        break;

    case REQ_DISCSEN: // 25
        printf("[SERVER] Recebido REQ_DISCSEN do sensor ID %s (fd: %d).\n", p1, source_fd);

        handle_disconnect_request(sensors_array, p1, send_buffer, source_fd, sensor_count_ptr, master_set_ptr);

        break;
    case REQ_SENSLOC: // 38
        if (my_role == ROLE_LOCATION_SERVER)
            handle_localte_method(sensors_array, p1, send_buffer, source_fd);
        break;
    case 40: //  REQ_SENSSTATUS e REQ_LOCLIST
        if (my_role == ROLE_STATUS_SERVER)
        {
            handle_check_failure_command(source_fd, p1, sensors_array, p2p_fd_ptr, pending_requests_array);
        }
        else if (my_role == ROLE_LOCATION_SERVER)
        {
            handle_diagnose_command(p2, sensors_array, source_fd, send_buffer);
        }
        break;

    case REQ_CONNPEER: // 20
        printf("[P2P] REQ_CONNPEER\n");

        const char *peer_id_para_resposta = "PEER_ID_1";
        build_message(send_buffer, MAX_MSG_SIZE, RES_CONNPEER, peer_id_para_resposta, NULL);
        send(source_fd, send_buffer, strlen(send_buffer), 0);
        break;

    case RES_CONNPEER: // 21

        if (*p2p_handshake_complete_ptr == 1)
            return;

        if (my_role == ROLE_STATUS_SERVER)
        {
            printf("[P2P] New Peer ID: %s.\n", p1);

            const char *nosso_id_para_o_peer = "PEER_ID_SS";
            printf("[P2P] Peer PidSj connected\n");

            build_message(send_buffer, MAX_MSG_SIZE, RES_CONNPEER, nosso_id_para_o_peer, NULL);
            send(source_fd, send_buffer, strlen(send_buffer), 0);
        }
        else if (my_role == ROLE_LOCATION_SERVER)
        {
            printf("[P2P] New Peer ID: %s.\n", p1);
        }
        *p2p_handshake_complete_ptr = 1;
        break;

    case REQ_DISCPEER: // 22
        printf("[P2P] REQ_DISCPEER\n");
        if (strcmp(p1, "PEER_ID_1") == 0)
        {
            build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "2", "Peer not found");
            send(source_fd, send_buffer, strlen(send_buffer), 0);
            return;
        }

        build_message(send_buffer, MAX_MSG_SIZE, MSG_OK, "1", "Peer PEER_ID_1 disconnected");
        send(source_fd, send_buffer, strlen(send_buffer), 0);

        printf("[P2P] A fechar a ligação P2P (fd: %d) do nosso lado.\n", source_fd);
        close(source_fd);
        FD_CLR(source_fd, master_set_ptr);
        *p2p_fd_ptr = -1;
        *p2p_handshake_complete_ptr = 0;
        break;

    case REQ_CHECKALERT: // 36 (SS -> SL)
        if (my_role == ROLE_LOCATION_SERVER)
            handle_check_alert_request(sensors_array, p1, source_fd, send_buffer);
        break;
    case RES_CHECKALERT: // 37 (SL -> SS)
        if (my_role == ROLE_STATUS_SERVER)
            handle_check_alert_response(source_fd, p1, sensors_array, send_buffer, pending_requests_array);
        break;

    case RES_CONNSEN: // 24
    case RES_SENSLOC: // 39
    case 41:
        printf(">> AVISO: Recebido código de resposta do servidor (%d) do fd %d. Inesperado.\n", received_code, source_fd);
        break;

    case MSG_OK:
        printf("[P2P] %s \n", p2);
        break;

    case MSG_ERROR: // 255
        if (source_fd == *p2p_fd_ptr)
        {
            printf("[SS] Recebido MSG_ERROR do SL (peer).\n");

            int pending_slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (pending_requests_array[i].is_active)
                {
                    pending_slot = i;
                    break;
                }
            }
            if (pending_slot != -1)
            {
                int original_client_fd = pending_requests_array[pending_slot].original_client_fd;
                printf("[SS] A encaminhar o erro para o cliente original (fd: %d).\n", original_client_fd);

                build_message(send_buffer, sizeof(send_buffer), MSG_ERROR, p1, NULL);
                send(original_client_fd, send_buffer, strlen(send_buffer), 0);

                pending_requests_array[pending_slot].is_active = 0;
            }
        }

        break;

    default:
        printf("[SERVER] Código de mensagem desconhecido (%d) recebido do fd %d. Ignorando.\n", received_code, source_fd);
        break;
    }
}

void handle_localte_method(SensorInfo *sensors_array, char p1[256], char send_buffer[500], int source_fd)
{

    printf("[SL] REQ_SENSLOC %s.\n", p1);
    SensorInfo *sensor = get_sensor_by_id(sensors_array, p1);
    if (sensor != NULL)
    {
        char location_str[5];
        snprintf(location_str, sizeof(location_str), "%d", sensor->location_id);
        build_message(send_buffer, MAX_MSG_SIZE, RES_SENSLOC, sensor->sensor_id_str, location_str);
    }
    else
        build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "10", "Sensor not found");

    send(source_fd, send_buffer, strlen(send_buffer), 0);
}

void handle_disconnect_request(SensorInfo *sensors_array, char p1[256], char send_buffer[500], int source_fd, int *sensor_count_ptr, fd_set *master_set_ptr)
{
    char *server_role_str = (my_role == ROLE_STATUS_SERVER) ? "SS" : "SL";
    SensorInfo *sensor_info = get_sensor_by_id(sensors_array, p1);
    if (sensor_info == NULL)
    {
        fprintf(stderr, "[%s] REQ_DISCSEN sensor not founded (ID: %s).\n", server_role_str, p1);
        build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "10", "Sensor not found");
        send(source_fd, send_buffer, strlen(send_buffer), 0);
        return;
    }

    build_message(send_buffer, MAX_MSG_SIZE, MSG_OK, "1", server_role_str);
    send(source_fd, send_buffer, strlen(send_buffer), 0);

    sensor_info->is_active = 0;
    (*sensor_count_ptr)--;

    close(source_fd);
    FD_CLR(source_fd, master_set_ptr);
}

void handle_check_alert_request(SensorInfo *sensors_array, char p1[256], int source_fd, char send_buffer[500])
{

    printf("[SL] REQ_CHECKALERT %s\n", p1);
    SensorInfo *found_sensor = get_sensor_by_id(sensors_array, p1);

    if (found_sensor != NULL)
    {
        char location_str[5];
        snprintf(location_str, sizeof(location_str), "%d", found_sensor->location_id);

        printf("[SL] Found location of sensor %s: location %s\n",
               p1, location_str);

        printf("[SL] Sending RES_CHECKALERT %s to SS\n", location_str);

        build_message(send_buffer, MAX_MSG_SIZE, RES_CHECKALERT, location_str, NULL);
    }
    else
    {
        printf("[SL] Sensor %s not found\n", p1);
        build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "10", NULL);
    }
    send(source_fd, send_buffer, strlen(send_buffer), 0);
}

void handle_check_failure_command(int source_fd, char p1[256], SensorInfo *sensors_array, int *p2p_fd_ptr, PendingRequest *pending_requests_array)
{

    printf("[SS] REQ_SENSSTATUS %s\n", p1);

    char send_buffer[MAX_MSG_SIZE];
    SensorInfo *found_sensor = get_sensor_by_id(sensors_array, p1);

    if (found_sensor == NULL)
    {
        build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "10", NULL);
        send(source_fd, send_buffer, strlen(send_buffer), 0);
        return;
    }

    if (found_sensor->is_active == 0)
    {
        build_message(send_buffer, MAX_MSG_SIZE, MSG_OK, "03", NULL);
        send(source_fd, send_buffer, strlen(send_buffer), 0);
        return;
    }

    printf("[SS] Sensor %s status = 1 (failure detected)\n", p1);

    if (p2p_fd_ptr != NULL && *p2p_fd_ptr != -1)
    {
        printf("[SS] Sending REQ_CHECKALERT %s to SL\n", p1);

        int pending_slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (pending_requests_array[i].is_active == 0)
            {
                pending_slot = i;
                break;
            }

        if (pending_slot != -1)
        {
            pending_requests_array[pending_slot].is_active = 1;
            pending_requests_array[pending_slot].original_client_fd = source_fd;
            strncpy(pending_requests_array[pending_slot].sensor_id_in_query, p1, sizeof(pending_requests_array[pending_slot].sensor_id_in_query) - 1);

            build_message(send_buffer, MAX_MSG_SIZE, REQ_CHECKALERT, p1, NULL);
        }
        else
        {
            build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "0", NULL);
        }
        send(*p2p_fd_ptr, send_buffer, strlen(send_buffer), 0);

        return;
    }

    build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "2", NULL);
    send(source_fd, send_buffer, strlen(send_buffer), 0);
}

void handle_diagnose_command(char p2[256], SensorInfo *sensors_array, int source_fd, char send_buffer[500])
{
    printf("[SL] REQ_LOCLIST %s\n", p2);
    char sensor_list_payload[MAX_MSG_SIZE - 10] = "";

    int target_loc = atoi(p2);
    int found_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (sensors_array[i].is_active && sensors_array[i].location_id == target_loc)
        {
            if (found_count > 0)
                strncat(sensor_list_payload, ",", sizeof(sensor_list_payload) - strlen(sensor_list_payload) - 1);

            strncat(sensor_list_payload, sensors_array[i].sensor_id_str, sizeof(sensor_list_payload) - strlen(sensor_list_payload) - 1);
            found_count++;
        }
    }

    if (found_count > 0)
    {
        printf("[SL] Found sensors at location %d\n", target_loc);
        char target_loc_str[16];
        snprintf(target_loc_str, sizeof(target_loc_str), "%d", target_loc);
        build_message(send_buffer, MAX_MSG_SIZE, RES_LOCLIST, target_loc_str, sensor_list_payload);
    }
    else
    {
        printf("[SL] Location not found\n");
        build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "10", NULL);
    }
    send(source_fd, send_buffer, strlen(send_buffer), 0);
}

void handle_error_adding_more_than_max_clients(int source_fd, char send_buffer[500], fd_set *master_set_ptr)
{
    printf("[SERVER] Sensor limit reached.\n");
    build_message(send_buffer, MAX_MSG_SIZE, MSG_ERROR, "9", "Sensor limit exceeded");
    send(source_fd, send_buffer, strlen(send_buffer), 0);

    close(source_fd);
    FD_CLR(source_fd, master_set_ptr);
}

void register_new_sensor(SensorInfo *sensors_array, int source_fd, char p1[256], long long *next_sensor_id_ptr, int *sensor_count_ptr, char send_buffer[500])
{
    // Try to find a empty slot in the sensors_array
    int slot_index = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (sensors_array[i].is_active == 0)
        {
            slot_index = i;
            break;
        }
    }

    if (slot_index != -1)
    {

        sensors_array[slot_index].is_active = 1;
        sensors_array[slot_index].socket_fd = source_fd;
        sensors_array[slot_index].location_id = atoi(p1);

        srand(time(NULL));
        sensors_array[slot_index].risk_status = rand() % 2;

        // Generate a new sensor ID and store it as a string
        snprintf(sensors_array[slot_index].sensor_id_str, 20, "%lld", *next_sensor_id_ptr);
        (*next_sensor_id_ptr)++;
        (*sensor_count_ptr)++;

        char *sensor_type = my_role == ROLE_STATUS_SERVER ? "SS" : "SL";

        printf("[%s] Client IdCi added: %s\n", sensor_type,
               sensors_array[slot_index].sensor_id_str);

        build_message(send_buffer, MAX_MSG_SIZE, RES_CONNSEN, sensors_array[slot_index].sensor_id_str, NULL);
        send(source_fd, send_buffer, strlen(send_buffer), 0);
    }
    // WANING: THIS SHOULD NEVER HAPPEN
    else
    {
        fprintf(stderr, "[SERVER] CRITICAL ERROR: sensor_count is out of sync with the sensors array.\n");
    }
}

/**
 * @brief Processes data received or disconnection on an established P2P communication socket.
 *
 * @param current_p2p_comm_fd The P2P communication socket that had activity.
 * @param p2p_comm_fd_main_ptr Pointer to the main p2p_fd variable (to be reset to -1 in case of disconnection).
 * @param p2p_handshake_complete_ptr Pointer to the flag indicating if the P2P handshake is complete.
 * @param master_set_ptr Pointer to the master set of FDs.
 */
void handle_p2p_communication(int current_p2p_comm_fd, int *p2p_comm_fd_main_ptr, int *p2p_handshake_complete_ptr, fd_set *master_set_ptr, SensorInfo *sensors_array, int *sensor_count_ptr,
                              long long *next_sensor_id_ptr, PendingRequest *pending_requests_array)
{
    char p2p_buffer[MAX_MSG_SIZE];
    memset(p2p_buffer, 0, MAX_MSG_SIZE);
    ssize_t p2p_bytes_received;

    // Attempt to receive data from the P2P communication socket
    // If recv() fails, it will return a negative value. If retuns 0, the other server disconnecte. In both case I handle the disconnection
    if ((p2p_bytes_received = recv(current_p2p_comm_fd, p2p_buffer, MAX_MSG_SIZE - 1, 0)) <= 0)
    {
        if (*p2p_handshake_complete_ptr == 0 && p2p_bytes_received == 0)
        {
            printf("[P2P DEBUG] recv() retornou 0 durante o handshake. A ignorar por agora para evitar desconexão prematura.\n");
        }
        else if (p2p_bytes_received == 0)
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
        *p2p_handshake_complete_ptr = 0;

        return;
    }

    // If we reach here, it means that we successfully received data from the P2P communication socket
    p2p_buffer[p2p_bytes_received] = '\0';
    process_incoming_message(current_p2p_comm_fd, p2p_buffer,
                             p2p_handshake_complete_ptr, master_set_ptr, p2p_comm_fd_main_ptr,
                             sensors_array, sensor_count_ptr,
                             next_sensor_id_ptr, pending_requests_array);
}

/**
 * @brief Processes data received or disconnection on an established client socket.
 *
 * @param client_socket_fd The client socket that had activity.
 * @param master_set_ptr Pointer to the master set of FDs.
 * @param sensors_array Pointer to the array of connected sensors.
 * @param sensor_count_ptr Pointer to the current number of connected sensors.
 * @param next_sensor_id_ptr Pointer to the next sensor ID value.
 */
void handle_client_communication(int client_socket_fd, fd_set *master_set_ptr,
                                 SensorInfo *sensors_array, int *sensor_count_ptr, long long *next_sensor_id_ptr, int *p2p_fd_ptr,
                                 PendingRequest *pending_requests_array)
{
    char recv_buffer[MAX_MSG_SIZE];
    memset(recv_buffer, 0, MAX_MSG_SIZE);
    ssize_t bytes_received;

    if ((bytes_received = recv(client_socket_fd, recv_buffer, MAX_MSG_SIZE - 1, 0)) <= 0)
    {
        // Close the client socket and remove it from the master set
        SensorInfo *sensor_to_close = get_sensor_by_socket_fd(sensors_array, *sensor_count_ptr, client_socket_fd);
        sensor_to_close->is_active = 0;
        (*sensor_count_ptr)--;
        close(client_socket_fd);
        FD_CLR(client_socket_fd, master_set_ptr);
        return;
    }

    // If we reach here, it means that we successfully received data from the client socket
    recv_buffer[bytes_received] = '\0';

    int received_code;
    char p1[256], p2[256];
    ParseResultType result = parse_message(recv_buffer, &received_code, p1, p2);

    if (result == PARSE_ERROR_INVALID_FORMAT)
    {
        fprintf(stderr, "[SERVER] Message format error");
        return;
    }

    process_incoming_message(client_socket_fd, recv_buffer, NULL,
                             master_set_ptr, p2p_fd_ptr, sensors_array, sensor_count_ptr, next_sensor_id_ptr, pending_requests_array);
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
    int common_p2p_port = atoi(argv[2]);
    int client_listen_port = atoi(argv[3]);
    // end args validation

    // Server function

    if (client_listen_port == 60000)
    {
        my_role = ROLE_LOCATION_SERVER;
        printf("[SL] stated at port %d\n", client_listen_port);
    }
    else if (client_listen_port == 61000)
    {
        my_role = ROLE_STATUS_SERVER;
        printf("[SS] started at port %d\n", client_listen_port);
    }
    else
    {
        error_exit("Invalid port. Use 60000 for SL and 61000 for SS");
    }
    // end server function

    // start of the sensors configuration
    SensorInfo connected_sensors[MAX_CLIENTS];
    PendingRequest pending_requests[MAX_CLIENTS];
    int sensor_count = 0;
    static long long next_sensor_id_numeric = 1000000001L;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        connected_sensors[i].is_active = 0;
        connected_sensors[i].socket_fd = -1;
        pending_requests[i].is_active = 0;
    }
    // end of the sensors configuration

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

    // Variable that represents if the P2P handshake is complete
    int p2p_handshake_complete = 0;

    // Socket responsible for listening to clients
    // This socket is used to accept new client connections
    int listen_fd;

    // Backlog for clients
    // This values indicated that the maximum number of pending connections is 10
    int client_backlog = 10;

    listen_fd = create_and_configure_listening_socket(client_listen_port, client_backlog);
    if (listen_fd == SOCKET_ERROR)
    {
        error_exit("Critical failure in create_and_configure_listening_socket");
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
    FD_SET(STDIN_FILENO, &master_set);

    fd_max = listen_fd;

    initialize_p2p_link(peer_target_ip, common_p2p_port,
                        &p2p_fd, &p2p_listen_fd,
                        &master_set, &fd_max);

    while (1)
    {
        if (p2p_fd == -1 && p2p_listen_fd == -1)
        {
            initialize_p2p_link(peer_target_ip, common_p2p_port,
                                &p2p_fd, &p2p_listen_fd,
                                &master_set, &fd_max);
        }

        read_fds = master_set;

        // The select() function blocks until one or more sockets in the master_set have activity
        // It will modify read_fds to indicate which sockets have activity
        // If select() returns, it means at least one socket has activity or an error occurred
        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == SOCKET_ERROR)
        {
            error_exit("Critical error select");
        }

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

            if (i == STDIN_FILENO)
            {
                char command_buffer[MAX_MSG_SIZE];
                // EOF
                if (fgets(command_buffer, sizeof(command_buffer), stdin) == NULL)
                    continue;

                // Remove a nova linha do final
                command_buffer[strcspn(command_buffer, "\n")] = 0;

                if (strcmp(command_buffer, "close connection") == 0)
                {
                    if (p2p_fd != -1)
                    {
                        printf("[SERVER] Finishing P2P...\n");
                        char send_buffer[MAX_MSG_SIZE];

                        build_message(send_buffer, MAX_MSG_SIZE, REQ_DISCPEER, NULL, NULL);

                        send(p2p_fd, send_buffer, strlen(send_buffer), 0);
                    }
                    // Finishing the code
                    exit(0);
                }
                else
                {
                    printf("[SERVER] Command not found: \"%s\"\n", command_buffer);
                }
                continue;
            }
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
                handle_p2p_communication(i, &p2p_fd, &p2p_handshake_complete, &master_set, connected_sensors, &sensor_count, &next_sensor_id_numeric, pending_requests);
            }
            // The last scenario is when the we do not have a p2p connections either we have a new connection
            // That indicate that the client is just sedind data or disconnecting
            else
            {
                handle_client_communication(i, &master_set, connected_sensors, &sensor_count, &next_sensor_id_numeric, &p2p_fd, pending_requests);
            }
        }
    }

    return 0;
}
