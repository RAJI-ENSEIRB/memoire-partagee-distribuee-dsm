#include "common_impl.h"

/* Gestion des erreurs avec message descriptif et arrêt du programme */
void error(char* error_description) {
   perror(error_description);
   exit(EXIT_FAILURE);
}

/* Création d'une socket TCP */
int do_socket(void) {
   int sock;
   /* Création avec gestion des interruptions */
   do {
       sock = socket(AF_INET, SOCK_STREAM, 0);
   } while (sock == -1 && (errno == EAGAIN || errno == EINTR));
   
   if (sock == -1) error("socket creation failed");
   return sock;
}

/* Initialisation d'une adresse serveur */
void init_serv_addr(struct sockaddr_in *serv_addr, int port) {
   /* Remise à zéro de la structure */
   memset(serv_addr, 0, sizeof(struct sockaddr_in));
   
   /* Configuration de l'adresse */
   serv_addr->sin_family = AF_INET;           // IPv4
   serv_addr->sin_port = htons(port);         // Port en network byte order
   serv_addr->sin_addr.s_addr = INADDR_ANY;   // Toutes les interfaces
}

/* Initialisation d'une adresse client avec IP spécifique */
void init_client_addr(struct sockaddr_in *serv_addr, char *ip, int port) {
   /* Remise à zéro de la structure */
   memset(serv_addr, 0, sizeof(struct sockaddr_in));
   
   /* Configuration de l'adresse */
   serv_addr->sin_family = AF_INET;           // IPv4
   serv_addr->sin_port = htons(port);         // Port en network byte order
   serv_addr->sin_addr.s_addr = inet_addr(ip);// Conversion de l'IP
}

/* Association d'une socket à une adresse (bind) */
void do_bind(int socket, struct sockaddr_in addr_in) {
   if (bind(socket, (struct sockaddr *)&addr_in, sizeof(addr_in)) == -1) {
       error("bind failed");
   }
}

/* Mise en écoute d'une socket */
void do_listen(int socket, int nb_max) {
   if (listen(socket, nb_max) == -1) {
       error("listen failed");
   }
}

/* Acceptation d'une connexion entrante avec gestion des interruptions */
int do_accept(int socket, struct sockaddr *addr, socklen_t* addrlen) {
   int client_sock;
   do {
       client_sock = accept(socket, addr, addrlen);
   } while (client_sock == -1 && errno == EINTR);
   
   if (client_sock == -1) error("accept failed");
   return client_sock;
}

/* Connexion à un serveur avec gestion des interruptions */
void do_connect(int sock, struct sockaddr_in host_addr) {
   int ret;
   do {
       ret = connect(sock, (struct sockaddr *)&host_addr, sizeof(host_addr));
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
   
   if (ret == -1) error("connect failed");
}

/* Création d'une socket serveur complète */
int creer_socket_serv(int *serv_port, struct sockaddr_in *serv_addr) {
   /* Création de la socket */
   int sock = do_socket();
   
   /* Initialisation de l'adresse */
   init_serv_addr(serv_addr, *serv_port);
   
   /* Bind de la socket */
   do_bind(sock, *serv_addr);
   
   /* Récupération du port attribué si port dynamique */
   socklen_t len = sizeof(struct sockaddr_in);
   if (getsockname(sock, (struct sockaddr *)serv_addr, &len) == -1) {
       error("getsockname failed");
   }
   *serv_port = ntohs(serv_addr->sin_port);
   
   return sock;
}

/* Sécurisation des communications avec gestion des interruptions */
ssize_t safe_write(int fd, const void *buf, size_t count) {
   size_t written = 0;
   while (written < count) {
       ssize_t ret = write(fd, buf + written, count - written);
       if (ret == -1) {
           if (errno == EINTR) continue;
           return -1;
       }
       written += ret;
   }
   return written;
}

/* Lecture sécurisée avec gestion des interruptions */
ssize_t safe_read(int fd, void *buf, size_t count) {
   size_t read_bytes = 0;
   while (read_bytes < count) {
       ssize_t ret = read(fd, buf + read_bytes, count - read_bytes);
       if (ret == -1) {
           if (errno == EINTR) continue;
           return -1;
       }
       if (ret == 0) break; // EOF
       read_bytes += ret;
   }
   return read_bytes;
}

