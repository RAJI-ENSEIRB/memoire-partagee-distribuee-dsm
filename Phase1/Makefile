ALL: default 

CC           = gcc
CLINKER      = $(CC)
OPTFLAGS     = -O0


SHELL = /bin/sh

CFLAGS  =   -DREENTRANT -Wunused -Wall -g 
CCFLAGS = $(CFLAGS)
LIBS =  -lpthread

EXECS = dsmexec dsmwrap truc

default: $(EXECS)

$(EXECS): %: %.o common.o

#%.o:%.c
#	$(CC) $(CFLAGS) -c $<

%:%.o common.o
	$(CLINKER) $(OPTFLAGS) -o $@ $^ $(LIBS)
	@-/bin/mkdir -p ./bin ; 
	@-mv $@ ./bin/

clean:
	@-/bin/rm -f *.o *~ PI* $(EXECS) *.out core 

veryclean: clean
	@-/bin/rm -f ./bin/*
	@-/bin/rmdir ./bin

