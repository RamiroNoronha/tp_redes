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

/**
 * @brief Cria, configura (SO_REUSEADDR), vincula (bind) e coloca um socket em modo de escuta.
 *
 * @param port A porta na qual o socket deve escutar.
 * @param backlog O número máximo de conexões pendentes para listen().
 * @return O descritor do arquivo do socket de escuta em caso de sucesso,
 * ou SOCKET_ERROR (-1) em caso de falha (com perror já chamado).
 */
int create_and_configure_listening_socket(int port, int backlog)
{
    int new_listen_fd;
    struct sockaddr_in addr;
    int optval = 1;

    // 1. Criar o socket
    if ((new_listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR)
    {
        perror("Erro ao criar socket de escuta na função");
        return SOCKET_ERROR;
    }

    // 2. Configurar SO_REUSEADDR
    if (setsockopt(new_listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == SOCKET_ERROR)
    {
        perror("Erro ao configurar SO_REUSEADDR na função");
        close(new_listen_fd);
        return SOCKET_ERROR;
    }

    // 3. Preparar a estrutura sockaddr_in
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    // 4. Vincular (bind) o socket à porta
    if (bind(new_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        perror("Erro ao fazer bind na porta na função");
        close(new_listen_fd);
        return SOCKET_ERROR;
    }

    // 5. Colocar o socket em modo de escuta
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
 * @brief Tenta estabelecer uma conexão P2P, conectando-se ativamente ou configurando um socket de escuta.
 *
 * @param peer_target_ip O IP do servidor peer para tentar a conexão ativa.
 * @param common_p2p_port A porta P2P comum para conexão e escuta.
 * @param p2p_fd_ptr Ponteiro para a variável que armazenará o FD da conexão P2P estabelecida.
 * @param p2p_listen_fd_ptr Ponteiro para a variável que armazenará o FD do socket de escuta P2P.
 * @param master_set_ptr Ponteiro para o conjunto mestre de FDs do select.
 * @param fd_max_ptr Ponteiro para o maior FD no conjunto mestre.
 */
void initialize_p2p_link(const char *peer_target_ip, int common_p2p_port,
                         int *p2p_fd_ptr, int *p2p_listen_fd_ptr,
                         fd_set *master_set_ptr, int *fd_max_ptr)
{
    printf("\n--- Configurando/Reiniciando Conexão P2P ---\n");
    printf("Tentando conectar ao peer %s na porta %d...\n", peer_target_ip, common_p2p_port);

    // Garante que os FDs P2P anteriores (se houver) estão limpos antes de tentar uma nova configuração.
    // Isso é importante se esta função for chamada para restabelecer uma conexão.
    if (*p2p_fd_ptr != -1)
    {
        printf("Limpando p2p_fd existente: %d\n", *p2p_fd_ptr);
        FD_CLR(*p2p_fd_ptr, master_set_ptr);
        close(*p2p_fd_ptr);
        *p2p_fd_ptr = -1;
    }
    if (*p2p_listen_fd_ptr != -1)
    {
        printf("Limpando p2p_listen_fd existente: %d\n", *p2p_listen_fd_ptr);
        FD_CLR(*p2p_listen_fd_ptr, master_set_ptr);
        close(*p2p_listen_fd_ptr);
        *p2p_listen_fd_ptr = -1;
    }

    int temp_p2p_socket;
    if ((temp_p2p_socket = socket(AF_INET, SOCK_STREAM, 0)) == SOCKET_ERROR)
    {
        // Não chamamos error_exit aqui para permitir que o servidor continue para clientes
        perror("Erro ao criar socket P2P para tentativa de conexão ativa");
        // Tentar escutar P2P como fallback pode ser uma opção aqui, mas a lógica original
        // tentava conectar primeiro, depois escutar. Se o socket falha, é um problema.
        // Por ora, vamos manter o fluxo original e focar na falha do connect.
        // Poderíamos decidir que, se o socket falhar, não há P2P por enquanto.
        return; // Sai da função se não conseguir nem criar o socket de tentativa
    }

    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(common_p2p_port);

    if (inet_pton(AF_INET, peer_target_ip, &peer_addr.sin_addr) <= 0)
    {
        perror("Erro ao converter endereço IP do peer para P2P");
        close(temp_p2p_socket);
        return; // Sai se o IP for inválido
    }

    if (connect(temp_p2p_socket, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) == 0)
    { // Sucesso é 0
        // Conexão P2P ativa bem-sucedida
        *p2p_fd_ptr = temp_p2p_socket;
        FD_SET(*p2p_fd_ptr, master_set_ptr);
        if (*p2p_fd_ptr > *fd_max_ptr)
        {
            *fd_max_ptr = *p2p_fd_ptr;
        }
        printf("Conectado com sucesso ao servidor peer (fd: %d).\n", *p2p_fd_ptr);
    }
    else
    {
        // Conexão P2P ativa falhou
        close(temp_p2p_socket); // Fecha o socket que falhou ao conectar
        perror("Falha ao conectar ativamente ao peer (ou peer não disponível)");

        printf("Iniciando escuta P2P passiva na porta %d...\n", common_p2p_port);
        int p2p_listen_backlog = 1; // P2P espera apenas 1 conexão
        *p2p_listen_fd_ptr = create_and_configure_listening_socket(common_p2p_port, p2p_listen_backlog);

        if (*p2p_listen_fd_ptr == SOCKET_ERROR)
        {
            fprintf(stderr, "Falha crítica ao tentar configurar escuta P2P passiva. P2P não estará disponível.\n");
            // Não chama error_exit para permitir que o servidor continue para clientes
        }
        else
        {
            FD_SET(*p2p_listen_fd_ptr, master_set_ptr);
            if (*p2p_listen_fd_ptr > *fd_max_ptr)
            {
                *fd_max_ptr = *p2p_listen_fd_ptr;
            }
            // A mensagem de sucesso da escuta já é impressa por create_and_configure_listening_socket
        }
    }
    printf("--- Configuração/Reinicialização P2P Concluída ---\n");
}

/**
 * @brief Aceita uma nova conexão de cliente e a adiciona ao conjunto mestre.
 *
 * @param main_listen_fd O socket de escuta principal para conexões de clientes.
 * @param master_set_ptr Ponteiro para o conjunto mestre de FDs.
 * @param fd_max_ptr Ponteiro para o maior FD no conjunto mestre.
 */
void handle_new_client_connection(int main_listen_fd, fd_set *master_set_ptr, int *fd_max_ptr)
{
    struct sockaddr_in new_client_addr;
    socklen_t new_client_addr_len = sizeof(new_client_addr);
    int new_client_fd;

    new_client_fd = accept(main_listen_fd, (struct sockaddr *)&new_client_addr, &new_client_addr_len);
    if (new_client_fd == SOCKET_ERROR)
    {
        perror("Erro ao aceitar nova conexão de cliente");
        return; // Retorna sem adicionar nada se o accept falhar
    }

    FD_SET(new_client_fd, master_set_ptr);
    if (new_client_fd > *fd_max_ptr)
    {
        *fd_max_ptr = new_client_fd;
    }

    char client_ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &new_client_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL)
    {
        perror("Erro ao converter endereço IP do novo cliente");
        // Mesmo com erro no inet_ntop, o socket foi aceito e adicionado.
        // Poderíamos fechar e remover se o log do IP for crítico.
        // Por ora, apenas logamos o erro.
        printf("Novo cliente conectado (fd: %d), erro ao obter IP.\n", new_client_fd);
    }
    else
    {
        printf("Novo cliente conectado: %s:%d (fd: %d)\n",
               client_ip, ntohs(new_client_addr.sin_port), new_client_fd);
    }
}

/**
 * @brief Aceita uma conexão P2P entrante, configura o socket de comunicação P2P
 * e desativa o socket de escuta P2P.
 *
 * @param current_p2p_listen_fd O socket de escuta P2P que teve atividade.
 * @param p2p_comm_fd_main_ptr Ponteiro para a variável p2p_fd da main (para armazenar o novo FD de comunicação).
 * @param p2p_listen_fd_main_ptr Ponteiro para a variável p2p_listen_fd da main (para ser resetada para -1).
 * @param master_set_ptr Ponteiro para o conjunto mestre de FDs.
 * @param fd_max_ptr Ponteiro para o maior FD no conjunto mestre.
 */
void handle_incoming_p2p_connection(int current_p2p_listen_fd, int *p2p_comm_fd_main_ptr,
                                    int *p2p_listen_fd_main_ptr,
                                    fd_set *master_set_ptr, int *fd_max_ptr)
{
    printf("Detectada tentativa de conexão no socket de escuta P2P (fd: %d).\n", current_p2p_listen_fd);
    struct sockaddr_in incoming_peer_addr;
    socklen_t incoming_peer_addr_len = sizeof(incoming_peer_addr);
    int accepted_p2p_comm_fd;

    accepted_p2p_comm_fd = accept(current_p2p_listen_fd, (struct sockaddr *)&incoming_peer_addr, &incoming_peer_addr_len);
    if (accepted_p2p_comm_fd == SOCKET_ERROR)
    {
        perror("Erro ao aceitar conexão P2P entrante");
        return;
    }

    char peer_ip[INET_ADDRSTRLEN];
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

    if (*p2p_comm_fd_main_ptr != -1)
    { // Verifica se já existe uma conexão P2P principal
        printf("AVISO: Conexão P2P principal já existente (fd: %d). Fechando nova tentativa de conexão P2P (fd: %d).\n",
               *p2p_comm_fd_main_ptr, accepted_p2p_comm_fd);
        close(accepted_p2p_comm_fd);
    }
    else
    {
        // Define o novo socket de comunicação P2P
        *p2p_comm_fd_main_ptr = accepted_p2p_comm_fd;
        FD_SET(*p2p_comm_fd_main_ptr, master_set_ptr);
        if (*p2p_comm_fd_main_ptr > *fd_max_ptr)
        {
            *fd_max_ptr = *p2p_comm_fd_main_ptr;
        }
        printf("Socket de comunicação P2P estabelecido através de escuta (fd: %d).\n", *p2p_comm_fd_main_ptr);

        // Como a conexão P2P foi estabelecida, paramos de escutar por novas conexões P2P.
        printf("Fechando socket de escuta P2P (original fd: %d) e removendo do master_set.\n", current_p2p_listen_fd);
        FD_CLR(current_p2p_listen_fd, master_set_ptr);
        close(current_p2p_listen_fd);
        *p2p_listen_fd_main_ptr = -1; // Indica que não estamos mais escutando P2P
    }
}

/**
 * @brief Processa dados recebidos ou desconexão em um socket P2P de comunicação estabelecido.
 *
 * @param current_p2p_comm_fd O socket P2P de comunicação que teve atividade.
 * @param p2p_comm_fd_main_ptr Ponteiro para a variável p2p_fd da main (para ser resetada para -1 em caso de desconexão).
 * @param master_set_ptr Ponteiro para o conjunto mestre de FDs.
 * // Não precisamos mais de p2p_listen_fd_main_ptr ou dos args de porta aqui,
 * // pois a lógica de restabelecimento foi movida para o topo do while(1) e usa initialize_p2p_link.
 */
void handle_p2p_communication(int current_p2p_comm_fd, int *p2p_comm_fd_main_ptr,
                              fd_set *master_set_ptr)
{
    char p2p_buffer[MAX_MSG_SIZE];
    memset(p2p_buffer, 0, MAX_MSG_SIZE);
    ssize_t p2p_bytes_received;

    p2p_bytes_received = recv(current_p2p_comm_fd, p2p_buffer, MAX_MSG_SIZE - 1, 0);

    if (p2p_bytes_received <= 0)
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
        *p2p_comm_fd_main_ptr = -1; // Indica que a conexão P2P ativa foi perdida

        printf("Conexão P2P (original fd: %d) terminada. Tentativa de restabelecimento ocorrerá no próximo ciclo.\n", current_p2p_comm_fd);
        // A lógica no início do while(1) (if p2p_fd == -1 && p2p_listen_fd == -1)
        // cuidará de chamar initialize_p2p_link() para tentar restabelecer.
    }
    else
    {
        p2p_buffer[p2p_bytes_received] = '\0';
        printf("Recebido do peer (fd %d): '%s'\n", current_p2p_comm_fd, p2p_buffer);
        // Futuramente: Lógica para tratar mensagens de protocolo P2P (REQ_CONNPEER, etc.)
    }
}

/**
 * @brief Processa dados recebidos ou desconexão em um socket de cliente estabelecido.
 *
 * @param client_socket_fd O socket do cliente que teve atividade.
 * @param master_set_ptr Ponteiro para o conjunto mestre de FDs.
 * // fd_max_ptr poderia ser passado se quiséssemos recalcular fd_max ao fechar, mas é opcional.
 */
void handle_client_communication(int client_socket_fd, fd_set *master_set_ptr)
{
    char buffer[MAX_MSG_SIZE];
    memset(buffer, 0, MAX_MSG_SIZE);
    ssize_t bytes_received;

    bytes_received = recv(client_socket_fd, buffer, MAX_MSG_SIZE - 1, 0);

    if (bytes_received <= 0)
    {
        if (bytes_received == 0)
        {
            printf("Socket %d (cliente) desconectou.\n", client_socket_fd);
        }
        else
        {
            perror("Erro no recv() do cliente");
        }
        close(client_socket_fd);
        FD_CLR(client_socket_fd, master_set_ptr);
        // Opcional: Se client_socket_fd == *fd_max_ptr, você precisaria varrer master_set_ptr
        // para encontrar o novo fd_max. Por simplicidade, podemos omitir isso por enquanto,
        // já que fd_max só cresce e select() lida bem com isso (embora menos eficiente).
    }
    else
    {
        buffer[bytes_received] = '\0';
        printf("Recebido do cliente (fd %d): %s\n", client_socket_fd, buffer);

        // Lógica de resposta simples (eco ou mensagem de confirmação)
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
    int optval = 1;         // optval para setsockopt, usado em create_and_configure_listening_socket e P2P connect setup

    printf("Servidor iniciando...\n");

    // Configurar socket de escuta para clientes
    int listen_fd;
    int client_backlog = 10; // Aumentado um pouco o backlog para clientes
    printf("Configurando socket de escuta para clientes na porta %d...\n", client_listen_port);
    listen_fd = create_and_configure_listening_socket(client_listen_port, client_backlog);
    if (listen_fd == SOCKET_ERROR)
    {
        error_exit("Falha crítica ao configurar socket de escuta para clientes");
    }

    // Variables for Select
    fd_set master_set, read_fds;
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

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == SOCKET_ERROR)
        {
            error_exit("Erro crítico no select"); // Pode ser interrompido por sinal, tratar EINTR em produção
        }
        printf("Atividade detectada!\n");

        for (int i = 0; i <= fd_max; i++)
        {
            if (FD_ISSET(i, &read_fds)) // Somente processa se 'i' tem atividade
            {
                if (i == listen_fd)
                {
                    handle_new_client_connection(listen_fd, &master_set, &fd_max);
                }
                else if (p2p_listen_fd != -1 && i == p2p_listen_fd)
                {
                    handle_incoming_p2p_connection(i, &p2p_fd, &p2p_listen_fd, &master_set, &fd_max);
                }
                else if (p2p_fd != -1 && i == p2p_fd)
                {
                    handle_p2p_communication(i, &p2p_fd, &master_set);
                }
                else
                {
                    handle_client_communication(i, &master_set);
                }
            }
        }
    }

    return 0;
}
