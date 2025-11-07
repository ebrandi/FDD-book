CC = cc
CFLAGS = -Wall -Wextra -std=c99

linkage_demo: main.o foo.o
	$(CC) $(CFLAGS) -o linkage_demo main.o foo.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

foo.o: foo.c
	$(CC) $(CFLAGS) -c foo.c

clean:
	rm -f *.o linkage_demo
