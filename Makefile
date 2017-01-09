CFLAGS=-D_GNU_SOURCE -Wall -O3 -ffast-math -pthread $(shell pkg-config --cflags libdrm) -I.
LDFLAGS=-lm -pthread $(shell pkg-config --libs libdrm)
SRC=rototiller.c modules/ray/*.c modules/roto/roto.c modules/sparkler/*.c modules/stars/*.c drmsetup.c fb.c fps.c util.c

all: rototiller

rototiller: $(SRC) Makefile modules/ray/*.h modules/roto/roto.h modules/sparkler/*.h modules/stars/stars.h drmsetup.h fb.h fps.h util.h
	$(CC) -o $@ $(SRC) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f rototiller
