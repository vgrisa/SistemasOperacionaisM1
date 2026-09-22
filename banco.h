/*
 * banco.h — definições usadas pelo cliente e pelo servidor.
 *
 * Os dois programas são processos diferentes, então precisam concordar sobre o
 * formato das estruturas que atravessam a memória compartilhada. Por isso tudo
 * o que é comum aos dois fica aqui.
 */

#ifndef BANCO_H
#define BANCO_H

/* Tamanhos do sistema */
#define MAX_REGISTROS    100   /* quantos registros a tabela comporta        */
#define MAX_REQUISICOES   10   /* slots do buffer na memória compartilhada   */
#ifndef NUM_THREADS                 /* pode ser mudado na compilação:            */
#define NUM_THREADS        4        /* gcc -DNUM_THREADS=2 ...                   */
#endif

/* Nomes dos objetos do sistema operacional */
#define SHM_NOME    "/banco_memoria"
#define SEM_VAZIAS  "/banco_vazias"   /* conta os slots livres    */
#define SEM_CHEIAS  "/banco_cheias"   /* conta as requisições     */
#define SEM_MUTEX   "/banco_mutex"    /* protege o buffer         */

/* Arquivos */
#define ARQUIVO_BANCO "banco.txt"
#define ARQUIVO_LOG   "log.txt"

/* Um registro do banco — a estrutura pedida no enunciado */
typedef struct {
    int  id;
    char nome[50];
} Registro;

/* As operações que o servidor sabe executar.
   FIM não é uma operação do banco: é o aviso de que o cliente terminou. */
typedef enum {
    INSERT,
    DELETE,
    SELECT,
    UPDATE,
    FIM
} Operacao;

/* Uma requisição do cliente para o servidor */
typedef struct {
    Operacao op;
    int      id;
    char     nome[50];
} Requisicao;

/*
 * O que fica dentro da memória compartilhada: um buffer circular de
 * requisições. O cliente escreve na posição "fim", o servidor lê na posição
 * "inicio", e os dois índices dão a volta quando chegam ao final do vetor.
 */
typedef struct {
    Requisicao requisicoes[MAX_REQUISICOES];
    int inicio;   /* de onde o servidor lê   */
    int fim;      /* onde o cliente escreve  */
} Buffer;

#endif
