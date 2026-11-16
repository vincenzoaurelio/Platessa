CC          ?= cc
PKG_CONFIG  ?= pkg-config

SDL_CFLAGS:=$(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null)
SDL_LIBS  :=$(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf SDL2_image 2>/dev/null)

CFLAGS := -O2 -std=c11 -Wall -Wextra $(SDL_CFLAGS) 
LIBS   := $(SDL_LIBS) -lm

all: platessa

md4c.c md4c.h:
	curl -sL -o md4c.c https://raw.githubusercontent.com/mity/md4c/master/src/md4c.c
	curl -sL -o md4c.h https://raw.githubusercontent.com/mity/md4c/master/src/md4c.h

platessa: platessa.c md4c.c md4c.h
	$(CC) $(CFLAGS) -o $@ platessa.c md4c.c $(LIBS)

debug: platessa.c md4c.c md4c.h
	$(CC) $(CFLAGS) -DDEBUG -ggdb -o debug platessa.c md4c.c $(LIBS)
    

clean:
	rm -f platessa md4c.c md4c.h debug

.PHONY: all clean

