ALL: default 

CC       = gcc
CLINKER  = $(CC)
OPTFLAGS = -O0 

SHELL = /bin/sh

CFLAGS  = -DREENTRANT -g -I../Phase1
CCFLAGS = $(CFLAGS)
LIBS    = -lpthread 

EXECS = exemple 

default: $(EXECS) 

$(EXECS): %: %.o dsm.o ../Phase1/common.o

dsm.o: dsm_impl.h

%:%.o dsm.o ../Phase1/common.o
	$(CLINKER) $(OPTFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@-mv $@ ../Phase1/bin/		

clean:
	@-/bin/rm -f *.o *~ PI* $(EXECS) *.out core  *.so

