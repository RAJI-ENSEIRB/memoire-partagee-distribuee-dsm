#include "common_impl.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>  // Pour PATH_MAX
#include <signal.h>
#include <sys/select.h>
#include <bits/sigaction.h>
#include <asm-generic/signal-defs.h>

/* variables globales */
dsm_proc_t *proc_array = NULL;
volatile int num_procs_creat = 0;
int *pipe_stdout = NULL;
int *pipe_stderr = NULL;

void usage(void) {
    fprintf(stdout, "Usage : dsmexec machine_file executable arg1 arg2 ...\n");
    fflush(stdout);
    exit(EXIT_FAILURE);
}

void sigchld_handler(int sig) {
    /* On traite les fils qui se terminent */
    /* pour éviter les zombies */
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        num_procs_creat--;
        printf("Process with pid %d terminated, remaining: %d\n", pid, num_procs_creat);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage();
    } else {
        pid_t pid;
        int num_procs = 0;
        int i;

        /* Mise en place d'un traitant pour récupérer les fils zombies */
        struct sigaction sa;
        sa.sa_handler = sigchld_handler;
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCHLD, &sa, NULL);

        /* Lecture du fichier de machines */
        FILE *machine_file = fopen(argv[1], "r");
        if (machine_file == NULL) {
            perror("Error opening machine file");
            exit(EXIT_FAILURE);
        }

        /* Comptage du nombre de machines */
        char line[MAX_STR];
        while (fgets(line, MAX_STR, machine_file) != NULL) {
            line[strcspn(line, "\n")] = '\0';
            if (strlen(line) > 0) {
                num_procs++;
            }
        }
        printf("Number of processes to launch: %d\n", num_procs);

        /* Allocation des tableaux */
        proc_array = malloc(num_procs * sizeof(dsm_proc_t));
        pipe_stdout = malloc(num_procs * sizeof(int));
        pipe_stderr = malloc(num_procs * sizeof(int));

        if (!proc_array || !pipe_stdout || !pipe_stderr) {
            perror("malloc error");
            exit(EXIT_FAILURE);
        }

        /* Initialisation des structures */
        memset(proc_array, 0, num_procs * sizeof(dsm_proc_t));

        /* Relecture du fichier pour remplir proc_array */
        rewind(machine_file);
        int proc_index = 0;
        while (fgets(line, MAX_STR, machine_file) != NULL && proc_index < num_procs) {
            line[strcspn(line, "\n")] = '\0';
            if (strlen(line) > 0) {
                strncpy(proc_array[proc_index].connect_info.machine, line, MAX_STR - 1);
                proc_array[proc_index].connect_info.rank = proc_index;
                proc_array[proc_index].connect_info.port_num = -1;
                proc_array[proc_index].connect_info.fd = -1;
                proc_array[proc_index].connect_info.fd_for_exit = -1;
                printf("Added machine %d: %s\n", proc_index, proc_array[proc_index].connect_info.machine);
                proc_index++;
            }
        }
        fclose(machine_file);

        /* Récupération du nom d'hôte du lanceur */
        char launcher_hostname[MAX_STR];
        if (gethostname(launcher_hostname, MAX_STR) < 0) {
            perror("gethostname");
            exit(EXIT_FAILURE);
        }

        /* Création de la socket d'écoute */
        int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        /* Permettre la réutilisation rapide du port */
        int optval = 1;
        if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            perror("setsockopt");
            close(listen_sock);
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in server_addr = {0};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Accepte les connexions sur toutes les interfaces
        server_addr.sin_port = 0; /* Port dynamique */

        if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("bind");
            exit(EXIT_FAILURE);
        }

        socklen_t addrlen = sizeof(server_addr);
        if (getsockname(listen_sock, (struct sockaddr *)&server_addr, &addrlen) == -1) {
            perror("getsockname");
            exit(EXIT_FAILURE);
        }
        int listen_port = ntohs(server_addr.sin_port);
        printf("Listening on port %d\n", listen_port);

        if (listen(listen_sock, SOMAXCONN) == -1) {
            perror("listen");
            exit(EXIT_FAILURE);
        }

        /* Création des fils */
        for (i = 0; i < num_procs; i++) {
            int stdout_pipe[2];
            int stderr_pipe[2];

            /* Création du tube pour rediriger stdout */
            if (pipe(stdout_pipe) == -1) ERROR_EXIT("pipe stdout");
            /* Création du tube pour rediriger stderr */
            if (pipe(stderr_pipe) == -1) ERROR_EXIT("pipe stderr");

            pipe_stdout[i] = stdout_pipe[0];
            pipe_stderr[i] = stderr_pipe[0];

            pid = fork();
            if (pid == -1) ERROR_EXIT("fork");

            if (pid == 0) { /* Fils */
                /* Redirection stdout/stderr */
                dup2(stdout_pipe[1], STDOUT_FILENO);
                dup2(stderr_pipe[1], STDERR_FILENO);
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);
                close(stderr_pipe[0]);
                close(stderr_pipe[1]);

                /* Fermeture des descripteurs inutilisés */
                close(listen_sock);
                for (int k = 0; k < i; k++) {
                    close(pipe_stdout[k]);
                    close(pipe_stderr[k]);
                }

                /* Résoudre le chemin absolu de l'exécutable */
                char *exec_path = argv[2];

                char port_str[16];
                snprintf(port_str, sizeof(port_str), "%d", listen_port);

                /* Calcul du nombre total d'arguments SSH */
                int ssh_arg_count = 0;
                /* Compter les arguments de base */
                ssh_arg_count = 7;  // "ssh", "-o", "StrictHostKeyChecking=no", machine, "dsmwrap", port_str, launcher_hostname
                /* Ajouter le programme et ses arguments */
                for (int k = 3; k < argc; k++) {  // Commence à 3 au lieu de 2
                    ssh_arg_count++;
                }

                /* Allocation du tableau d'arguments */
                char **ssh_args = malloc((ssh_arg_count + 1) * sizeof(char*));  // +1 pour le NULL final
                if (!ssh_args) {
                    ERROR_EXIT("malloc ssh_args");
                }

                /* Remplissage des arguments SSH */
                int arg_index = 0;
                ssh_args[arg_index++] = "ssh";
                ssh_args[arg_index++] = "-o";
                ssh_args[arg_index++] = "StrictHostKeyChecking=no";
                ssh_args[arg_index++] = proc_array[i].connect_info.machine;
                ssh_args[arg_index++] = "dsmwrap";
                ssh_args[arg_index++] = port_str;
                ssh_args[arg_index++] = launcher_hostname;  // Ajout du nom d'hôte du lanceur

                /* Ajout du programme et de ses arguments */
                ssh_args[arg_index++] = exec_path;  // Utiliser exec_path
                for (int k = 3; k < argc; k++) {    // Ajuster l'indice de départ
                    ssh_args[arg_index++] = argv[k];
                }
                ssh_args[arg_index] = NULL;

                /* Affichage pour debug */
                printf("Executing: ");
                for(int j = 0; ssh_args[j] != NULL; j++) {
                    printf("%s ", ssh_args[j]);
                }
                printf("\n");

                execvp("ssh", ssh_args);
                perror("execvp");
                free(ssh_args);
                exit(EXIT_FAILURE);

            } else if (pid > 0) { /* Père */
                /* Fermeture des extrémités des tubes non utiles */
                close(stdout_pipe[1]);
                close(stderr_pipe[1]);

                proc_array[i].pid = pid;
                num_procs_creat++;
                printf("Created process %d with pid %d\n", i, pid);
            }
        }

        /* Acceptation des connexions des processus dsm */
        for (i = 0; i < num_procs; i++) {
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(client_addr);

            printf("Waiting for connection from process %d/%d\n", i+1, num_procs);
            int sock_fd = accept(listen_sock, (struct sockaddr *)&client_addr, &len);
            if (sock_fd == -1) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            printf("Connection accepted from process %d\n", i);

            /* Stockage du descripteur */
            proc_array[i].connect_info.fd = sock_fd;

            /* Réception des informations de connexion du processus distant */
            dsm_proc_conn_t proc_info;
            if (read(sock_fd, &proc_info, sizeof(dsm_proc_conn_t)) < 0) {
                perror("read proc_info");
                exit(EXIT_FAILURE);
            }
            proc_array[i].connect_info.port_num = proc_info.port_num;
        }

        /***********************************************************/
        /********** ATTENTION : LE PROTOCOLE D'ECHANGE *************/
        /********** DECRIT CI-DESSOUS NE DOIT PAS ETRE *************/
        /********** MODIFIE, NI DEPLACE DANS LE CODE   *************/
        /***********************************************************/

        /* 1- Envoi du nombre de processus aux processus dsm */
        /* On envoie cette information sous la forme d'un ENTIER */
        
                

        /* 2- Envoi des rangs aux processus dsm */
        /* Chaque processus distant ne reçoit QUE SON numéro de rang */
      

        /* 3- Envoi des infos de connexion aux processus */
        /* Chaque processus distant doit recevoir toutes les infos de connexion */
        

        /***********************************************************/
        /********** FIN DU PROTOCOLE D'ECHANGE DES DONNEES *********/
        /********** ENTRE DSMEXEC ET LES PROCESSUS DISTANTS ********/
        /***********************************************************/

        /* Gestion des E/S : on récupère les caractères */
        /* sur les tubes de redirection de stdout/stderr */
        while (num_procs_creat > 0) {
            fd_set readfds;
            FD_ZERO(&readfds);

            int max_fd = 0;
            int tubes_ouverts = 0;

            for (i = 0; i < num_procs; i++) {
                if (pipe_stdout[i] >= 0) {
                    FD_SET(pipe_stdout[i], &readfds);
                    max_fd = (pipe_stdout[i] > max_fd) ? pipe_stdout[i] : max_fd;
                    tubes_ouverts++;
                }
                if (pipe_stderr[i] >= 0) {
                    FD_SET(pipe_stderr[i], &readfds);
                    max_fd = (pipe_stderr[i] > max_fd) ? pipe_stderr[i] : max_fd;
                    tubes_ouverts++;
                }
            }

            if (tubes_ouverts == 0) {
                printf("All pipes closed, exiting...\n");
                break;
            }

            if (select(max_fd + 1, &readfds, NULL, NULL, NULL) == -1) {
                if (errno == EINTR) continue;
                ERROR_EXIT("select");
            }

            for (i = 0; i < num_procs; i++) {
                if (pipe_stdout[i] >= 0 && FD_ISSET(pipe_stdout[i], &readfds)) {
                    char buffer[1024];
                    int bytes_read = read(pipe_stdout[i], buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("[Proc %d : %s : stdout] %s", i, proc_array[i].connect_info.machine, buffer);
                        fflush(stdout);
                    } else if (bytes_read == 0 || (bytes_read < 0 && errno != EINTR)) {
                        close(pipe_stdout[i]);
                        pipe_stdout[i] = -1;
                    }
                }
                if (pipe_stderr[i] >= 0 && FD_ISSET(pipe_stderr[i], &readfds)) {
                    char buffer[1024];
                    int bytes_read = read(pipe_stderr[i], buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        fprintf(stderr, "[Proc %d : %s : stderr] %s", i, proc_array[i].connect_info.machine, buffer);
                        fflush(stderr);
                    } else if (bytes_read == 0 || (bytes_read < 0 && errno != EINTR)) {
                        close(pipe_stderr[i]);
                        pipe_stderr[i] = -1;
                    }
                }
            }
        }

        /* On attend les processus fils */
        while (num_procs_creat > 0) {
            pause();  // Attend que le gestionnaire de signaux traite les fils restants
        }

        /* On ferme les descripteurs proprement */
        for (i = 0; i < num_procs; i++) {
            if (pipe_stdout[i] >= 0)
                close(pipe_stdout[i]);
            if (pipe_stderr[i] >= 0)
                close(pipe_stderr[i]);
            if (proc_array[i].connect_info.fd >= 0)
                close(proc_array[i].connect_info.fd);
        }

        /* On ferme la socket d'écoute */
        close(listen_sock);

        /* Libération de la mémoire */
        free(proc_array);
        free(pipe_stdout);
        free(pipe_stderr);

        exit(EXIT_SUCCESS);
    }
}
