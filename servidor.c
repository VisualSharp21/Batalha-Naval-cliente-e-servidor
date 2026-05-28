#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

/* Configurações da rede e do jogo */
#define PORT        8080
#define BOARD_SIZE  10

/* Representação visual e lógica do tabuleiro no servidor */
#define AGUA      '.'
#define NAVIO     'N'
#define ATINGIDO  'X'
#define ERROU     'O'

/* Estrutura que guarda os dados de uma embarcação individual */
typedef struct {
    int  tamanho;
    char nome[32];
    int  celulas_restantes; /* Usado para saber quando o navio afunda totalmente */
} Navio;

/* Estrutura que guarda o estado completo de um jogador na partida */
typedef struct {
    char  board[BOARD_SIZE][BOARD_SIZE]; /* Matriz do tabuleiro */
    Navio navios[5];                     /* Lista de navios do jogador */
    int   num_navios;                    /* Quantos navios já foram posicionados */
    int   navios_afundados;              /* Placar de perdas */
    int   pronto;                        /* Flag: 1 se o jogador já enviou todo o tabuleiro */
} Jogador;

/* Função auxiliar para enviar mensagens pela rede e printar no console do servidor */
void enviar(int fd, const char *msg) {
    send(fd, msg, strlen(msg), 0);
    printf("[SERVIDOR] -> fd=%d: %s", fd, msg);
}

/* Prepara o tabuleiro de um jogador, enchendo de água e zerando os status */
void init_jogador(Jogador *j) {
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            j->board[r][c] = AGUA;
            
    j->num_navios       = 0;
    j->navios_afundados = 0;
    j->pronto         = 0;
}

/* * Traduz a mensagem de posicionamento que vem do cliente.
 * Separa a string por espaços e vírgulas para extrair as coordenadas e salvar na matriz.
 */
int receber_navio(Jogador *j, const char *msg) {
    char copia[512];
    strncpy(copia, msg, sizeof(copia)-1);

    /* Divide a string usando o espaço como delimitador */
    char *tok = strtok(copia, " "); 
    if (!tok || strcmp(tok, "BOARD") != 0) return -1;

    char *coords = strtok(NULL, " "); 
    char *nome   = strtok(NULL, " "); 
    char *tam_s  = strtok(NULL, " \n"); 
    
    /* Valida se todos os pedaços da mensagem chegaram corretamente */
    if (!coords || !nome || !tam_s) return -1;

    int tam = atoi(tam_s);
    if (j->num_navios >= 5) return -1; /* Proteção contra excesso de navios */

    /* Registra os dados estruturais do navio */
    Navio *nav = &j->navios[j->num_navios];
    strncpy(nav->nome, nome, sizeof(nav->nome)-1);
    nav->tamanho = tam;
    nav->celulas_restantes = tam;

    /* Extrai cada coordenada separada por vírgula e marca no tabuleiro local */
    char *cell = strtok(coords, ",");
    while (cell) {
        if (strlen(cell) < 2) return -1;
        
        /* Converte a letra da linha (A-J) para índice numérico (0-9) */
        int row = cell[0] - 'A';
        int col = atoi(cell + 1);
        
        /* Verifica se a coordenada está dentro do mapa e se o espaço está vazio */
        if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE)
            return -1;
        if (j->board[row][col] != AGUA) return -1; 
        
        /* Grava o pedaço do navio no tabuleiro */
        j->board[row][col] = NAVIO;
        cell = strtok(NULL, ",");
    }

    j->num_navios++;
    return 0;
}

/* * Motor principal das regras do jogo. 
 * Avalia a coordenada do tiro e decide o destino da jogada.
 */
int processar_tiro(Jogador *alvo, int row, int col, char *nome_afundado) {
    /* Verifica limites do mapa e se o local já foi bombardeado antes */
    if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) return -1;
    if (alvo->board[row][col] == ATINGIDO || alvo->board[row][col] == ERROU) return -1;

    /* Caso o tiro caia na água */
    if (alvo->board[row][col] == AGUA) {
        alvo->board[row][col] = ERROU;
        return 0;
    }

    /* Caso acerte um pedaço de navio */
    alvo->board[row][col] = ATINGIDO;

    /* Varre o tabuleiro inteiro para ver se ainda sobrou algum 'N' intacto */
    int navios_vivos = 0;
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            if (alvo->board[r][c] == NAVIO) navios_vivos++;

    /* Calcula o HP total original da frota do jogador */
    int total_celulas = 0;
    for (int i = 0; i < alvo->num_navios; i++)
        total_celulas += alvo->navios[i].tamanho;

    /* Conta quantos acertos totais o jogador já sofreu */
    int atingidos = 0;
    for (int r = 0; r < BOARD_SIZE; r++)
        for (int c = 0; c < BOARD_SIZE; c++)
            if (alvo->board[r][c] == ATINGIDO) atingidos++;

    /* Se não há mais 'N' no mapa, o jogador perdeu o jogo */
    if (navios_vivos == 0) {
        strcpy(nome_afundado, "todos");
        return 3; 
    }

    /* Procura qual navio específico foi atingido para diminuir a vida dele */
    for (int i = 0; i < alvo->num_navios; i++) {
        Navio *nav = &alvo->navios[i];
        if (nav->celulas_restantes > 0) {
            nav->celulas_restantes--;
            
            /* Se a vida deste navio zerou, ele afundou agora */
            if (nav->celulas_restantes == 0) {
                strncpy(nome_afundado, nav->nome, 31);
                alvo->navios_afundados++;
                return 2; 
            }
            break;
        }
    }

    /* O tiro acertou um navio, mas ele ainda não afundou */
    return 1; 
}

int main() {
    /* Passo 1: Criar o socket (O Aparelho TCP) */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) { perror("socket"); return 1; }

    /* Configuração para evitar erro de "Porta em uso" caso o servidor reinicie rápido */
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Passo 2: Configurar a estrutura de endereço e Porta */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY; /* Aceita conexão de qualquer interface de rede */

    /* Passo 3: Amarrar o socket à porta (Bind) */
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind"); return 1;
    }
    
    /* Passo 4: Habilitar o modo de escuta para até 2 clientes pendentes na fila */
    listen(listen_fd, 2);
    printf("[SERVIDOR] Porta %d. Aguardando 2 jogadores...\n\n", PORT);

    /* Passo 5: Aceitar (Accept) os dois clientes que tentarem conectar */
    int p_fd[2];
    for (int i = 0; i < 2; i++) {
        printf("[SERVIDOR] Aguardando jogador %d...\n", i+1);
        p_fd[i] = accept(listen_fd, NULL, NULL);
        printf("[SERVIDOR] Jogador %d conectado! fd=%d\n", i+1, p_fd[i]);

        /* Avisa o cliente sobre quem ele é na partida */
        char msg[32];
        snprintf(msg, sizeof(msg), "PLAYER %d\n", i+1);
        enviar(p_fd[i], msg);
    }

    /* Prepara as variáveis lógicas do jogo na memória do servidor */
    Jogador jogadores[2];
    init_jogador(&jogadores[0]);
    init_jogador(&jogadores[1]);

    /* Comanda que os clientes enviem suas formações iniciais */
    enviar(p_fd[0], "SEND_BOARD\n");
    enviar(p_fd[1], "SEND_BOARD\n");
    printf("\n[SERVIDOR] Aguardando tabuleiros dos jogadores...\n");

    /* * Configuração do Select 
     * Agrupa os sockets dos dois jogadores para o servidor escutar ambos simultaneamente
     */
    fd_set master;
    FD_ZERO(&master);
    FD_SET(p_fd[0], &master);
    FD_SET(p_fd[1], &master);
    int max_fd = (p_fd[0] > p_fd[1]) ? p_fd[0] : p_fd[1];

    char buf[1024];

    /* Fase 1: Loop de recebimento de tabuleiros (Enquanto algum jogador não estiver pronto) */
    while (!jogadores[0].pronto || !jogadores[1].pronto) {
        /* Copia o master para o tmp, pois o select modifica o set original a cada evento */
        fd_set tmp = master;
        select(max_fd+1, &tmp, NULL, NULL, NULL);

        for (int i = 0; i < 2; i++) {
            /* Pula o jogador se não foi ele quem enviou dado ou se ele já terminou o tabuleiro */
            if (!FD_ISSET(p_fd[i], &tmp)) continue;
            if (jogadores[i].pronto) continue;

            /* Lê a mensagem recebida na rede */
            int bytes = recv(p_fd[i], buf, sizeof(buf)-1, 0);
            if (bytes <= 0) { printf("Jogador %d desconectou.\n", i+1); return 1; }
            buf[bytes] = '\0';
            buf[strcspn(buf, "\n")] = '\0'; /* Remove quebras de linha para facilitar leitura */
            
            printf("[SERVIDOR] <- fd=%d (J%d): %s\n", p_fd[i], i+1, buf);

            /* Trata as mensagens de configuração de navios */
            if (strncmp(buf, "BOARD ", 6) == 0) {
                if (receber_navio(&jogadores[i], buf) == 0) {
                    enviar(p_fd[i], "OK\n");
                } else {
                    enviar(p_fd[i], "INVALID\n");
                }
            } else if (strcmp(buf, "BOARD_DONE") == 0) {
                jogadores[i].pronto = 1; /* Marca que este jogador terminou a fase de preparo */
                printf("[SERVIDOR] Tabuleiro do Jogador %d recebido (%d navios).\n", i+1, jogadores[i].num_navios);
            }
        }
    }

    /* Fase 2: O Combate */
    enviar(p_fd[0], "READY\n");
    enviar(p_fd[1], "READY\n");

    int vez = 0; /* Indica que o Jogador 1 (índice 0) começa */
    printf("\n[SERVIDOR] JOGO INICIADO! Vez do Jogador 1.\n\n");
    enviar(p_fd[0], "YOUR_TURN\n");
    enviar(p_fd[1], "WAIT\n");

    int jogo_ativo = 1;
    
    /* Loop principal da partida */
    while (jogo_ativo) {
        fd_set tmp = master;
        select(max_fd+1, &tmp, NULL, NULL, NULL);

        for (int i = 0; i < 2; i++) {
            /* Checa se há alguma mensagem nova vindo deste jogador */
            if (!FD_ISSET(p_fd[i], &tmp)) continue;

            int bytes = recv(p_fd[i], buf, sizeof(buf)-1, 0);
            if (bytes <= 0) {
                printf("[SERVIDOR] Jogador %d desconectou.\n", i+1);
                jogo_ativo = 0; break;
            }
            
            buf[bytes] = '\0';
            buf[strcspn(buf, "\n")] = '\0';
            printf("[SERVIDOR] <- fd=%d (J%d): %s\n", p_fd[i], i+1, buf);

            /* Sistema anti-trapaça: Ignora mensagens de quem não tem a vez de jogar */
            if (i != vez) { enviar(p_fd[i], "WAIT\n"); continue; }

            /* Processamento do comando de tiro */
            if (strncmp(buf, "TIRO ", 5) == 0 && strlen(buf) >= 6) {
                char coord[8];
                strncpy(coord, buf+5, sizeof(coord)-1);

                if (strlen(coord) < 2) { enviar(p_fd[i], "INVALID\n"); continue; }

                /* Transforma texto "A3" em matriz numérica [0][3] */
                int row = coord[0] - 'A';
                int col = atoi(coord+1);

                /* Define quem está sofrendo o ataque */
                int alvo = 1 - vez; 
                char nome_afundado[32] = "";
                
                /* Chama a função que aplica as regras lógicas e devolve o status do tiro */
                int resultado = processar_tiro(&jogadores[alvo], row, col, nome_afundado);

                /* Fluxo de Respostas com base no que aconteceu */
                if (resultado == -1) {
                    enviar(p_fd[i], "INVALID\n");
                    enviar(p_fd[i], "YOUR_TURN\n");
                }
                else if (resultado == 3) {
                    /* O jogador atual destruiu a frota inteira do alvo */
                    enviar(p_fd[vez],  "WIN\n");
                    enviar(p_fd[alvo], "LOSE\n");
                    printf("[SERVIDOR] Jogador %d VENCEU!\n", vez+1);
                    jogo_ativo = 0; /* Encerra o laço principal */
                }
                else if (resultado == 2) {
                    /* Um navio foi completamente destruído. O atirador ganha tiro extra */
                    char msg[64];
                    snprintf(msg, sizeof(msg), "SUNK %s\n", nome_afundado);
                    enviar(p_fd[vez], msg);
                    enviar(p_fd[alvo], "ENEMY_SUNK\n");
                    enviar(p_fd[vez], "YOUR_TURN\n"); 
                }
                else if (resultado == 1) {
                    /* Apenas um acerto no casco. O atirador ganha tiro extra */
                    enviar(p_fd[vez], "HIT\n");
                    enviar(p_fd[alvo], "ENEMY_HIT\n");
                    enviar(p_fd[vez], "YOUR_TURN\n"); 
                }
                else {
                    /* Tiro na água. A vez passa para o adversário */
                    enviar(p_fd[vez], "MISS\n");
                    vez = 1 - vez; /* Alterna de 0 para 1, ou de 1 para 0 */
                    enviar(p_fd[vez],   "YOUR_TURN\n");
                    enviar(p_fd[1-vez], "WAIT\n");
                    printf("[SERVIDOR] Vez do Jogador %d.\n", vez+1);
                }
            } else {
                /* Qualquer comando desconhecido é invalidado */
                enviar(p_fd[i], "INVALID\n");
            }
        }
    }

    /* Passo 6: Desligamento (Fechamento dos sockets/fds para liberar a memória e as portas do SO) */
    close(p_fd[0]);
    close(p_fd[1]);
    close(listen_fd);
    printf("[SERVIDOR] Jogo encerrado.\n");
    return 0;
}
