CFLAGS=-Wall -O3 $(shell pkg-config --cflags libdrm)
LDFLAGS=-lm $(shell pkg-config --libs libdrm)

all: rototiller32 rototiller64

rototiller32: rototiller32.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

rototiller64: rototiller64.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f rototiller32 rototiller64
