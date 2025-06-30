#include "dsm_impl.h"

int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */ 



static dsm_proc_conn_t *procs = NULL;
static dsm_page_info_t table_page[PAGE_NUMBER];
static pthread_t comm_daemon;
static void *fault_addr;

static int dsm_send(int dest, void *buf, size_t size);
static int dsm_recv(int from, void *buf, size_t size);
void init_client_addr(struct sockaddr_in *serv_addr, char *ip, int port); 


/* indique l'adresse de debut de la page de numero numpage */
static char *num2address( int numpage ){ 
   char *pointer = (char *)(BASE_ADDR+(numpage*(PAGE_SIZE)));
   
   if( pointer >= (char *)TOP_ADDR ){
      fprintf(stderr,"[%i] Invalid address !\n", DSM_NODE_ID);
      return NULL;
   }
   else return pointer;
}

/* cette fonction permet de recuperer un numero de page */
/* a partir  d'une adresse  quelconque */
static int address2num( char *addr ){
   return (((intptr_t)(addr - BASE_ADDR))/(PAGE_SIZE));
}

/* cette fonction permet de recuperer l'adresse d'une page */
/* a partir d'une adresse quelconque (dans la page)        */
static char *address2pgaddr( char *addr ){
   return  (char *)(((intptr_t) addr) & ~(PAGE_SIZE-1)); 
}

/* fonctions pouvant etre utiles */
static void dsm_change_info( int numpage, dsm_page_state_t state, dsm_page_owner_t owner){
   if ((numpage >= 0) && (numpage < PAGE_NUMBER)){	
	if (state != NO_CHANGE )
	table_page[numpage].status = state;
      if (owner >= 0 )
	table_page[numpage].owner = owner;
      return;
   }
   else{
	fprintf(stderr,"[%i] Invalid page number !\n", DSM_NODE_ID);
      return;
   }
}

static dsm_page_owner_t get_owner( int numpage){
   return table_page[numpage].owner;
}

static dsm_page_state_t get_status( int numpage){
   return table_page[numpage].status;
}

/* Allocation d'une nouvelle page */
static void dsm_alloc_page( int numpage ){
   char *page_addr = num2address( numpage );
   mmap(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   return ;
}

/* Changement de la protection d'une page */
static void dsm_protect_page( int numpage , int prot){
   char *page_addr = num2address( numpage );
   mprotect(page_addr, PAGE_SIZE, prot);
   return;
}

static void dsm_free_page( int numpage ){
   char *page_addr = num2address( numpage );
   munmap(page_addr, PAGE_SIZE);
   return;
}

// gére les communications réseau entre les différents processus dans un système de mémoire partagée
static void *dsm_comm_daemon( void *arg){  
   while(1){
      fd_set readfds;
      int max_fd = 0;
      FD_ZERO(&readfds);

      // parcours tous les processus, déscripteur ajouté, max_fd mis à jour
      for(int i = 0; i < DSM_NODE_NUM; i++){
         if(i != DSM_NODE_ID){
            FD_SET(procs[i].fd, &readfds);
            if(procs[i].fd > max_fd) max_fd = procs[i].fd;
         }
      }

      // lecture requête, reception + envoie page demandé, modification protection page + changement infos
      if(select(max_fd + 1, &readfds, NULL, NULL, NULL) > 0){
         for(int i = 0; i < DSM_NODE_NUM; i++) {
            if(i != DSM_NODE_ID && FD_ISSET(procs[i].fd, &readfds)) {
               dsm_req_t req;
               dsm_recv(i, &req, sizeof(dsm_req_t));
               char *page_addr = num2address(req.page_num);
               dsm_send(req.source, page_addr, PAGE_SIZE);
               dsm_protect_page(req.page_num, PROT_NONE);
               dsm_change_info(req.page_num, NO_ACCESS, req.source);
            }
         }
      }

	/* a modifier : OK */
	printf("[%i] Waiting for incoming reqs \n", DSM_NODE_ID);
	sleep(2);
   }
   return NULL;
}

static int dsm_send(int dest,void *buf,size_t size){
   if (write(procs[dest].fd, buf, size) < 0) {
      perror("dsm_send failed");
      return -1;
   }
   /* a completer : OK */
  return 0;
}

static int dsm_recv(int from,void *buf,size_t size){
   if (read(procs[from].fd, buf, size) < 0) {
      perror("dsm_recv failed");
      return -1;
   }
   /* a completer : OK */
  return 0;
}

// gère les fautes d'accès mémoire
static void dsm_handler(void){

   /* A modifier */
   int page_num = address2num((char *)fault_addr);
   dsm_page_owner_t owner = get_owner(page_num);

   printf("[%i] Requesting page %d from owner %d\n", DSM_NODE_ID, page_num, owner);

   dsm_req_t req;
   req.page_num = page_num;
   req.source = DSM_NODE_ID;

   dsm_send(owner, &req, sizeof(dsm_req_t));
   char *page_addr = num2address(page_num);
   dsm_recv(owner, page_addr, PAGE_SIZE);

   dsm_protect_page(page_num, PROT_READ | PROT_WRITE);
   dsm_change_info(page_num, WRITE, DSM_NODE_ID);
}

/* traitant de signal adequat */
static void segv_handler(int sig, siginfo_t *info, void *context){
   /* A completer */
   /* adresse qui a provoque une erreur */
   void  *addr = info->si_addr;   
  /* Si ceci ne fonctionne pas, utiliser a la place :*/
  /*
   #ifdef __x86_64__
   void *addr = (void *)(context->uc_mcontext.gregs[REG_CR2]);
   #elif __i386__
   void *addr = (void *)(context->uc_mcontext.cr2);
   #else
   void  addr = info->si_addr;
   #endif
   */
   /*
   pour plus tard (question ++):
   dsm_access_t access  = (((ucontext_t *)context)->uc_mcontext.gregs[REG_ERR] & 2) ? WRITE_ACCESS : READ_ACCESS;   
  */   
   /* adresse de la page dont fait partie l'adresse qui a provoque la faute */
   void  *page_addr  = (void *)(((unsigned long) addr) & ~(PAGE_SIZE-1));

   if ((addr >= (void *)BASE_ADDR) && (addr < (void *)TOP_ADDR)){
	   dsm_handler();
   }
   else{
	/* SIGSEGV normal : ne rien faire*/
   }
}

/* Seules ces deux dernieres fonctions sont visibles et utilisables */
/* dans les programmes utilisateurs de la DSM                       */
char *dsm_init(int argc, char *argv[]){   
   struct sigaction act;
   int index;   

   /* Récupération de la valeur des variables d'environnement */
   /* DSMEXEC_FD et MASTER_FD                                 */
   int dsmexec_fd = atoi(getenv("DSMEXEC_FD"));
   int master_fd = atoi(getenv("MASTER_FD"));

   if (dsmexec_fd < 0 || master_fd < 0){
      fprintf(stderr, "Erreur: Variables d'environnement DSMEXEC_FD ou MASTER_FD.\n", DSM_NODE_ID);
      exit(EXIT_FAILURE);
   }

   /* reception du nombre de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_NUM) */
   if (read(dsmexec_fd, &DSM_NODE_NUM, sizeof(int)) != sizeof(int)) {
      perror("Erreur reception DSM_NODE_NUM");
      exit(EXIT_FAILURE);
   }
   
   /* reception de mon numero de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_ID)      */
   if (read(dsmexec_fd, &DSM_NODE_ID, sizeof(int)) != sizeof(int)) {
      perror("Erreur reception DSM_NODE_ID");
      exit(EXIT_FAILURE);
   }
   
   /* reception des informations de connexion des autres */
   /* processus envoyees par le lanceur :                */
   /* nom de machine, numero de port, etc.               */
   procs = malloc(DSM_NODE_NUM * sizeof(dsm_proc_conn_t)); // tableau contenant les informations de connexion 
   if (!procs) {
      perror("Erreur à l'allocation de mémoire pour procs");
      exit(EXIT_FAILURE);
   }

   for (index = 0; index < DSM_NODE_NUM; index++) {
      ssize_t total_bytes_read = 0;
      ssize_t bytes_read;
      char *buffer_ptr = (char *)&procs[index];
      size_t bytes_to_read = sizeof(dsm_proc_conn_t); 

      while (total_bytes_read < bytes_to_read) {
         bytes_read = read(dsmexec_fd, buffer_ptr + total_bytes_read, bytes_to_read - total_bytes_read);
        
         if (bytes_read < 0) {
            if (errno == EINTR) {
               continue;
            }
            perror("Erreur reception des informations de connexion");
            fprintf(stderr, "[%i] Bytes reçus : %ld, attendu : %ld\n", DSM_NODE_ID, total_bytes_read, (long)bytes_to_read);
            exit(EXIT_FAILURE);
         } else if (bytes_read == 0) {
            fprintf(stderr, "[%i] Connexion fermée avant de recevoir toutes les informations de connexion.\n", DSM_NODE_ID);
            exit(EXIT_FAILURE);
         }

      total_bytes_read += bytes_read;
      }

   printf("[%i] Informations de connexion reçu par le processus %d: %s:%d\n", DSM_NODE_ID, index, procs[index].machine, procs[index].port_num);
   } 
   
   /* initialisation des connexions              */ 
   /* avec les autres processus : connect/accept */   
   int listen_sock = -1;

   if (DSM_NODE_NUM > 1) {
      printf("[%d] Creating listening socket\n", getpid());
      listen_sock = socket(AF_INET, SOCK_STREAM, 0);
      if (listen_sock < 0) {
         perror("socket creation");
         exit(EXIT_FAILURE);
      }

      struct sockaddr_in server_addr = {0};
      server_addr.sin_family = AF_INET;
      server_addr.sin_addr.s_addr = INADDR_ANY;
      server_addr.sin_port = htons(atoi(argv[2]) + 1024 + DSM_NODE_ID);

      printf("[%d] Binding to port %d\n", getpid(), ntohs(server_addr.sin_port));
      if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
         perror("bind");
         exit(EXIT_FAILURE);
      }

      if (listen(listen_sock, DSM_NODE_NUM) < 0) {
         perror("listen");
         exit(EXIT_FAILURE);
      }
      printf("[%d] Listening for connections\n", getpid());

      for (int i = 0; i < DSM_NODE_NUM; i++) {
         if (i < DSM_NODE_ID) {
               struct sockaddr_in client_addr;
               socklen_t len = sizeof(client_addr);
               printf("[%d] Waiting for connection from process %d\n", DSM_NODE_ID, i);
               procs[i].fd = accept(listen_sock, (struct sockaddr*)&client_addr, &len);
               if (procs[i].fd < 0) {
                  perror("accept");
                  exit(EXIT_FAILURE);
               }
               printf("[%d] Accepted connection from process %d\n", DSM_NODE_ID, i);
         } else if (i > DSM_NODE_ID) {
               sleep(1);
               printf("[%d] Connecting to process %d\n", DSM_NODE_ID, i);

               struct hostent *he = gethostbyname(procs[i].machine);
               if (!he) {
                  fprintf(stderr, "[%d] Cannot resolve hostname %s\n", getpid(), procs[i].machine);
                  exit(EXIT_FAILURE);
               }

               struct sockaddr_in remote_addr = {0};
               remote_addr.sin_family = AF_INET;
               remote_addr.sin_port = htons(atoi(argv[2]) + 1024 + i);
               memcpy(&remote_addr.sin_addr, he->h_addr_list[0], he->h_length);

               procs[i].fd = socket(AF_INET, SOCK_STREAM, 0);
               if (connect(procs[i].fd, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
                  perror("connect");
                  exit(EXIT_FAILURE);
               }
               printf("[%d] Connected to process %d\n", DSM_NODE_ID, i);
         }
      }

      if (listen_sock >= 0) {
         close(listen_sock);
      }
   }
   
   /* Allocation des pages en tourniquet */
   for(index = 0; index < PAGE_NUMBER; index ++){	
      if ((index % DSM_NODE_NUM) == DSM_NODE_ID)
         dsm_alloc_page(index);	     
         dsm_change_info( index, WRITE, index % DSM_NODE_NUM);
   }
   
   /* mise en place du traitant de SIGSEGV */
   act.sa_flags = SA_SIGINFO; 
   act.sa_sigaction = segv_handler;
   sigaction(SIGSEGV, &act, NULL);
   
   /* creation du thread de communication           */
   /* ce thread va attendre et traiter les requetes */
   /* des autres processus                          */
   pthread_create(&comm_daemon, NULL, dsm_comm_daemon, NULL);
   
   /* Adresse de début de la zone de mémoire partagée */
   return ((char *)BASE_ADDR);
}

void dsm_finalize( void ){

   for (int i = 0; i < PAGE_NUMBER; i++) {
      dsm_free_page(i);
   }
   
   /* fermer proprement les connexions avec les autres processus */
   for(int i = 0; i < DSM_NODE_NUM; i++) {
       if(i != DSM_NODE_ID) {
           close(procs[i].fd);
       }
   }

   /* terminer correctement le thread de communication */
   pthread_cancel(comm_daemon);
   pthread_join(comm_daemon, NULL);

   /* libération de la mémoire */
   free(procs);
   
   return;
}

