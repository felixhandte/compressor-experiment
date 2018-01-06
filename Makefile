export

CC = gcc
CFLAGS = -O3 -march=native -mtune=native -ggdb -Wall -Wextra -Wno-pointer-sign

HEADERS = compressor.h compressor_utils.h varint.h
OBJECTS = compressor.o compressor_utils.o varint.o

.PHONY: all
all : compressor tests

compressor : $(OBJECTS) main.o
	$(CC) $(CFLAGS) -o compressor $(OBJECTS) main.o

main.o : main.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o main.o main.c

compressor.o : compressor.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o compressor.o compressor.c

compressor_utils.o : compressor_utils.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o compressor_utils.o compressor_utils.c

varint.o : varint.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o varint.o varint.c


.PHONY: tests
tests : $(OBJECTS)
	$(MAKE) -C tests CFLAGS="$(CFLAGS)"

.PHONY: test
test : tests
	$(MAKE) -C tests test


.PHONY: clean
clean :
	rm -f compressor *.o
	$(MAKE) -C tests clean

.PHONY: force
force : clean all
