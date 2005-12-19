CC = gcc
CFLAGS = -O2 -Wall -pedantic
CPPFLAGS = -I/usr/local/include/mjpegtools

BINARIES = yuvresample yuvinfo yuvcolor

all: $(BINARIES)

clean:
	rm -f *.o
	rm -f $(BINARIES)

yuvresample: yuvresample.o
	$(CC) -o $@ $^ -lmjpegutils

yuvinfo: yuvinfo.o
	$(CC) -o $@ $^ -lmjpegutils

yuvcolor: yuvcolor.o
	$(CC) -o $@ $^ -lmjpegutils

.PHONY: all clean
