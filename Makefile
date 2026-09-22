# Makefile — compila os dois programas.
#
#   -pthread  habilita as threads (pthread_create, pthread_mutex_t)
#   -lrt      biblioteca de tempo real: shm_open e sem_open

CC     = gcc
CFLAGS = -Wall -Wextra -pthread
LIBS   = -lrt

all: servidor cliente

servidor: servidor.c banco.h
	$(CC) $(CFLAGS) servidor.c -o servidor $(LIBS)

cliente: cliente.c banco.h
	$(CC) $(CFLAGS) cliente.c -o cliente $(LIBS)

limpar:
	rm -f servidor cliente log.txt
