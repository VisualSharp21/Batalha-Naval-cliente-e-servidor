
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define PORT        8080
#define BOARD_SIZE  10

/* Células do tabuleiro */
#define AGUA      '.'
#define NAVIO     'N'
#define ATINGIDO  'X'
#define ERROU     'O'

typedef struct {
    int  tamanho;
    char nome[32];
    int  celulas_restantes;
} Navio;

typedef struct {
    char  board[BOARD_SIZE][BOARD_SIZE];
    Navio navios[5];
    int   num_navios;
    int   navios_afundados;
    int   pronto;           /* 1 = tabuleiro já foi recebido */
} Jogador;

/* Envia string para um fd */
void enviar(int fd, const char *msg) {
    send(fd, msg, strlen(msg), 0);
    printf("[SERVIDOR] -> fd=%d: %s", fd, msg);
}

/* Inicializa tabuleiro vazio */
void init_jogador(Jogador *j) {
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            j->board[r][c] = AGUA;
    j->num_navios     = 0;
    j->navios_afundados = 0;
    j->pronto         = 0;
}

/*
 * Recebe o tabuleiro do cliente no formato:
 * "BOARD <letra_linha><col>,<letra_linha><col>,... NOME tamanho\n"
 * Exemplo: "BOARD A0,A1,A2 DESTROYER 3\n"
 * O cliente envia um "BOARD ..." por navio.
 */
int receber_navio(Jogador *j, const char *msg) {
    /* Formato: BOARD A0,B0,C0 DESTROYER 3 */
    char copia[512];
    strncpy(copia, msg, sizeof(copia)-1);

    char *tok = strtok(copia, " "); /* "BOARD" */
    if (!tok || strcmp(tok, "BOARD") != 0) return -1;

    char *coords = strtok(NULL, " "); /* "A0,A1,A2" */
    char *nome   = strtok(NULL, " "); /* "DESTROYER" */
    char *tam_s  = strtok(NULL, " \n"); /* "3" */
    if (!coords || !nome || !tam_s) return -1;

    int tam = atoi(tam_s);
    if (j->num_navios >= 5) return -1;

    Navio *nav = &j->navios[j->num_navios];
    strncpy(nav->nome, nome, sizeof(nav->nome)-1);
    nav->tamanho = tam;
    nav->celulas_restantes = tam;

    /* Posiciona cada célula no tabuleiro */
    char *cell = strtok(coords, ",");
    while (cell) {
        if (strlen(cell) < 2) return -1;
        int row = cell[0] - 'A';
        int col = atoi(cell + 1);
        if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE)
            return -1;
        if (j->board[row][col] != AGUA) return -1; /* colisão */
        j->board[row][col] = NAVIO;
        cell = strtok(NULL, ",");
    }

    j->num_navios++;
    return 0;
}

/*
 * Processa tiro. Retorna:
 *  0 = miss
 *  1 = hit
 *  2 = sunk (afundou um navio)
 *  3 = win  (afundou todos)
 * -1 = inválido
 */
int processar_tiro(Jogador *alvo, int row, int col, char *nome_afundado) {
    if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE)
        return -1;
    if (alvo->board[row][col] == ATINGIDO || alvo->board[row][col] == ERROU)
        return -1;

    if (alvo->board[row][col] == AGUA) {
        alvo->board[row][col] = ERROU;
        return 0;
    }

    /* Acertou navio */
    alvo->board[row][col] = ATINGIDO;

    /* Verifica qual navio foi atingido e se afundou */
    for (int i = 0; i < alvo->num_navios; i++) {

        /* Conta células do navio ainda intactas para ver se afundou */
        /* (solução simples: decrementamos ao primeiro hit que fecha o navio) */
        /* Verificamos se este hit fechou algum navio contando NAVIO restante */
    }

    /* Abordagem direta: verifica se algum navio ainda tem células intactas
       contando quantos 'N' existem no tabuleiro após o hit */
    int navios_vivos = 0;
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            if (alvo->board[r][c] == NAVIO) navios_vivos++;

    /* Calcula total de células de navio originais */
    int total_celulas = 0;
    for (int i = 0; i < alvo->num_navios; i++)
        total_celulas += alvo->navios[i].tamanho;

    /* Conta atingidos */
    int atingidos = 0;
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            if (alvo->board[r][c] == ATINGIDO) atingidos++;

    if (navios_vivos == 0) {
        /* Todos afundados */
        strcpy(nome_afundado, "todos");
        return 3;
    }

    /* Decrementa celulas_restantes do primeiro navio ainda vivo */
    for (int i = 0; i < alvo->num_navios; i++) {
        Navio *nav = &alvo->navios[i];
        if (nav->celulas_restantes > 0) {
            nav->celulas_restantes--;
            if (nav->celulas_restantes == 0) {
                strncpy(nome_afundado, nav->nome, 31);
                alvo->navios_afundados++;
                return 2; /* sunk */
            }
            break;
        }
    }

    return 1; /* hit simples */
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind"); return 1;
    }
    listen(listen_fd, 2);

    printf("[SERVIDOR] Porta %d. Aguardando 2 jogadores...\n\n", PORT);

    int p_fd[2];
    for (int i = 0; i < 2; i++) {
        printf("[SERVIDOR] Aguardando jogador %d...\n", i+1);
        p_fd[i] = accept(listen_fd, NULL, NULL);
        printf("[SERVIDOR] Jogador %d conectado! fd=%d\n", i+1, p_fd[i]);

        /* Informa ao cliente qual número ele é */
        char msg[32];
        snprintf(msg, sizeof(msg), "PLAYER %d\n", i+1);
        enviar(p_fd[i], msg);
    }

    /* Inicializa estado dos jogadores */
    Jogador jogadores[2];
    init_jogador(&jogadores[0]);
    init_jogador(&jogadores[1]);

    /* Pede tabuleiros */
    enviar(p_fd[0], "SEND_BOARD\n");
    enviar(p_fd[1], "SEND_BOARD\n");

    printf("\n[SERVIDOR] Aguardando tabuleiros dos jogadores...\n");

    /*  select() para receber tabuleiros dos 2 jogadores */
    fd_set master;
    FD_ZERO(&master);
    FD_SET(p_fd[0], &master);
    FD_SET(p_fd[1], &master);
    int max_fd = (p_fd[0] > p_fd[1]) ? p_fd[0] : p_fd[1];

    char buf[1024];

    /* Recebe navios até ambos estarem prontos */
    /* Cada jogador envia 5 mensagens BOARD (uma por navio), depois "BOARD_DONE" */
    while (!jogadores[0].pronto || !jogadores[1].pronto) {
        fd_set tmp = master;
        select(max_fd+1, &tmp, NULL, NULL, NULL);

        for (int i = 0; i < 2; i++) {
            if (!FD_ISSET(p_fd[i], &tmp)) continue;
            if (jogadores[i].pronto) continue;

            int bytes = recv(p_fd[i], buf, sizeof(buf)-1, 0);
            if (bytes <= 0) { printf("Jogador %d desconectou.\n", i+1); return 1; }
            buf[bytes] = '\0';
            buf[strcspn(buf, "\n")] = '\0';
            printf("[SERVIDOR] <- fd=%d (J%d): %s\n", p_fd[i], i+1, buf);

            if (strncmp(buf, "BOARD ", 6) == 0) {
                if (receber_navio(&jogadores[i], buf) == 0) {
                    enviar(p_fd[i], "OK\n");
                } else {
                    enviar(p_fd[i], "INVALID\n");
                }
            } else if (strcmp(buf, "BOARD_DONE") == 0) {
                jogadores[i].pronto = 1;
                printf("[SERVIDOR] Tabuleiro do Jogador %d recebido (%d navios).\n",
                       i+1, jogadores[i].num_navios);
            }
        }
    }

    /* Jogo começa */
    enviar(p_fd[0], "READY\n");
    enviar(p_fd[1], "READY\n");

    int vez = 0;
    printf("\n[SERVIDOR] JOGO INICIADO! Vez do Jogador 1.\n\n");
    enviar(p_fd[0], "YOUR_TURN\n");
    enviar(p_fd[1], "WAIT\n");

    int jogo_ativo = 1;
    while (jogo_ativo) {
        fd_set tmp = master;
        select(max_fd+1, &tmp, NULL, NULL, NULL);

        for (int i = 0; i < 2; i++) {
            if (!FD_ISSET(p_fd[i], &tmp)) continue;

            int bytes = recv(p_fd[i], buf, sizeof(buf)-1, 0);
            if (bytes <= 0) {
                printf("[SERVIDOR] Jogador %d desconectou.\n", i+1);
                jogo_ativo = 0; break;
            }
            buf[bytes] = '\0';
            buf[strcspn(buf, "\n")] = '\0';
            printf("[SERVIDOR] <- fd=%d (J%d): %s\n", p_fd[i], i+1, buf);

            /* Ignora se não é a vez */
            if (i != vez) { enviar(p_fd[i], "WAIT\n"); continue; }

            /* Processa TIRO A3 */
            if (strncmp(buf, "TIRO ", 5) == 0 && strlen(buf) >= 6) {
                char coord[8];
                strncpy(coord, buf+5, sizeof(coord)-1);

                if (strlen(coord) < 2) { enviar(p_fd[i], "INVALID\n"); continue; }

                int row = coord[0] - 'A';
                int col = atoi(coord+1);

                int alvo = 1 - vez;
                char nome_afundado[32] = "";
                int resultado = processar_tiro(&jogadores[alvo], row, col, nome_afundado);

                if (resultado == -1) {
                    enviar(p_fd[i], "INVALID\n");
                    enviar(p_fd[i], "YOUR_TURN\n");
                }
                else if (resultado == 3) {
                    /* Vitória pae*/
                    enviar(p_fd[vez],  "WIN\n");
                    enviar(p_fd[alvo], "LOSE\n");
                    printf("[SERVIDOR] Jogador %d VENCEU!\n", vez+1);
                    jogo_ativo = 0;
                }
                else if (resultado == 2) {
                    /* Afundou navio — jogador joga de novo */
                    char msg[64];
                    snprintf(msg, sizeof(msg), "SUNK %s\n", nome_afundado);
                    enviar(p_fd[vez], msg);
                    enviar(p_fd[alvo], "ENEMY_SUNK\n");
                    enviar(p_fd[vez], "YOUR_TURN\n"); /* continua jogando */
                }
                else if (resultado == 1) {
                    /* Hit — jogador joga de novo */
                    enviar(p_fd[vez], "HIT\n");
                    enviar(p_fd[alvo], "ENEMY_HIT\n");
                    enviar(p_fd[vez], "YOUR_TURN\n"); /* continua jogando */
                }
                else {
                    /* Miss — passa a vez */
                    enviar(p_fd[vez], "MISS\n");
                    vez = 1 - vez;
                    enviar(p_fd[vez],   "YOUR_TURN\n");
                    enviar(p_fd[1-vez], "WAIT\n");
                    printf("[SERVIDOR] Vez do Jogador %d.\n", vez+1);
                }
            } else {
                enviar(p_fd[i], "INVALID\n");
            }
        }
    }

    close(p_fd[0]);
    close(p_fd[1]);
    close(listen_fd);
    printf("[SERVIDOR] Jogo encerrado.\n");
    return 0;
}
