DIRS	= tc mstp life map

all: $(DIRS)

$(DIRS):	FRC
	( cd $@ && make )

clean:	FRC
	( cd tc && make clean )
	( cd mstp && make clean )
	( cd map && make clean )
	( cd life && make clean )
	rm -f *~

FRC:
