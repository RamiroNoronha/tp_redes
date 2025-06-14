#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>
#include "common.h"

typedef struct
{
    int sl_fd;
    int ss_fd;

    char sensor_id[20];
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

/**
 * @brief Generates a unique 10-digit sensor ID, checking and updating an ID file.
 * @param id_buffer The buffer where the unique string ID will be stored.
 * @param buffer_size The size of id_buffer.
 */
void generate_unique_sensor_id(char *id_buffer, size_t buffer_size)
{
    FILE *fp;
    char line[256];
    int is_unique;
    long long new_id_num;
    const char *id_filename = "sensor_ids.txt";

    srand(time(NULL) ^ getpid());

    do
    {
        is_unique = 1;
        new_id_num = 1000000000LL + (rand() % 9000000000LL);
        snprintf(id_buffer, buffer_size, "%lld", new_id_num);

        fp = fopen(id_filename, "r");
        if (fp != NULL)
        {
            while (fgets(line, sizeof(line), fp))
            {
                line[strcspn(line, "\n")] = 0;
                if (strcmp(line, id_buffer) == 0)
                {
                    is_unique = 0;
                    break;
                }
            }
            fclose(fp);
        }
    } while (!is_unique);

    fp = fopen(id_filename, "a");
    if (fp == NULL)
    {
        perror("Não foi possível abrir sensor_ids.txt para escrita");
        return;
    }
    fprintf(fp, "%s\n", id_buffer);
    fclose(fp);

    printf("[ID_GEN] ID único gerado e guardado: %s\n", id_buffer);
}

/**
 * @brief Remove uma linha (um ID) do ficheiro sensor_ids.txt.
 * @param id_to_remove O ID de string a ser removido do ficheiro.
 */
void remove_id_from_file(const char *id_to_remove)
{
    const char *original_filename = "sensor_ids.txt";
    const char *temp_filename = "sensor_ids.tmp";
    FILE *fp_orig, *fp_temp;
    char line[256];

    fp_orig = fopen(original_filename, "r");
    fp_temp = fopen(temp_filename, "w");

    if (fp_orig == NULL || fp_temp == NULL)
    {
        if (fp_orig)
            fclose(fp_orig);
        if (fp_temp)
            fclose(fp_temp);
        return;
    }

    while (fgets(line, sizeof(line), fp_orig))
    {
        line[strcspn(line, "\n")] = 0;

        if (strcmp(line, id_to_remove) != 0)
        {
            fprintf(fp_temp, "%s\n", line);
        }
    }

    fclose(fp_orig);
    fclose(fp_temp);

    if (remove(original_filename) != 0)
    {
        perror("Erro ao apagar o ficheiro de IDs original");
        return;
    }
    if (rename(temp_filename, original_filename) != 0)
    {
        perror("Erro ao renomear o ficheiro temporário de IDs");
        return;
    }
}

const char *get_error_message(const char *error_code)
{
    if (strcmp(error_code, "01") == 0)
    {
        return "Peer limit exceeded";
    }
    else if (strcmp(error_code, "02") == 0)
    {
        return "Peer not found";
    }
    else if (strcmp(error_code, "09") == 0)
    {
        return "Sensor limit exceeded";
    }
    else if (strcmp(error_code, "10") == 0)
    {
        return "Sensor not found";
    }
    else if (strcmp(error_code, "11") == 0)
    {
        return "Location not found";
    }
    else
    {
        return "Unknown error";
    }
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

    char *rest = strtok(NULL, "");
    if (!rest)
        return PARSE_SUCCESS_ONE_PAYLOAD;

    while (*rest && (*rest == ' ' || *rest == '\t'))
        rest++;

    if (p2)
        strncpy(p2, rest, 255);

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
    return "Outside covered zone";
    ;
}

void process_keyboard_input(SensorState *state)
{
    char command_buffer[MAX_MSG_SIZE];
    if (fgets(command_buffer, sizeof(command_buffer), stdin) == NULL)
    {
        printf("\n[CLIENT] EOF DETECTED\n");
        build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->sensor_id, NULL);
        send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
        build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->sensor_id, NULL);
        send(state->ss_fd, state->send_buffer, strlen(state->send_buffer), 0);
        return;
    }
    command_buffer[strcspn(command_buffer, "\n")] = 0;

    char temp_copy[MAX_MSG_SIZE];
    strncpy(temp_copy, command_buffer, sizeof(temp_copy));
    char *command = strtok(temp_copy, " ");
    if (!command)
        return;

    if (strcmp(command, "kill") == 0)
    {

        printf("[CLIENT] kill\n");
        printf("[CLIENT] Sending REQ_DISCSEN %s\n", state->sensor_id);
        build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->sensor_id, NULL);
        send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
        build_message(state->send_buffer, sizeof(state->send_buffer), REQ_DISCSEN, state->sensor_id, NULL);
        send(state->ss_fd, state->send_buffer, strlen(state->send_buffer), 0);
        if (strlen(state->sensor_id) > 0)
        {
            remove_id_from_file(state->sensor_id);
        }
    }
    else if (strcmp(command, "check") == 0)
    {
        char *arg = strtok(NULL, "");
        if (arg && strcmp(arg, "failure") == 0)
        {
            printf("[CLIENT] check failure\n");
            printf("[CLIENT] Sending REQ_SENSSTATUS %s\n", state->sensor_id);
            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_SENSSTATUS, state->sensor_id, NULL);
            send(state->ss_fd, state->send_buffer, strlen(state->send_buffer), 0);
        }
    }
    else if (strcmp(command, "locate") == 0)
    {
        char *target_id = strtok(NULL, " ");
        if (target_id)
        {
            printf("[CLIENT] located %s\n", target_id);
            printf("[CLIENT] Sending REQ_SENSLOC %s\n", target_id);

            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_SENSLOC, target_id, NULL);
            send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
        }
    }
    else if (strcmp(command, "diagnose") == 0)
    {
        char *target_loc = strtok(NULL, " ");
        if (target_loc)
        {
            printf("[CLIENT] diagnose %s\n", target_loc);
            printf("[CLIENT] Sending REQ_LOCLIST %s\n", target_loc);
            build_message(state->send_buffer, sizeof(state->send_buffer), REQ_LOCLIST, state->sensor_id, target_loc);
            send(state->sl_fd, state->send_buffer, strlen(state->send_buffer), 0);
        }
    }
    else
    {
        printf("[CLIENT] Command not found: '%s'\n", command);
    }
}

void process_server_message(int server_fd, SensorState *state)
{
    ssize_t bytes = recv(server_fd, state->recv_buffer, sizeof(state->recv_buffer) - 1, 0);
    if (bytes <= 0)
    {
        if (server_fd == state->sl_fd)
            state->sl_fd = -1;
        if (server_fd == state->ss_fd)
            state->ss_fd = -1;
        return;
    }
    state->recv_buffer[bytes] = '\0';

    int code;
    char p1[256], p2[256];
    parse_message(state->recv_buffer, &code, p1, p2);

    switch (code)
    {
    case RES_CONNSEN:
        if (server_fd == state->sl_fd)
        {
            strncpy(state->sensor_id, p1, sizeof(state->sensor_id) - 1);
            state->registered_on_sl = 1;
            printf("[CLIENTE] SS New ID:: %s\n", state->sensor_id);
        }
        else if (server_fd == state->ss_fd)
        {
            strncpy(state->sensor_id, p1, sizeof(state->sensor_id) - 1);
            state->registered_on_ss = 1;
            printf("[CLIENTE] SL New ID:: %s\n", state->sensor_id);
        }
        break;
    case 41: // RES_LOCLIST && RES_SENSSTATUS
        if (p1[0] != '\0' && p2[0] != '\0')
        {
            printf("[CLIENT] Sensors at location %s:  %s\n", p1, p2);
        }
        else
        {
            int loc_id = atoi(p1);
            printf("[CLIENT] Alert received from area: %d (%s)\n", loc_id, get_region_name(loc_id));
        }
        break;
    case RES_SENSLOC:
        printf("[CLIENT] Current sensor %s location: %s\n", p1, p2);
        break;
    case MSG_ERROR:
        printf("[CLIENT] %s\n", get_error_message(p1));
        break;
    case MSG_OK:
        if (p1[0] == '1')
        {
            printf("[CLIENT] Successful disconnect%s\n", p2);
            if (strcmp(p2, "SS") == 0)
            {
                state->ss_fd = -1;
                state->registered_on_ss = 0;
            }
            else if (strcmp(p2, "SL") == 0)
            {
                state->sl_fd = -1;
                state->registered_on_sl = 0;
            }

            if (state->sl_fd == -1 && state->ss_fd == -1)
            {
                printf("[CLIENT] Both connections closed. Exiting...\n");
                exit(0);
            }
        }
        printf("[CLIENT] %s\n", p2);
        break;
    default:
        printf("[CLIENT] Code not founded: %d\n", code);
        break;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Uso: %s <server_ip> <sl_port> <ss_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *server_ip = argv[1];
    int sl_port = atoi(argv[2]);
    int ss_port = atoi(argv[3]);

    srand(time(NULL));

    int location_id = (rand() % 11) + 1;
    if (location_id == 11)
        location_id = -1;

    SensorState state = {0};

    generate_unique_sensor_id(state.sensor_id, sizeof(state.sensor_id));

    state.sl_fd = connect_to_server(server_ip, sl_port);
    if (state.sl_fd == SOCKET_ERROR)
        error_exit("Fail to connect to SL");

    state.ss_fd = connect_to_server(server_ip, ss_port);
    if (state.ss_fd == SOCKET_ERROR)
    {
        close(state.sl_fd);
        error_exit("Fail to connect to SS");
    }

    char location_id_str[4];
    snprintf(location_id_str, sizeof(location_id_str), "%d", location_id);
    build_message(state.send_buffer, sizeof(state.send_buffer), REQ_CONNSEN, state.sensor_id, location_id_str);

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
                        printf("[CLIENT] Wait the register.\n");
                    }
                }
                else if (i == state.sl_fd || i == state.ss_fd)
                {
                    process_server_message(i, &state);
                }
            }
        }
    }

    printf("\n[CLIENT] Finishing client...\n");
    return 0;
}
