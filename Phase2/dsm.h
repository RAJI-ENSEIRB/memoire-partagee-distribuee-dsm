#ifndef _DSM_H
#define _DSM_H
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
//#include <signal.h>
//#include "GM_lock.h"
#include <unistd.h>
//#include <sys/mman.h>


extern int DSM_NODE_ID;
extern int DSM_NODE_NUM;

char *dsm_init( int argc, char *argv[]);
void  dsm_finalize( void );
#endif /* _DSM_H */
