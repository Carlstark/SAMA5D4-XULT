CROSS=arm-linux-
FLAGS = -I./3rd/include
LIBS  = -L./3rd/lib -pthread -lts -Wl,-Bstatic -ljpeg -Wl,-Bdynamic 

all: camview

camview: main.c fbutils.c
	$(CROSS)gcc -s -o $@ $^ $(FLAGS) $(LIBS)

clean:
	@rm -vf camview *.o *~
