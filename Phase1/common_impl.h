#ifndef COMMON_IMPL_H
#define COMMON_IMPL_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* autres includes (eventuellement) */

#define ERROR_EXIT(str) {perror(str);exit(EXIT_FAILURE);}

/**************************************************************/
/****************** DEBUT DE PARTIE NON MODIFIABLE ************/
/**************************************************************/

#define MAX_STR  (1024)
typedef char maxstr_t[MAX_STR];

/* definition du type des infos */
/* de connexion des processus dsm */
struct dsm_proc_conn  {
   int      rank;
   maxstr_t machine;
   int      port_num;
   int      fd; 
   int      fd_for_exit; /* special */  
};

typedef struct dsm_proc_conn dsm_proc_conn_t; 

/**************************************************************/
/******************* FIN DE PARTIE NON MODIFIABLE *************/
/**************************************************************/

/* definition du type des infos */
/* d'identification des processus dsm */
struct dsm_proc {   
  pid_t pid;
  dsm_proc_conn_t connect_info;
};
typedef struct dsm_proc dsm_proc_t;

/* Constantes */
#define NB_MAX_PROC 100
#define LENGTH_IP_ADDR 16

/* Prototypes des fonctions */
/* Gestion d'erreurs */
void error(char* error_description);

/* Fonctions de base socket */
int do_socket(void);
void do_bind(int socket, struct sockaddr_in addr_in);
void do_listen(int socket, int nb_max);
int do_accept(int socket, struct sockaddr *addr, socklen_t* addrlen);
void do_connect(int sock, struct sockaddr_in host_addr);

/* Fonctions d'initialisation d'adresses */
void init_serv_addr(struct sockaddr_in *serv_addr, int port);
void init_client_addr(struct sockaddr_in *serv_addr, char *ip, int port);

/* Fonction de création socket serveur */
int creer_socket_serv(int *serv_port, struct sockaddr_in *serv_addr);

/* Fonctions de communication sécurisée */
ssize_t safe_write(int fd, const void *buf, size_t count);
ssize_t safe_read(int fd, void *buf, size_t count);

#endif /* COMMON_IMPL_H */