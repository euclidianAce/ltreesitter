# !!!
# !!! You should probably be using `luarocks make` instead of this
# !!!
# !!! This is mostly for quick and dirty testing
# !!!

HEADERS=$(wildcard csrc/*.h) $(wildcard include/ltreesitter/*.h)
SRC=$(wildcard csrc/*.c)
OBJ=$(SRC:.c=.o)

CFLAGS := -I./include -Wall -Wextra -Werror -Og -ggdb -std=c99 -pedantic -fPIC
# CFLAGS += -DLOG_GC
# CFLAGS += -fsanitize=address,undefined,leak
LIBS :=
# LIBS += -lasan -lubsan -lpthread
LIBS += -ltree-sitter -llua -ldl

INSTALL_PREFIX:=/usr/local/lib

dynamic: ltreesitter.so
static: ltreesitter.a

ltreesitter.a: $(OBJ) $(HEADERS)
	$(AR) r $@ $(OBJ)

ltreesitter.so: $(OBJ) $(HEADERS)
	$(CC) -shared $(OBJ) -o $@ $(LIBS)

clean:
	rm -f $(OBJ) ltreesitter.a ltreesitter.so

all: clean ltreesitter.a ltreesitter.so

install: ltreesitter.a ltreesitter.so
	cp ltreesitter.a $(INSTALL_PREFIX)/
	cp ltreesitter.so $(INSTALL_PREFIX)/

.PHONY: clean all
