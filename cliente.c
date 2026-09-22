/*
 * cliente.c — processo cliente.
 *
 * Abre a memória compartilhada que o servidor criou e coloca requisições nela.
 * Não conhece a tabela nem as threads do servidor: a única ligação entre os
 * dois processos é a memória compartilhada.
 *
 * Compilar: make
 * Executar: ./cliente   (com o servidor já no ar)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>

#include "banco.h"

static Buffer *buffer;
static sem_t  *sem_vazias;
static sem_t  *sem_cheias;
static sem_t  *sem_mutex;

/* Coloca uma requisição no buffer compartilhado. */
static void enviar(Operacao op, int id, const char *nome)
{
    Requisicao req;

    req.op = op;
    req.id = id;
    strncpy(req.nome, nome, 49);
    req.nome[49] = '\0';

    sem_wait(sem_vazias);   /* espera ter slot livre          */
    sem_wait(sem_mutex);    /* entra na região do buffer      */

    buffer->requisicoes[buffer->fim] = req;
    buffer->fim = (buffer->fim + 1) % MAX_REQUISICOES;

    sem_post(sem_mutex);    /* libera o buffer                */
    sem_post(sem_cheias);   /* avisa que chegou requisição    */
}

int main(void)
{
    int fd, i;

    printf("=== CLIENTE ===\n");

    /* ---- abre a memória compartilhada criada pelo servidor ---- */
    fd = shm_open(SHM_NOME, O_RDWR, 0666);
    if (fd == -1) {
        printf("ERRO: nao achei a memoria compartilhada.\n");
        printf("O servidor esta rodando? Abra outro terminal e execute ./servidor\n");
        return 1;
    }
    buffer = mmap(NULL, sizeof(Buffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* ---- abre os semáforos criados pelo servidor ---- */
    sem_vazias = sem_open(SEM_VAZIAS, 0);
    sem_cheias = sem_open(SEM_CHEIAS, 0);
    sem_mutex  = sem_open(SEM_MUTEX,  0);
    if (sem_vazias == SEM_FAILED || sem_cheias == SEM_FAILED || sem_mutex == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    printf("conectado ao servidor, enviando requisicoes...\n\n");

    /* ---- as requisições ---- */
    enviar(INSERT, 7,  "Joao");
    printf("enviado: INSERT id=7 nome='Joao'\n");

    enviar(SELECT, 5,  "");
    printf("enviado: SELECT nome WHERE id=5\n");

    enviar(INSERT, 10, "Maria");
    printf("enviado: INSERT id=10 nome='Maria'\n");

    enviar(UPDATE, 7,  "Joao Paulo");
    printf("enviado: UPDATE nome='Joao Paulo' WHERE id=7\n");

    enviar(SELECT, 7,  "");
    printf("enviado: SELECT nome WHERE id=7\n");

    enviar(DELETE, 2,  "");
    printf("enviado: DELETE WHERE id=2\n");

    enviar(SELECT, 2,  "");
    printf("enviado: SELECT nome WHERE id=2\n");

    enviar(INSERT, 1,  "Repetido");
    printf("enviado: INSERT id=1 nome='Repetido'  (id ja existe)\n");

    /* ---- avisa o servidor que acabou ----
       Uma requisição FIM para cada thread: cada uma recebe a sua e encerra. */
    for (i = 0; i < NUM_THREADS; i++)
        enviar(FIM, 0, "");

    printf("\ntodas as requisicoes enviadas\n");

    munmap(buffer, sizeof(Buffer));
    close(fd);
    sem_close(sem_vazias);
    sem_close(sem_cheias);
    sem_close(sem_mutex);
    return 0;
}
