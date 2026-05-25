
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT       8080
#define BOARD_SIZE 10

/* Células do tabuleiro visual */
#define AGUA     '.'
#define NAVIO    'N'
#define ATINGIDO 'X'
#define ERROU    'O'

/* Tabuleiros do cliente */
char meu_board[BOARD_SIZE][BOARD_SIZE];    /* meu tabuleiro (com meus navios) */
char ataque[BOARD_SIZE][BOARD_SIZE];       /* onde já atirei no oponente */

/* Navios disponíveis */
typedef struct {
    char nome[32];
    int  tamanho;
} NavioTipo;

NavioTipo frota[] = {
    {"PORTA-AVIOES", 5},
    {"NAVIO-GUERRA", 4},
    {"DESTROYER",    3},
    {"SUBMARINO",    2},
    {"PATRULHA",     1},
};
#define NUM_NAVIOS 5

/* ─── Funções de exibição ─── */

void limpar_tela() {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[2J\033[H");
#endif
}

void cor_celula(char c) {
    switch (c) {
        case NAVIO:    printf("\033[0;34m[N]\033[0m"); break; /* azul */
        case ATINGIDO: printf("\033[0;31m[X]\033[0m"); break; /* vermelho */
        case ERROU:    printf("\033[0;33m[O]\033[0m"); break; /* amarelo */
        default:       printf("[ ]");                  break; /* cinza */
    }
}

void imprimir_tabuleiros() {
    printf("\n");
    printf("         ===== BATALHA NAVAL =====\n\n");
    printf("        VOCE                     ADVERSARIO\n");
    printf("   0  1  2  3  4  5  6  7  8  9    ");
    printf("   0  1  2  3  4  5  6  7  8  9\n");

    for (int r = 0; r < BOARD_SIZE; r++) {
        char letra = 'A' + r;
        /* Meu tabuleiro */
        printf("%c  ", letra);
        for (int c = 0; c < BOARD_SIZE; c++) cor_celula(meu_board[r][c]);
        printf("    ");
        /* Tabuleiro de ataque */
        printf("%c  ", letra);
        for (int c = 0; c < BOARD_SIZE; c++) cor_celula(ataque[r][c]);
        printf("\n");
    }
    printf("\n");
    printf("  [N]=navio  [X]=acertou  [O]=errou  [ ]=agua\n\n");
}

/* ─── Inicialização ─── */

void init_tabuleiros() {
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++) {
            meu_board[r][c] = AGUA;
            ataque[r][c]    = AGUA;
        }
}

/* Converte coordenada "A3" para row=0, col=3. Retorna 0 se ok, -1 se inválido. */
int parse_coord(const char *s, int *row, int *col) {
    if (!s || strlen(s) < 2) return -1;
    char letra = toupper(s[0]);
    if (letra < 'A' || letra > 'J') return -1;
    *row = letra - 'A';
    *col = atoi(s+1);
    if (*col < 0 || *col >= BOARD_SIZE) return -1;
    return 0;
}

/*  Posicionamento de navios  */

int posicionar_navio(const NavioTipo *nav, int fd) {
    char buf[64];
    int row, col, horizontal;

    while (1) {
        printf("  Posicione o %s (%d unidade(s))\n", nav->nome, nav->tamanho);
        printf("  Coordenada inicial (ex: A3): ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin)) return -1;
        buf[strcspn(buf, "\n")] = '\0';
        for (int i = 0; buf[i]; i++) buf[i] = toupper(buf[i]);

        if (parse_coord(buf, &row, &col) == -1) {
            printf("  [!] Coordenada inválida. Use letra A-J e número 0-9.\n\n");
            continue;
        }

        if (nav->tamanho > 1) {
            printf("  Direção? (H=horizontal / V=vertical): ");
            fflush(stdout);
            char dir[8];
            if (!fgets(dir, sizeof(dir), stdin)) return -1;
            horizontal = (toupper(dir[0]) == 'H');
        } else {
            horizontal = 1; /* tamanho 1: direção não importa */
        }

        /* Verifica se cabe e não colide */
        int valido = 1;
        for (int i = 0; i < nav->tamanho; i++) {
            int r = row + (horizontal ? 0 : i);
            int c = col + (horizontal ? i : 0);
            if (r >= BOARD_SIZE || c >= BOARD_SIZE || meu_board[r][c] != AGUA) {
                valido = 0; break;
            }
        }

        if (!valido) {
            printf("  [!] Posição inválida ou colide com outro navio. Tente novamente.\n\n");
            continue;
        }

        /* Monta a string de coordenadas para enviar ao servidor */
        /* Formato: "BOARD A0,A1,A2 DESTROYER 3" */
        char coords[256] = "";
        for (int i = 0; i < nav->tamanho; i++) {
            int r = row + (horizontal ? 0 : i);
            int c = col + (horizontal ? i : 0);
            meu_board[r][c] = NAVIO; /* marca no tabuleiro visual */

            char cel[8];
            snprintf(cel, sizeof(cel), "%c%d", 'A'+r, c);
            if (i > 0) strcat(coords, ",");
            strcat(coords, cel);
        }

        char msg[512];
        snprintf(msg, sizeof(msg), "BOARD %s %s %d\n", coords, nav->nome, nav->tamanho);
        send(fd, msg, strlen(msg), 0);

        /* Aguarda confirmação do servidor */
        char resp[32];
        int bytes = recv(fd, resp, sizeof(resp)-1, 0);
        if (bytes <= 0) return -1;
        resp[bytes] = '\0';
        resp[strcspn(resp, "\n")] = '\0';

        if (strcmp(resp, "OK") == 0) {
            printf("  [✓] %s posicionado!\n\n", nav->nome);
            imprimir_tabuleiros();
            return 0;
        } else {
            /* Remove do tabuleiro visual e tenta de novo */
            for (int i = 0; i < nav->tamanho; i++) {
                int r = row + (horizontal ? 0 : i);
                int c = col + (horizontal ? i : 0);
                meu_board[r][c] = AGUA;
            }
            printf("  [!] Servidor recusou. Tente outra posição.\n\n");
        }
    }
}

/* ─── Main ─── */

int main(int argc, char *argv[]) {
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";

    /* Camada de Transporte: socket TCP */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) { perror("socket"); return 1; }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port   = htons(PORT);
    if (inet_pton(AF_INET, ip, &srv.sin_addr) <= 0) {
        fprintf(stderr, "IP inválido: %s\n", ip); return 1;
    }

    printf("[CLIENTE] Conectando a %s:%d...\n", ip, PORT);
    if (connect(fd, (struct sockaddr*)&srv, sizeof(srv)) == -1) {
        perror("connect"); return 1;
    }

    init_tabuleiros();

    /*  Camada de Aplicação: loop de mensagens  */
    char buf[1024];
    int  minha_vez  = 0;
    int  num_jogador = 0;
    int  posicionando = 0; /* fase de posicionamento */

    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        if (minha_vez && !posicionando)
            FD_SET(STDIN_FILENO, &fds);

        if (select(fd+1, &fds, NULL, NULL, NULL) == -1) break;

        /* ── Mensagem do servidor ── */
        if (FD_ISSET(fd, &fds)) {
            int bytes = recv(fd, buf, sizeof(buf)-1, 0);
            if (bytes <= 0) { printf("\n[!] Servidor desconectou.\n"); break; }
            buf[bytes] = '\0';
            buf[strcspn(buf, "\n")] = '\0';

            /*  Protocolo: interpreta cada mensagem  */

            if (strncmp(buf, "PLAYER ", 7) == 0) {
                num_jogador = atoi(buf+7);
                printf("[CLIENTE] Você é o Jogador %d!\n", num_jogador);
            }
            else if (strcmp(buf, "SEND_BOARD") == 0) {
                /* Fase de posicionamento */
                posicionando = 1;
                limpar_tela();
                printf("=== POSICIONE SEUS NAVIOS (Jogador %d) ===\n\n", num_jogador);
                imprimir_tabuleiros();

                for (int i = 0; i < NUM_NAVIOS; i++) {
                    if (posicionar_navio(&frota[i], fd) == -1) {
                        printf("[!] Erro no posicionamento.\n");
                        return 1;
                    }
                }

                /* Sinaliza que terminou */
                send(fd, "BOARD_DONE\n", 11, 0);
                printf("[CLIENTE] Tabuleiro enviado! Aguardando o oponente...\n");
                posicionando = 0;
            }
            else if (strcmp(buf, "READY") == 0) {
                limpar_tela();
                imprimir_tabuleiros();
                printf("=== JOGO INICIADO! ===\n");
            }
            else if (strcmp(buf, "YOUR_TURN") == 0) {
                minha_vez = 1;
                printf("\n>>> SUA VEZ! Coordenada (ex: A3): ");
                fflush(stdout);
            }
            else if (strcmp(buf, "WAIT") == 0) {
                minha_vez = 0;
                printf("[...] Aguardando o oponente atirar...\n");
            }
            else if (strcmp(buf, "HIT") == 0) {
                printf("\033[0;31m[!!!] ACERTOU! Atire novamente.\033[0m\n");
            }
            else if (strcmp(buf, "MISS") == 0) {
                printf("\033[0;33m[ o ] Errou. Água. Vez do oponente.\033[0m\n");
            }
            else if (strncmp(buf, "SUNK ", 5) == 0) {
                printf("\033[0;31m[!!!] AFUNDOU o %s! Atire novamente.\033[0m\n", buf+5);
            }
            else if (strcmp(buf, "ENEMY_HIT") == 0) {
                printf("\033[0;31m[!!!] Oponente acertou um dos seus navios!\033[0m\n");
                imprimir_tabuleiros();
            }
            else if (strcmp(buf, "ENEMY_SUNK") == 0) {
                printf("\033[0;31m[!!!] Oponente afundou um dos seus navios!\033[0m\n");
                imprimir_tabuleiros();
            }
            else if (strcmp(buf, "INVALID") == 0) {
                printf("[!] Posição inválida ou já usada. Tente outra.\n>>> ");
                fflush(stdout);
            }
            else if (strcmp(buf, "WIN") == 0) {
                imprimir_tabuleiros();
                printf("\033[0;32m\n╔═══\n");
                printf("  VOCÊ GANHOU   \n");
                printf("╚═\033[0m\n\n");
                break;
            }
            else if (strcmp(buf, "LOSE") == 0) {
                imprimir_tabuleiros();
                printf("\033[0;31m\n╔\n");
                printf("   VOCÊ PERDEU    \n");
                printf("╚═\033[0m\n\n");
                break;
            }
        }

        /* ── Jogador digitou uma coordenada ── */
        if (minha_vez && !posicionando && FD_ISSET(STDIN_FILENO, &fds)) {
            char linha[32];
            if (!fgets(linha, sizeof(linha), stdin)) break;
            linha[strcspn(linha, "\n")] = '\0';
            for (int i = 0; linha[i]; i++) linha[i] = toupper(linha[i]);

            int row, col;
            if (parse_coord(linha, &row, &col) == 0) {
                if (ataque[row][col] != AGUA) {
                    printf("[!] Você já atirou aqui. Escolha outra posição.\n>>> ");
                    fflush(stdout);
                } else {
                    /* Marca provisoriamente e envia */
                    ataque[row][col] = '?';
                    char msg[32];
                    snprintf(msg, sizeof(msg), "TIRO %c%d\n", 'A'+row, col);
                    send(fd, msg, strlen(msg), 0);
                    minha_vez = 0;

                    /* Aguarda resposta do servidor para atualizar tabuleiro */
                    int bytes2 = recv(fd, buf, sizeof(buf)-1, 0);
                    if (bytes2 <= 0) break;
                    buf[bytes2] = '\0';
                    buf[strcspn(buf, "\n")] = '\0';

                    if (strcmp(buf, "HIT") == 0 || strncmp(buf, "SUNK", 4) == 0) {
                        ataque[row][col] = ATINGIDO;
                        if (strncmp(buf, "SUNK ", 5) == 0)
                            printf("\033[0;31m[!!!] AFUNDOU o %s! Atire novamente.\033[0m\n", buf+5);
                        else
                            printf("\033[0;31m[!!!] ACERTOU! Atire novamente.\033[0m\n");
                        imprimir_tabuleiros();

                        /* Recebe YOUR_TURN para continuar jogando */
                        recv(fd, buf, sizeof(buf)-1, 0);
                        buf[strcspn(buf, "\n")] = '\0';
                        if (strcmp(buf, "YOUR_TURN") == 0) {
                            minha_vez = 1;
                            printf(">>> SUA VEZ! Coordenada (ex: A3): ");
                            fflush(stdout);
                        } else if (strcmp(buf, "WIN") == 0) {
                            imprimir_tabuleiros();
                            printf("\033[0;32m\n╔══════════════════════╗\n║   VOCÊ GANHOU!!!     ║\n╚══════════════════════╝\033[0m\n\n");
                            goto fim;
                        }
                    } else if (strcmp(buf, "MISS") == 0) {
                        ataque[row][col] = ERROU;
                        printf("\033[0;33m[ o ] Errou. Água. Vez do oponente.\033[0m\n");
                        imprimir_tabuleiros();
                        /* Recebe WAIT */
                        recv(fd, buf, sizeof(buf)-1, 0);
                    } else if (strcmp(buf, "INVALID") == 0) {
                        ataque[row][col] = AGUA;
                        printf("[!] Posição inválida. Tente outra.\n>>> ");
                        fflush(stdout);
                        minha_vez = 1;
                    } else if (strcmp(buf, "WIN") == 0) {
                        imprimir_tabuleiros();
                        printf("\033[0;32m\n╔══════════════════════╗\n║   VOCÊ GANHOU!!!     ║\n╚══════════════════════╝\033[0m\n\n");
                        goto fim;
                    }
                }
            } else {
                printf("[!] Formato inválido. Use letra A-J e número 0-9 (ex: A3, H7)\n>>> ");
                fflush(stdout);
            }
        }
    }

fim:
    close(fd);
    return 0;
}
