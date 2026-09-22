/*
 * servidor.c — processo servidor (gerenciador do banco).
 *
 * Cria a memória compartilhada e os semáforos, carrega o banco do arquivo e
 * sobe um pool de threads. Cada thread pega uma requisição do buffer e a
 * executa sobre a tabela de dados, que é compartilhada por todas elas.
 *
 * O acesso à tabela é protegido por um mutex: é a seção crítica do programa.
 *
 * Compilar: make
 * Executar: ./servidor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>

#include "banco.h"

/* ------------------------------------------------------------------ */
/* Variáveis compartilhadas pelas threads                              */
/* ------------------------------------------------------------------ */

/* A "tabela de dados": é ela que todas as threads acessam ao mesmo tempo. */
static Registro tabela[MAX_REGISTROS];
static int      total_registros = 0;

/* O mutex que protege a tabela. Só uma thread por vez entra na seção crítica. */
static pthread_mutex_t mutex_tabela = PTHREAD_MUTEX_INITIALIZER;

/* O log também é compartilhado, então também precisa de um mutex — senão as
   mensagens das threads saem misturadas no meio da linha. */
static pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;
static FILE *arquivo_log = NULL;

/* Memória compartilhada e semáforos */
static Buffer *buffer;
static sem_t  *sem_vazias;
static sem_t  *sem_cheias;
static sem_t  *sem_mutex;

/* ------------------------------------------------------------------ */
/* Log                                                                 */
/* ------------------------------------------------------------------ */

static void escrever_log(int thread, const char *mensagem)
{
    pthread_mutex_lock(&mutex_log);
    printf("[thread %d] %s\n", thread, mensagem);
    fflush(stdout);
    if (arquivo_log) {
        fprintf(arquivo_log, "[thread %d] %s\n", thread, mensagem);
        fflush(arquivo_log);
    }
    pthread_mutex_unlock(&mutex_log);
}

/* ------------------------------------------------------------------ */
/* Operações da tabela                                                 */
/*                                                                     */
/* Todas são chamadas com o mutex já preso, então podem mexer na tabela */
/* sem se preocupar com as outras threads.                              */
/* ------------------------------------------------------------------ */

static int procurar_posicao(int id)
{
    int i;
    for (i = 0; i < total_registros; i++)
        if (tabela[i].id == id)
            return i;
    return -1;   /* não encontrado */
}

static void op_insert(int id, const char *nome, char *resultado)
{
    if (procurar_posicao(id) != -1) {
        sprintf(resultado, "INSERT id=%d -> ERRO: id ja existe", id);
        return;
    }
    if (total_registros >= MAX_REGISTROS) {
        sprintf(resultado, "INSERT id=%d -> ERRO: tabela cheia", id);
        return;
    }
    tabela[total_registros].id = id;
    strncpy(tabela[total_registros].nome, nome, 49);
    tabela[total_registros].nome[49] = '\0';
    total_registros++;
    sprintf(resultado, "INSERT id=%d nome='%s' -> OK", id, nome);
}

static void op_select(int id, char *resultado)
{
    int pos = procurar_posicao(id);
    if (pos == -1)
        sprintf(resultado, "SELECT nome WHERE id=%d -> nao encontrado", id);
    else
        sprintf(resultado, "SELECT nome WHERE id=%d -> '%s'", id, tabela[pos].nome);
}

static void op_update(int id, const char *nome, char *resultado)
{
    int pos = procurar_posicao(id);
    if (pos == -1) {
        sprintf(resultado, "UPDATE id=%d -> nao encontrado", id);
        return;
    }
    strncpy(tabela[pos].nome, nome, 49);
    tabela[pos].nome[49] = '\0';
    sprintf(resultado, "UPDATE id=%d nome='%s' -> OK", id, nome);
}

static void op_delete(int id, char *resultado)
{
    int pos = procurar_posicao(id);
    int i;
    if (pos == -1) {
        sprintf(resultado, "DELETE WHERE id=%d -> nao encontrado", id);
        return;
    }
    /* Puxa os registros seguintes uma posição para trás. */
    for (i = pos; i < total_registros - 1; i++)
        tabela[i] = tabela[i + 1];
    total_registros--;
    sprintf(resultado, "DELETE WHERE id=%d -> OK", id);
}

/* ------------------------------------------------------------------ */
/* Arquivo do banco                                                    */
/* ------------------------------------------------------------------ */

static void carregar_banco(void)
{
    FILE *f = fopen(ARQUIVO_BANCO, "r");
    if (f == NULL) {
        printf("banco.txt nao encontrado, comecando com a tabela vazia\n");
        return;
    }
    while (total_registros < MAX_REGISTROS &&
           fscanf(f, "%d;%49[^\n]\n", &tabela[total_registros].id,
                  tabela[total_registros].nome) == 2) {
        total_registros++;
    }
    fclose(f);
    printf("banco.txt carregado com %d registro(s)\n", total_registros);
}

static void salvar_banco(void)
{
    int i;
    FILE *f = fopen(ARQUIVO_BANCO, "w");
    if (f == NULL) {
        printf("nao foi possivel gravar o banco.txt\n");
        return;
    }
    for (i = 0; i < total_registros; i++)
        fprintf(f, "%d;%s\n", tabela[i].id, tabela[i].nome);
    fclose(f);
    printf("banco.txt salvo com %d registro(s)\n", total_registros);
}

/* ------------------------------------------------------------------ */
/* A função que cada thread do pool executa                            */
/* ------------------------------------------------------------------ */

static void *trabalhador(void *arg)
{
    int meu_numero = *(int *)arg;
    Requisicao req;
    char resultado[200];

    while (1) {
        /* ---- pega uma requisição da memória compartilhada ---- */
        sem_wait(sem_cheias);   /* espera existir requisição          */
        sem_wait(sem_mutex);    /* entra na região do buffer          */

        req = buffer->requisicoes[buffer->inicio];
        buffer->inicio = (buffer->inicio + 1) % MAX_REQUISICOES;

        sem_post(sem_mutex);    /* libera o buffer                    */
        sem_post(sem_vazias);   /* avisa que abriu um slot            */

        if (req.op == FIM)
            break;              /* o cliente terminou                 */

        /* ---- início da SEÇÃO CRÍTICA ------------------------------
           Daqui até o unlock, só esta thread mexe na tabela.         */
        pthread_mutex_lock(&mutex_tabela);

        switch (req.op) {
            case INSERT: op_insert(req.id, req.nome, resultado); break;
            case SELECT: op_select(req.id, resultado);           break;
            case UPDATE: op_update(req.id, req.nome, resultado); break;
            case DELETE: op_delete(req.id, resultado);           break;
            default:     sprintf(resultado, "operacao desconhecida");
        }

        /* Pausa proposital: sem ela tudo termina em milissegundos e não dá
           para acompanhar as threads se revezando na tela. */
        usleep(300000);

        pthread_mutex_unlock(&mutex_tabela);
        /* ---- fim da SEÇÃO CRÍTICA ------------------------------- */

        escrever_log(meu_numero, resultado);
    }

    escrever_log(meu_numero, "encerrando");
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Programa principal                                                  */
/* ------------------------------------------------------------------ */

int main(void)
{
    pthread_t threads[NUM_THREADS];
    int       numeros[NUM_THREADS];
    int       i, fd;

    printf("=== SERVIDOR ===\n");

    carregar_banco();

    arquivo_log = fopen(ARQUIVO_LOG, "w");

    /* ---- memória compartilhada ---- */
    shm_unlink(SHM_NOME);   /* apaga sobra de uma execução anterior */
    fd = shm_open(SHM_NOME, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }
    if (ftruncate(fd, sizeof(Buffer)) == -1) {
        perror("ftruncate");
        return 1;
    }
    buffer = mmap(NULL, sizeof(Buffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    buffer->inicio = 0;
    buffer->fim    = 0;

    /* ---- semáforos ---- */
    sem_unlink(SEM_VAZIAS);
    sem_unlink(SEM_CHEIAS);
    sem_unlink(SEM_MUTEX);
    sem_vazias = sem_open(SEM_VAZIAS, O_CREAT, 0666, MAX_REQUISICOES);
    sem_cheias = sem_open(SEM_CHEIAS, O_CREAT, 0666, 0);
    sem_mutex  = sem_open(SEM_MUTEX,  O_CREAT, 0666, 1);
    if (sem_vazias == SEM_FAILED || sem_cheias == SEM_FAILED || sem_mutex == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    printf("memoria compartilhada e semaforos criados\n");

    /* ---- pool de threads ---- */
    for (i = 0; i < NUM_THREADS; i++) {
        numeros[i] = i;
        pthread_create(&threads[i], NULL, trabalhador, &numeros[i]);
    }
    printf("%d threads criadas, esperando requisicoes...\n\n", NUM_THREADS);

    /* Espera todas as threads terminarem. */
    for (i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    printf("\n");
    salvar_banco();

    /* ---- limpeza ---- */
    if (arquivo_log) fclose(arquivo_log);
    munmap(buffer, sizeof(Buffer));
    close(fd);
    shm_unlink(SHM_NOME);
    sem_close(sem_vazias); sem_unlink(SEM_VAZIAS);
    sem_close(sem_cheias); sem_unlink(SEM_CHEIAS);
    sem_close(sem_mutex);  sem_unlink(SEM_MUTEX);

    printf("servidor encerrado\n");
    return 0;
}
