1. Use make to compile
make
gcc -Wall -pedantic -ggdb -c oss.c
gcc -Wall -pedantic -ggdb -c shared.c
gcc -Wall -pedantic -ggdb oss.o shared.o -o oss
gcc -Wall -pedantic -ggdb -c user.c
gcc -Wall -pedantic -ggdb user.o shared.o -o user

2. Run program
	./oss
	./oss -v

3. Description
Program generates output in txt files - oss.txt and P[1..N].txt. Frame Table is represented as mentioned in the document.
