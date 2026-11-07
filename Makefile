CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2

all: montador ligador

montador: montador.c common.h
	$(CC) $(CFLAGS) -o montador montador.c

ligador: ligador.c common.h
	$(CC) $(CFLAGS) -o ligador ligador.c

clean:
	rm -f montador ligador *.obj *.lig
