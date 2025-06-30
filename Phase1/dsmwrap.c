#include "common_impl.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>  // Pour PATH_MAX

int main(int argc, char *argv[]) {
    /* Processus intermédiaire pour "nettoyer" */
    /* la liste des arguments qu'on va passer */
    /* à la commande à exécuter finalement */
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port_number> <launcher_hostname> <program> [args...]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Lancement de dsmwrap\n");
    printf("[dsmwrap] Port serveur reçu : %d\n", atoi(argv[1]));
    fflush(stdout);

    /* Création d'une socket pour se connecter au */
    /* lanceur et envoyer/recevoir les infos */
    /* nécessaires pour la phase dsm_init */
    int sock_launcher = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_launcher < 0) {
        perror("socket creation");
        exit(EXIT_FAILURE);
    }

    /* Connexion au lanceur */
    struct sockaddr_in launcher_addr;
    char *launcher_host = argv[2];  // Récupère le nom d'hôte du lanceur depuis argv[2]
    printf("[dsmwrap] Connecting to launcher at %s:%d\n", launcher_host, atoi(argv[1]));
    memset(&launcher_addr, 0, sizeof(launcher_addr));
    launcher_addr.sin_family = AF_INET;
    launcher_addr.sin_port = htons(atoi(argv[1]));

    if (inet_pton(AF_INET, launcher_host, &launcher_addr.sin_addr) <= 0) {
        /* Si l'adresse n'est pas une IP, on tente une résolution DNS */
        struct hostent *host_info = gethostbyname(launcher_host);
        if (host_info == NULL) {
            herror("gethostbyname");
            exit(EXIT_FAILURE);
        }
        memcpy(&launcher_addr.sin_addr, host_info->h_addr, host_info->h_length);
    }

    if (connect(sock_launcher, (struct sockaddr *)&launcher_addr, sizeof(launcher_addr)) < 0) {
        perror("connect to launcher");
        exit(EXIT_FAILURE);
    }

    /* Envoi du nom de machine au lanceur */
    char hostname[MAX_STR];
    if (gethostname(hostname, MAX_STR) < 0) {
        perror("gethostname");
        exit(EXIT_FAILURE);
    }

    /* Envoi du pid au lanceur (optionnel) */
    pid_t pid = getpid();
    if (write(sock_launcher, &pid, sizeof(pid_t)) < 0) {
        perror("write pid");
        exit(EXIT_FAILURE);
    }

    /* Création de la socket d'écoute pour les */
    /* connexions avec les autres processus dsm */
    int sock_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen < 0) {
        perror("socket listen");
        exit(EXIT_FAILURE);
    }

    /* Permettre la réutilisation rapide du port */
    int optval = 1;
    if (setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(sock_listen);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = 0;  // Port dynamique

    if (bind(sock_listen, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    /* Récupération du port attribué */
    socklen_t len = sizeof(listen_addr);
    if (getsockname(sock_listen, (struct sockaddr *)&listen_addr, &len) < 0) {
        perror("getsockname");
        exit(EXIT_FAILURE);
    }
    int port = ntohs(listen_addr.sin_port);

    /* Envoi des informations au lanceur */
    dsm_proc_conn_t init_info;
    memset(&init_info, 0, sizeof(dsm_proc_conn_t));
    strncpy(init_info.machine, hostname, MAX_STR - 1);
    init_info.port_num = port;
    init_info.rank = -1;
    init_info.fd = -1;
    init_info.fd_for_exit = -1;

    if (write(sock_launcher, &init_info, sizeof(dsm_proc_conn_t)) < 0) {
        perror("write init info");
        exit(EXIT_FAILURE);
    }

    /************** ATTENTION **************/
    /* Vous remarquerez que ce n'est pas   */
    /* ce processus qui récupère son rang, */
    /* ni le nombre de processus           */
    /* ni les informations de connexion    */
    /* (cf protocole dans dsmexec)         */
    /***************************************/

    /* Préparation des arguments pour le programme */
    char **new_argv = malloc((argc - 3 + 1) * sizeof(char*));  // argc - 3 car on saute argv[0], argv[1], argv[2]
    if (!new_argv) {
        perror("malloc new_argv");
        exit(EXIT_FAILURE);
    }

    /* Résoudre le chemin absolu du programme à exécuter */
    char prog_path[PATH_MAX];
    if (realpath(argv[3], prog_path) == NULL) {
        perror("realpath");
        free(new_argv);
        exit(EXIT_FAILURE);
    }

    new_argv[0] = prog_path;  // Utiliser le chemin absolu
    for (int i = 4; i < argc; i++) {
        new_argv[i - 3] = argv[i];  // Ajuster les indices
    }
    new_argv[argc - 3] = NULL;

    /* Clean up before exec */
    close(sock_launcher);
    close(sock_listen);

    /* Exécution du programme */
    printf("[dsmwrap] Executing: %s\n", prog_path);
    execvp(prog_path, new_argv);

    /* En cas d'échec de l'exec */
    perror("execvp failed");
    free(new_argv);
    return EXIT_FAILURE;
}
