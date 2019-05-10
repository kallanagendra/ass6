CC=gcc
CFLAGS=-Wall

sim: oss user

shared.o: shared.c shared.h
	$(CC) $(CFLAGS) -c shared.c

oss.o: oss.c shared.h
	$(CC) $(CFLAGS) -c oss.c

user.o: user.c shared.h
	$(CC) $(CFLAGS) -c user.c

oss: oss.o shared.o
	$(CC) $(CFLAGS) oss.o shared.o -o oss

user: user.o shared.o
	$(CC) $(CFLAGS) user.o shared.o -o user

clean:
	rm -f oss user *.o P[0-9]*.txt oss.txt
