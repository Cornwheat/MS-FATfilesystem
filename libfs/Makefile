# Target library
cc = gcc
CLFAGS = -Wall -Werror
lib := libfs.a

all: $(lib)

## TODO

clean: 
	rm -f *.o
	rm -f libfs


disk.o: disk.c
	$(cc) -g -c disk.c -o disk.o


fs.o: fs.c
	$(cc) -g -c fs.c -o fs.o


libfs.a: disk.o fs.o
	ar rcs libfs.a disk.o fs.o
