CORE_OBJS	= multiscale.o tweak.o
CORE_CLFILES	= render.cl ../tc/multiscale.cl

%.o: ../tc/%.c
	$(CC) $(CFLAGS) -c $<

include ../Makefile.common
