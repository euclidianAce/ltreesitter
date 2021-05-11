
# luarocks will override all of these so the running lua version wont matter in that case
LUA_VERSION = $(shell lua -e 'print(_VERSION:match("%d.%d"))')
LUA_DIR = /usr/local
LUA_INCDIR = $(LUA_DIR)/include
LUA_LIBDIR = $(LUA_DIR)/lib
LUA_SHAREDIR = $(LUA_DIR)/share/lua/$(LUA_VERSION)

INST_PREFIX = /usr/local
INST_BINDIR = $(INST_PREFIX)/bin
INST_LIBDIR = $(INST_PREFIX)/lib/lua/$(LUA_VERSION)
INST_LUADIR = $(INST_PREFIX)/share/lua/$(LUA_VERSION)
INST_CONFDIR = $(INST_PREFIX)/etc

DEFINES =
CFLAGS = -Wall -Wextra -O2 -g -fPIC
LIBFLAG = -shared
SRC = $(wildcard csrc/*.c)
OBJ = $(SRC:.c=.o)

USE_STATIC_TREE_SITTER = 0
TREE_SITTER_DIR = /usr/local
TREE_SITTER_INCDIR = $(TREE_SITTER_DIR)/include
TREE_SITTER_LIBDIR = $(TREE_SITTER_DIR)/lib
TREE_SITTER_STATIC_LIB = $(TREE_SITTER_LIBDIR)/libtree-sitter.a

USE_LIBUV = 0
LIBUV_DIR = /usr/local
LIBUV_INCDIR = $(LIBUV_DIR)/include
LIBUV_LIBDIR = $(LIBUV_DIR)/lib

ifeq ($(USE_LIBUV), 1)
  DEFINES += -DLTREESITTER_USE_LIBUV
endif

INCLUDE += -I$(LUA_INCDIR) -I$(TREE_SITTER_INCDIR) -I./include
LDFLAGS += -ldl

all: ltreesitter.so

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE) $(DEFINES)

ifneq ($(USE_STATIC_TREE_SITTER), 1)
  LDFLAGS += -L$(TREE_SITTER_LIBDIR) -ltree-sitter
else
  OBJ += $(TREE_SITTER_STATIC_LIB)
endif

ifeq ($(USE_LIBUV), 1)
  LDFLAGS += -luv
endif

ltreesitter.so: $(OBJ)
	@echo -- Building ltreesitter.so
	@echo    CFLAGS: $(CFLAGS)
	@echo    Using static treesitter? $(USE_STATIC_TREE_SITTER)
	@echo    Using libuv? $(USE_LIBUV)
	$(CC) $(CFLAGS) $(LIBFLAG) -o $@ $(OBJ) $(LDFLAGS)

install: ltreesitter.so
	@echo -- Installing ltreesitter.so
	@echo    LUA_VERSION: $(LUA_VERSION)
	@echo    INST_PREFIX: $(INST_PREFIX)
	@echo    INST_BINDIR: $(INST_BINDIR)
	@echo    INST_LIBDIR: $(INST_LIBDIR)
	@echo    INST_LUADIR: $(INST_LUADIR)
	@echo    INST_CONFDIR: $(INST_CONFDIR)
	mkdir -p $(INST_LIBDIR)/ltreesitter
	mkdir -p $(INST_LUADIR)
	cp ltreesitter.so $(INST_LIBDIR)
	cp ltreesitter.d.tl $(INST_LUADIR)
