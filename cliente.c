#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* --- CONFIGURAÇÕES DO JOGO E DA REDE --- */
#define SERVER_PORT      8080  /* A porta de comunicação exata que o servidor está escutando */
#define GRID_DIMENSION   10    /* Tamanho do tabuleiro (10x10) */

/* Dicionário visual: caracteres usados para desenhar o mapa no terminal */
#define CELL_WATER       '.'
#define CELL_SHIP        'N'
#define CELL_HIT         'X'
#define CELL_MISS        'O'

/* --- MATRIZES GLOBAIS DO JOGADOR --- */
/* O cliente precisa de dois mapas separados para jogar: */
char grade_aliada[GRID_DIMENSION][GRID_DIMENSION];   /* O mapa onde ficam os SEUS navios */
char grade_inimiga[GRID_DIMENSION][GRID_DIMENSION];  /* O mapa "radar" para anotar onde você atirou */

/* Estrutura que define as propriedades de cada tipo de embarcação */
typedef struct {
    char designacao[32]; /* Nome do navio (ex: SUBMARINO) */
    int  comprimento;    /* Quantos espaços ele ocupa na matriz */
} ClasseNavio;

/* O esquadrão padrão que o jogador precisa posicionar no início da partida */
ClasseNavio esquadrao[] = {
    {"PORTA-AVIOES", 5},
    {"NAVIO-GUERRA", 4},
    {"DESTROYER",    3},
    {"SUBMARINO",    2},
    {"PATRULHA",     1},
};
#define TOTAL_FROTA 5

/* =========================================================================
 * FUNÇÕES DE INTERFACE VISUAL
 * ========================================================================= */

/* Limpa a tela do terminal (Cross-platform: funciona no Windows e no Linux/Mac) */
void limpar_console() {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[2J\033[H");
#endif
}

/* Aplica códigos ANSI para colorir os caracteres no terminal do Linux/Mac */
void aplicar_cor(char celula) {
    switch (celula) {
        case CELL_SHIP: printf("\033[0;34m(N)\033[0m"); break; /* Navio = Azul */
        case CELL_HIT:  printf("\033[0;31m(X)\033[0m"); break; /* Fogo = Vermelho */
        case CELL_MISS: printf("\033[0;33m(~)\033[0m"); break; /* Água atingida = Amarelo */
        default:        printf(" . ");                  break; /* Desconhecido = Cinza */
    }
}

/* Renderiza os dois mapas lado a lado na tela do jogador */
void desenhar_interface() {
    printf("\n================= COMANDO NAVAL =================\n\n");
    /* Ajustamos o espaçamento dos títulos para ficar centralizado */
    printf("       TERRITÓRIO ALIADO                     RADAR INIMIGO\n");
    /* Adicionamos os espaços corretos no meio para alinhar a régua 2 perfeitamente! */
    printf("   0  1  2  3  4  5  6  7  8  9        0  1  2  3  4  5  6  7  8  9\n");

    /* Laço para desenhar linha por linha */
    for (int l = 0; l < GRID_DIMENSION; l++) {
        char index_linha = 'A' + l; /* Converte índice 0, 1, 2 para as letras A, B, C */
        
        /* 1. Imprime a linha do mapa aliado (Esquerda) */
        printf("%c ", index_linha);
        for (int c = 0; c < GRID_DIMENSION; c++) aplicar_cor(grade_aliada[l][c]);
        
        printf("    ");
        
        /* 2. Imprime a linha do mapa de ataques (Direita) */
        printf("%c ", index_linha);
        for (int c = 0; c < GRID_DIMENSION; c++) aplicar_cor(grade_inimiga[l][c]);
        printf("\n");
    }
    printf("\n  (N)=Embarcação  (X)=Fogo/Acerto  (~)=Água  . =Desconhecido\n\n");
}

/* =========================================================================
 * LÓGICA DE JOGO E PREPARAÇÃO
 * ========================================================================= */

/* Enche ambos os mapas com água antes do jogo iniciar usando a função de memória memset */
void zerar_matrizes() {
    memset(grade_aliada, CELL_WATER, sizeof(grade_aliada));
    memset(grade_inimiga, CELL_WATER, sizeof(grade_inimiga));
}

/* Traduz texto humano (ex: "B4") para índices de matriz (linha 1, coluna 4) */
int decodificar_coordenada(const char *input, int *linha, int *coluna) {
    if (!input || strlen(input) < 2) return -1;
    
    char char_linha = toupper(input[0]);
    if (char_linha < 'A' || char_linha > 'J') return -1; /* Valida a letra */
    
    *linha = char_linha - 'A';
    *coluna = atoi(input + 1); /* Pega o número logo após a letra */
    
    if (*coluna < 0 || *coluna >= GRID_DIMENSION) return -1; /* Valida o número */
    return 0;
}

/* Sistema interativo onde o jogador escolhe onde colocar os navios. 
 * Ele faz a validação local e depois envia a coordenada pro Servidor aprovar.
 */
int alocar_embarcacao(const ClasseNavio *navio, int socket_cliente) {
    char input_buffer[64];
    int linha, coluna, is_horizontal;

    /* Repete até o jogador digitar algo válido que o servidor aceite */
    while (1) {
        printf(" -> Alocando %s (Ocupa %d espaços)\n", navio->designacao, navio->comprimento);
        printf(" Digite a coordenada raiz (ex: B4): ");
        fflush(stdout);

        /* Lê a digitação do usuário */
        if (!fgets(input_buffer, sizeof(input_buffer), stdin)) return -1;
        input_buffer[strcspn(input_buffer, "\n")] = '\0';
        for (int i = 0; input_buffer[i]; i++) input_buffer[i] = toupper(input_buffer[i]);

        if (decodificar_coordenada(input_buffer, &linha, &coluna) == -1) {
            printf(" [AVISO] Coordenada fora do padrão.\n\n"); continue;
        }

        /* Navios de tamanho 1 não precisam de direção vertical/horizontal */
        if (navio->comprimento > 1) {
            printf(" Qual o eixo? (H = Horizontal / V = Vertical): ");
            fflush(stdout);
            char eixo[8];
            if (!fgets(eixo, sizeof(eixo), stdin)) return -1;
            is_horizontal = (toupper(eixo[0]) == 'H');
        } else {
            is_horizontal = 1; 
        }

        /* SIMULAÇÃO DE COLISÃO: Verifica se cabe no mapa e se não bate em outro navio */
        int espaco_livre = 1;
        for (int i = 0; i < navio->comprimento; i++) {
            int l_atual = linha + (is_horizontal ? 0 : i);
            int c_atual = coluna + (is_horizontal ? i : 0);
            if (l_atual >= GRID_DIMENSION || c_atual >= GRID_DIMENSION || grade_aliada[l_atual][c_atual] != CELL_WATER) {
                espaco_livre = 0; break;
            }
        }

        if (!espaco_livre) {
            printf(" [AVISO] Manobra impossível. Fora do mapa ou área já ocupada.\n\n");
            continue;
        }

        /* MONTAGEM DA MENSAGEM DE REDE: Exemplo de resultado -> "BOARD B4,C4,D4 DESTROYER 3" */
        char string_coords[256] = "";
        for (int i = 0; i < navio->comprimento; i++) {
            int l_atual = linha + (is_horizontal ? 0 : i);
            int c_atual = coluna + (is_horizontal ? i : 0);
            
            grade_aliada[l_atual][c_atual] = CELL_SHIP; /* Desenha localmente */

            char bloco[8];
            snprintf(bloco, sizeof(bloco), "%c%d", 'A' + l_atual, c_atual);
            if (i > 0) strcat(string_coords, ",");
            strcat(string_coords, bloco);
        }

        /* Envia o pedido usando WRITE (padrão POSIX escolhido para o projeto) */
        char pacote[512];
        snprintf(pacote, sizeof(pacote), "BOARD %s %s %d\n", string_coords, navio->designacao, navio->comprimento);
        write(socket_cliente, pacote, strlen(pacote));

        /* Aguarda a resposta do servidor para confirmar se a posição foi aceita */
        char resposta_srv[32];
        int bytes_lidos = read(socket_cliente, resposta_srv, sizeof(resposta_srv) - 1);
        if (bytes_lidos <= 0) return -1;
        
        resposta_srv[bytes_lidos] = '\0';
        resposta_srv[strcspn(resposta_srv, "\n")] = '\0';

        if (strcmp(resposta_srv, "OK") == 0) {
            printf(" [+] %s posicionado com sucesso!\n\n", navio->designacao);
            desenhar_interface();
            return 0;
        } else {
            /* Se o servidor rejeitou, apagamos o desenho local (Rollback) e pedimos de novo */
            for (int i = 0; i < navio->comprimento; i++) {
                int l_atual = linha + (is_horizontal ? 0 : i);
                int c_atual = coluna + (is_horizontal ? i : 0);
                grade_aliada[l_atual][c_atual] = CELL_WATER;
            }
            printf(" [ERRO] O servidor recusou as coordenadas.\n\n");
        }
    }
}

/* =========================================================================
 * FUNÇÃO PRINCIPAL (MAIN) - GERENCIAMENTO DE REDE E EVENTOS
 * ========================================================================= */

int main(int argc, char *argv[]) {
    /* Pega o IP que o usuário digitou ao abrir o jogo, ou usa localhost por padrão */
    const char *endereco_ip = (argc > 1) ? argv[1] : "127.0.0.1";

    /* PASSO 1: Criação do Socket TCP (O "telefone" do cliente) */
    int socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_cliente == -1) { perror("Erro ao abrir socket"); return 1; }

    /* PASSO 2: Configuração de Endereço e Porta do destino (Servidor) */
    struct sockaddr_in dados_servidor;
    memset(&dados_servidor, 0, sizeof(dados_servidor));
    dados_servidor.sin_family = AF_INET;
    dados_servidor.sin_port   = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, endereco_ip, &dados_servidor.sin_addr) <= 0) {
        fprintf(stderr, "Falha ao reconhecer IP: %s\n", endereco_ip); return 1;
    }

    /* PASSO 3: Connect - Tenta ligar para o servidor na porta 8080 */
    printf("[SISTEMA] Estabelecendo conexão com o QG em %s:%d...\n", endereco_ip, SERVER_PORT);
    if (connect(socket_cliente, (struct sockaddr*)&dados_servidor, sizeof(dados_servidor)) == -1) {
        perror("Erro de conexão"); return 1;
    }

    zerar_matrizes();

    char buffer_rede[1024];
    int  meu_turno    = 0;
    int  minha_id     = 0;
    int  modo_preparo = 0; 

    /* PASSO 4: Loop Principal do Jogo (A Máquina de Estados) */
    while (1) {
        /* Configuração do SELECT: 
         * Permite que o jogo escute simultaneamente o Socket (para receber tiros do inimigo)
         * e o Teclado STDIN (para quando for a sua vez de digitar um tiro).
         */
        fd_set descritores_leitura;
        FD_ZERO(&descritores_leitura);
        FD_SET(socket_cliente, &descritores_leitura);
        
        /* Só libera o teclado se for a sua vez de atirar */
        if (meu_turno && !modo_preparo)
            FD_SET(STDIN_FILENO, &descritores_leitura);

        /* O Select pausa o programa aqui até o teclado ou a rede dispararem */
        if (select(socket_cliente + 1, &descritores_leitura, NULL, NULL, NULL) == -1) break;

        /* --------------------------------------------------------
         * CADASTRANDO EVENTOS DA REDE (Mensagens vindas do servidor)
         * -------------------------------------------------------- */
        if (FD_ISSET(socket_cliente, &descritores_leitura)) {
            int recebidos = read(socket_cliente, buffer_rede, sizeof(buffer_rede) - 1);
            if (recebidos <= 0) { printf("\n[ALERTA] Conexão com o servidor perdida.\n"); break; }
            
            buffer_rede[recebidos] = '\0';
            buffer_rede[strcspn(buffer_rede, "\n")] = '\0';

            /* Interpretador de Comandos do Protocolo: */
            if (strncmp(buffer_rede, "PLAYER ", 7) == 0) {
                minha_id = atoi(buffer_rede + 7);
                printf("[SISTEMA] Identificação confirmada. Você é o Comandante %d.\n", minha_id);
            }
            else if (strcmp(buffer_rede, "SEND_BOARD") == 0) {
                modo_preparo = 1;
                limpar_console();
                printf("=== FASE DE ESTRATÉGIA (Comandante %d) ===\n\n", minha_id);
                desenhar_interface();

                /* Pede para o usuário posicionar os 5 navios */
                for (int i = 0; i < TOTAL_FROTA; i++) {
                    if (alocar_embarcacao(&esquadrao[i], socket_cliente) == -1) {
                        printf("[FALHA] Procedimento de alocação abortado.\n");
                        return 1;
                    }
                }
                
                /* Avisa o servidor que já terminamos de montar o tabuleiro */
                write(socket_cliente, "BOARD_DONE\n", 11);
                printf("[SISTEMA] Frota pronta. Aguardando inteligência inimiga...\n");
                modo_preparo = 0;
            }
            else if (strcmp(buffer_rede, "READY") == 0) {
                limpar_console();
                desenhar_interface();
                printf("=== CÓDIGO VERMELHO: O COMBATE COMEÇOU ===\n");
            }
            else if (strcmp(buffer_rede, "YOUR_TURN") == 0) {
                meu_turno = 1;
                printf("\n>>> ARMAS PRONTAS. Informe o alvo (ex: C5): ");
                fflush(stdout); /* Garante que o texto apareça imediatamente na tela */
            }
            else if (strcmp(buffer_rede, "WAIT") == 0) {
                meu_turno = 0;
                printf("[...] Inimigo calculando disparo. Aguarde...\n");
            }
            else if (strcmp(buffer_rede, "HIT") == 0) {
                printf("\033[0;31m[!] IMPACTO CONFIRMADO! Você tem direito a outro tiro.\033[0m\n");
            }
            else if (strcmp(buffer_rede, "MISS") == 0) {
                printf("\033[0;33m[-] Tiro na água. Passando o controle ao inimigo.\033[0m\n");
            }
            else if (strncmp(buffer_rede, "SUNK ", 5) == 0) {
                printf("\033[0;31m[!!!] ALVO DESTRUÍDO: %s afundou! Dispare novamente.\033[0m\n", buffer_rede + 5);
            }
            else if (strcmp(buffer_rede, "ENEMY_HIT") == 0) {
                printf("\033[0;31m[ALERTA] Casco danificado! Inimigo acertou um tiro.\033[0m\n");
                desenhar_interface();
            }
            else if (strcmp(buffer_rede, "ENEMY_SUNK") == 0) {
                printf("\033[0;31m[CRÍTICO] Perdemos uma embarcação!\033[0m\n");
                desenhar_interface();
            }
            else if (strcmp(buffer_rede, "INVALID") == 0) {
                printf("[ERRO] Coordenada restrita ou repetida.\n>>> ");
                fflush(stdout);
            }
            else if (strcmp(buffer_rede, "WIN") == 0) {
                desenhar_interface();
                printf("\033[0;32m\n * * * VITÓRIA DECLARADA! A frota inimiga foi aniquilada! * * *\033[0m\n\n");
                break;
            }
            else if (strcmp(buffer_rede, "LOSE") == 0) {
                desenhar_interface();
                printf("\033[0;31m\n * * * DERROTA. O oceano engoliu nossa frota. * * *\033[0m\n\n");
                break;
            }
        }

        /* --------------------------------------------------------
         * CADASTRANDO EVENTOS DO TECLADO (Jogador atirando)
         * -------------------------------------------------------- */
        if (meu_turno && !modo_preparo && FD_ISSET(STDIN_FILENO, &descritores_leitura)) {
            char digitado[32];
            if (!fgets(digitado, sizeof(digitado), stdin)) break;
            
            digitado[strcspn(digitado, "\n")] = '\0';
            for (int i = 0; digitado[i]; i++) digitado[i] = toupper(digitado[i]);

            int linha_tiro, coluna_tiro;
            if (decodificar_coordenada(digitado, &linha_tiro, &coluna_tiro) == 0) {
                /* Impede o jogador de gastar tiro em um lugar que ele já atirou */
                if (grade_inimiga[linha_tiro][coluna_tiro] != CELL_WATER) {
                    printf("[AVISO] Setor já bombardeado. Escolha outro.\n>>> ");
                    fflush(stdout);
                } else {
                    /* Marca temporariamente com uma interrogação até o servidor confirmar */
                    grade_inimiga[linha_tiro][coluna_tiro] = '?'; 
                    
                    /* Formata o tiro (ex: "TIRO A3") e manda pelo WRITE */
                    char ataque_msg[32];
                    snprintf(ataque_msg, sizeof(ataque_msg), "TIRO %c%d\n", 'A' + linha_tiro, coluna_tiro);
                    write(socket_cliente, ataque_msg, strlen(ataque_msg));
                    
                    meu_turno = 0; /* Trava o teclado local enquanto o servidor não responder */

                    /* Trava esperando o veredito instantâneo do servidor (Hit ou Miss) */
                    int lidos_sync = read(socket_cliente, buffer_rede, sizeof(buffer_rede) - 1);
                    if (lidos_sync <= 0) break;
                    
                    buffer_rede[lidos_sync] = '\0';
                    buffer_rede[strcspn(buffer_rede, "\n")] = '\0';

                    /* Se foi acerto ou afundou um navio inteiro: */
                    if (strcmp(buffer_rede, "HIT") == 0 || strncmp(buffer_rede, "SUNK", 4) == 0) {
                        grade_inimiga[linha_tiro][coluna_tiro] = CELL_HIT; /* Pinta um X vermelho no radar */
                        
                        if (strncmp(buffer_rede, "SUNK ", 5) == 0)
                            printf("\033[0;31m[!!!] ALVO DESTRUÍDO: %s afundou! Dispare novamente.\033[0m\n", buffer_rede + 5);
                        else
                            printf("\033[0;31m[!] IMPACTO CONFIRMADO! Você tem direito a outro tiro.\033[0m\n");
                        
                        desenhar_interface();

                        /* Confere se a partida acabou (WIN) ou se o servidor mandou YOUR_TURN de novo */
                        read(socket_cliente, buffer_rede, sizeof(buffer_rede) - 1);
                        buffer_rede[strcspn(buffer_rede, "\n")] = '\0';
                        
                        if (strcmp(buffer_rede, "YOUR_TURN") == 0) {
                            meu_turno = 1;
                            printf(">>> ARMAS PRONTAS. Informe o alvo: ");
                            fflush(stdout);
                        } else if (strcmp(buffer_rede, "WIN") == 0) {
                            desenhar_interface();
                            printf("\033[0;32m\n * * * VITÓRIA DECLARADA! A frota inimiga foi aniquilada! * * *\033[0m\n\n");
                            goto encerrar;
                        }
                    
                    /* Se o tiro acertou a água: */
                    } else if (strcmp(buffer_rede, "MISS") == 0) {
                        grade_inimiga[linha_tiro][coluna_tiro] = CELL_MISS; /* Pinta um ~ amarelo no radar */
                        printf("\033[0;33m[-] Tiro na água. Passando o controle ao inimigo.\033[0m\n");
                        desenhar_interface();
                        /* Lê e limpa o próximo pacote "WAIT" que vem colado do servidor */
                        read(socket_cliente, buffer_rede, sizeof(buffer_rede) - 1);
                    
                    /* Se o tiro foi negado pelas regras de negócio do servidor: */
                    } else if (strcmp(buffer_rede, "INVALID") == 0) {
                        grade_inimiga[linha_tiro][coluna_tiro] = CELL_WATER;
                        printf("[ERRO] Setor inválido reportado pelo servidor.\n>>> ");
                        fflush(stdout);
                        meu_turno = 1; /* Devolve o turno para ele atirar direito */
                    
                    /* Caso especial: último navio foi afundado */
                    } else if (strcmp(buffer_rede, "WIN") == 0) {
                        desenhar_interface();
                        printf("\033[0;32m\n * * * VITÓRIA DECLARADA! A frota inimiga foi aniquilada! * * *\033[0m\n\n");
                        goto encerrar;
                    }
                }
            } else {
                printf("[ERRO] Sintaxe desconhecida. Exemplo de uso: D7\n>>> ");
                fflush(stdout);
            }
        }
    }

/* PASSO 5: Encerramento Seguro */
encerrar:
    /* Fecha o File Descriptor do socket, desligando a conexão com o servidor */
    close(socket_cliente);
    return 0;
}
