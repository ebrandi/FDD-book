CC = cc
CFLAGS = -Wall -Wextra -std=c99

mathprog: main.o mathutils.o
	$(CC) $(CFLAGS) -o mathprog main.o mathutils.o

main.o: main.c mathutils.h
	$(CC) $(CFLAGS) -c main.c

mathutils.o: mathutils.c mathutils.h
	$(CC) $(CFLAGS) -c mathutils.c

clean:
	rm -f *.o mathprog
