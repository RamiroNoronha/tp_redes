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

typedef struct
{
    // File Descriptors
    int sl_fd;
    int ss_fd;

    char sl_sensor_id[20];
    char ss_sensor_id[20];
    int registered_on_sl;
    int registered_on_ss;

    char send_buffer[MAX_MSG_SIZE];
    char recv_buffer[MAX_MSG_SIZE];
} SensorState;

void error_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int connect_to_server(const char *server_ip, int server_port)
{
    int server_fd;
    struct sockaddr_in server_addr;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == SOCKET_ERROR)
    {
        perror("socket");
        return SOCKET_ERROR;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(server_fd);
        return SOCKET_ERROR;
    }
    if (connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        perror("connect");
        close(server_fd);
        return SOCKET_ERROR;
    }
    return server_fd;
}

void build_message(char *dest_buffer, size_t buffer_size, int code, const char *p1, const char *p2)
{
    memset(dest_buffer, 0, buffer_size);
    if (p1 && p2)
        snprintf(dest_buffer, buffer_size, "%d %s %s", code, p1, p2);
    else if (p1)
        snprintf(dest_buffer, buffer_size, "%d %s", code, p1);
    else
        snprintf(dest_buffer, buffer_size, "%d", code);
}

ParseResultType parse_message(const char *buffer, int *code, char *p1, char *p2)
{
    char copy[MAX_MSG_SIZE];
    strncpy(copy, buffer, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    if (p1)
        p1[0] = '\0';
    if (p2)
        p2[0] = '\0';
    if (code)
        *code = -1;
    char *token, *endptr;
    token = strtok(copy, " \t\n\r");
    if (!token)
        return PARSE_ERROR_INVALID_FORMAT;
    *code = strtol(token, &endptr, 10);
    if (*endptr != '\0')
        return PARSE_ERROR_INVALID_FORMAT;
    token = strtok(NULL, " \t\n\r");
    if (!token)
        return PARSE_SUCCESS_CODE_ONLY;
    if (p1)
        strncpy(p1, token, 255);
    token = strtok(NULL, " \t\n\r");
    if (!token)
        return PARSE_SUCCESS_ONE_PAYLOAD;
    if (p2)
        strncpy(p2, token, 255);
    if (strtok(NULL, " \t\n\r"))
        return PARSE_ERROR_INVALID_FORMAT;
    return PARSE_SUCCESS_TWO_PAYLOADS;
}

const char *get_region_name(int loc_id)
{
    if (loc_id >= 1 && loc_id <= 3)
        return "Norte";
    if (loc_id >= 4 && loc_id <= 5)
        return "Sul";
    if (loc_id >= 6 && loc_id <= 7)
        return "Leste";
    if (loc_id >= 8 && loc_id <= 10)
        return "Oeste";
    return "Região Desconhecida";
}

void process_keyboard_input(SensorState *state)
{
    char command_buffer[MAX_MSG_SIZE];
    if (fgets(command_buffer, sizeof(command_buffer), stdin) == NULL)
    {
        printf("\n[CLIENT] EOF detectado. A preparar para encerrar...\n");
        build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->sl_sensor_id, NULL);
        send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
        build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->ss_sensor_id, NULL);
        send(state->ss_fd, state->send_buffer, strlen(state->send_buffer), 0);
        return;
    }
    command_buffer[strcspn(command_buffer, "\n")] = 0;

    char temp_copy[MAX_MSG_SIZE];
    strncpy(temp_copy, command_buffer, sizeof(temp_copy));
    char *command = strtok(temp_copy, " ");
    if (!command)
        return;

    if (strcmp(command, "close") == 0)
    {
        char *arg = strtok(NULL, "");
        if (arg && strcmp(arg, "connection") == 0)
        {
            printf("[CLIENT] A enviar pedidos de desconexão...\n");
            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->sl_sensor_id, NULL);
            send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->ss_sensor_id, NULL);
            send(state->ss_fd, state->send_buffer, strlen(state->send_buffer), 0);
        }
        else
        {
            printf("[CLIENT] Uso: 'close connection'\n");
        }
    }
    else if (strcmp(command, "check") == 0)
    {
        char *arg = strtok(NULL, "");
        if (arg && strcmp(arg, "failure") == 0)
        {
            printf("[CLIENT] A enviar REQ_SENSSTATUS para o SS...\n");
            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_SENSSTATUS, state->ss_sensor_id, NULL);
            send(state->ss_fd, state->send_buffer, strlen(state->send_buffer), 0);
        }
        else
        {
            printf("[CLIENT] Uso: 'check failure'\n");
        }
    }
    else if (strcmp(command, "locate") == 0)
    {
        char *target_id = strtok(NULL, " ");
        if (target_id)
        {
            printf("[CLIENT] A solicitar localização do sensor %s para o SL...\n", target_id);
            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_SENSLOC, target_id, NULL);
            send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
        }
        else
        {
            printf("[CLIENT] Uso: 'locate <SensID>'\n");
        }
    }
    else if (strcmp(command, "diagnose") == 0)
    {
        char *target_loc = strtok(NULL, " ");
        if (target_loc)
        {
            printf("[CLIENT] A solicitar diagnóstico da localização %s para o SL...\n", target_loc);
            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_LOCLIST, state->sl_sensor_id, target_loc);
            send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
        }
        else
        {
            printf("[CLIENT] Uso: 'diagnose <LocId>'\n");
        }
    }
    else
    {
        printf("[CLIENT] Comando desconhecido: '%s'\n", command);
    }
}

void process_server_message(int server_fd, SensorState *state)
{
    ssize_t bytes = recv(server_fd, state->recv_buffer, sizeof(state->recv_buffer) - 1, 0);
    if (bytes <= 0)
    {
        printf("\n[CLIENT] Servidor (fd: %d) desconectou. Encerrando.\n", server_fd);
        if (server_fd == state->sl_fd)
            state->sl_fd = -1;
        if (server_fd == state->ss_fd)
            state->ss_fd = -1;
        return;
    }
    state->recv_buffer[bytes] = '\0';
    printf("\n[CLIENT] Mensagem de (fd: %d): \"%s\"\n", server_fd, state->recv_buffer);

    int code;
    char p1[256], p2[256];
    parse_message(state->recv_buffer, &code, p1, p2);

    switch (code)
    {
    case RES_CONNSEN:
        if (server_fd == state->sl_fd)
        {
            strncpy(state->sl_sensor_id, p1, sizeof(state->sl_sensor_id) - 1);
            state->registered_on_sl = 1;
            printf(">> Registado no SL com ID: %s\n", state->sl_sensor_id);
        }
        else if (server_fd == state->ss_fd)
        {
            strncpy(state->ss_sensor_id, p1, sizeof(state->ss_sensor_id) - 1);
            state->registered_on_ss = 1;
            printf(">> Registado no SS com ID: %s\n", state->ss_sensor_id);
        }
        if (state->registered_on_sl && state->registered_on_ss)
        {
            printf("--------------------------------------------------\n");
            printf("[CLIENT] Registo completo em ambos os servidores!\n");
            printf("--------------------------------------------------\n");
        }
        break;
    case 41: // RES_LOCLIST && RES_SENSSTATUS
        if (strchr(p1, ','))
        {
            printf(">> Sensores na localização: %s\n", p1);
        }
        else
        {
            int loc_id = atoi(p1);
            printf(">> ALERTA DE FALHA! Localização: %d (%s)\n", loc_id, get_region_name(loc_id));
        }
        break;
    case RES_SENSLOC:
        printf(">> Localização do sensor: %s\n", p1);
        break;
    case MSG_ERROR:
        printf(">> ERRO recebido do servidor (código: %s)\n", p1);
        break;
    case MSG_OK:
        printf(">> OK recebido do servidor (código: %s)\n", p1);
        break;
    default:
        printf(">> Código de mensagem não tratado: %d\n", code);
        break;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        fprintf(stderr, "Uso: %s <server_ip> <sl_port> <ss_port> <location_id>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *server_ip = argv[1];
    int sl_port = atoi(argv[2]);
    int ss_port = atoi(argv[3]);
    int location_id = atoi(argv[4]);

    if (location_id < 1 || location_id > 10)
    {
        fprintf(stderr, "Argumento inválido: location_id deve estar entre 1 e 10.\n");
        exit(EXIT_FAILURE);
    }

    SensorState state = {0};

    printf("Cliente (sensor) a iniciar na localização %d...\n", location_id);

    state.sl_fd = connect_to_server(server_ip, sl_port);
    if (state.sl_fd == SOCKET_ERROR)
        error_exit("Falha ao conectar ao SL");
    printf("Conectado ao SL (fd: %d).\n", state.sl_fd);

    state.ss_fd = connect_to_server(server_ip, ss_port);
    if (state.ss_fd == SOCKET_ERROR)
    {
        close(state.sl_fd);
        error_exit("Falha ao conectar ao SS");
    }
    printf("Conectado ao SS (fd: %d).\n", state.ss_fd);

    char location_id_str[4];
    snprintf(location_id_str, sizeof(location_id_str), "%d", location_id);
    build_message(state.send_buffer, sizeof(state.send_buffer), REQ_CONNSEN, location_id_str, NULL);

    printf("[CLIENT->SL] Sending REQ_CONNSEN: \"%s\"\n", state.send_buffer);
    if (send(state.sl_fd, state.send_buffer, strlen(state.send_buffer), 0) < 0)
        error_exit("send to SL");

    printf("[CLIENT->SS] Sending REQ_CONNSEN: \"%s\"\n", state.send_buffer);
    if (send(state.ss_fd, state.send_buffer, strlen(state.send_buffer), 0) < 0)
        error_exit("send to SS");

    fd_set master_set, read_fds;
    int fd_max = (state.sl_fd > state.ss_fd) ? state.sl_fd : state.ss_fd;

    FD_ZERO(&master_set);
    FD_SET(state.sl_fd, &master_set);
    FD_SET(state.ss_fd, &master_set);
    FD_SET(STDIN_FILENO, &master_set);

    while (state.sl_fd != -1 || state.ss_fd != -1)
    {
        read_fds = master_set;
        if (state.registered_on_sl && state.registered_on_ss)
        {
            printf("\n> ");
            fflush(stdout);
        }

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) < 0)
            error_exit("select");

        for (int i = 0; i <= fd_max; i++)
        {
            if (FD_ISSET(i, &read_fds))
            {
                if (i == STDIN_FILENO)
                {
                    if (state.registered_on_sl && state.registered_on_ss)
                    {
                        process_keyboard_input(&state);
                    }
                    else
                    {
                        char temp_buf[1024];
                        fgets(temp_buf, sizeof(temp_buf), stdin);
                        printf("[CLIENT] Por favor, aguarde o registo ser completado.\n");
                    }
                }
                else if (i == state.sl_fd || i == state.ss_fd)
                {
                    process_server_message(i, &state);
                }
            }
        }
    }

    printf("\n[CLIENT] Encerrando cliente...\n");
    return 0;
}
