# This Makefile assumes it will be invoked from the top-level Makefile.
# It needs the compressor library to have already been built.

# override CFLAGS +=

override BINARIES = varint_test compress_test

.PHONY: all
all : $(BINARIES)

varint_test : varint_test.o ../varint.o
	$(CC) $(CFLAGS) -o varint_test varint_test.o ../varint.o

varint_test.o : varint_test.c ../compressor.h ../varint.h
	$(CC) $(CFLAGS) -I.. -c -o varint_test.o varint_test.c

compress_test : compress_test.o ../compressor.o ../compressor_utils.o ../varint.o
	$(CC) $(CFLAGS) -o compress_test compress_test.o ../compressor.o ../compressor_utils.o ../varint.o

compress_test.o : compress_test.c ../compressor.h ../compressor_utils.h ../varint.h
	$(CC) $(CFLAGS) -I.. -c -o compress_test.o compress_test.c

.PHONY: test
test : all
	./varint_test
	./compress_test

.PHONY: clean
clean :
	rm -f $(BINARIES) *.o

.PHONY: force
force : clean all