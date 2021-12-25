# This Makefile is strictly for building a static version of this library

SRC=$(wildcard csrc/*.c)
OBJ=$(SRC:.c=.o)

CFLAGS:=-Wall -Wextra -Werror -Og -ggdb -std=c99 -pedantic
CFLAGS+=-I./include

INSTALL_PREFIX:=/usr/local/lib

ltreesitter.a: $(OBJ)
	$(AR) r $@ $(OBJ)

clean:
	rm -f $(OBJ) ltreesitter.a

all: clean ltreesitter.a

install: ltreesitter.a
	cp ltreesitter.a $(INSTALL_PREFIX)/

.PHONY: clean all
