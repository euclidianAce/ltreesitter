# This makefile is autogenerated by scripts/makefilegen.tl
# The only real autogenerated parts are each c file's dependencies, so editing it by hand is probably fine

# luarocks will override all of these so the running lua version wont matter in that case
LUA_VERSION = $(shell lua -e 'print(_VERSION:match("%d.%d"))')
LUA_DIR = /usr/local
LUA_INCDIR = $(LUA_DIR)/include/lua/$(LUA_VERSION)
LUA_LIBDIR = $(LUA_DIR)/lib/lua/$(LUA_VERSION)
LUA_SHAREDIR = $(LUA_DIR)/share/lua/$(LUA_VERSION)

INST_PREFIX = /usr/local
INST_BINDIR = $(INST_PREFIX)/bin
INST_LIBDIR = $(INST_PREFIX)/lib/lua/$(LUA_VERSION)
INST_LUADIR = $(INST_PREFIX)/share/lua/$(LUA_VERSION)
INST_CONFDIR = $(INST_PREFIX)/etc

CFLAGS = -Wall -Wextra -O2 -g -fPIC
LIBFLAG = -shared
OBJ =

USE_STATIC_TREESITTER = 0
TREE_SITTER_DIR = /usr/local
TREE_SITTER_INCDIR = $(TREE_SITTER_DIR)/include
TREE_SITTER_LIBDIR = $(TREE_SITTER_DIR)/lib
TREE_SITTER_STATIC_LIB = $(TREE_SITTER_LIBDIR)/libtree-sitter.a

USE_LIBUV = 0
LIBUV_DIR = /usr/local
LIBUV_INCDIR = $(LIBUV_DIR)/include
LIBUV_LIBDIR = $(LIBUV_DIR)/lib

INCLUDE += -I$(LUA_INCDIR) -I$(TREE_SITTER_INCDIR) -I./include
LDFLAGS += -L$(LUA_LIBDIR) -ldl -llua

all: ltreesitter.so

OBJ += tree_cursor.o
tree_cursor.o: csrc/tree_cursor.c csrc/luautils.c csrc/node.c csrc/tree.c csrc/object.c csrc/types.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += node.o
node.o: csrc/node.c csrc/luautils.c csrc/object.c csrc/types.c csrc/node.c csrc/tree.c csrc/tree_cursor.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += tree.o
tree.o: csrc/tree.c csrc/object.c csrc/node.c csrc/types.c csrc/luautils.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += query.o
query.o: csrc/query.c csrc/luautils.c csrc/node.c csrc/types.c csrc/object.c csrc/parser.c csrc/query_cursor.c csrc/tree.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += types.o
types.o: csrc/types.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += parser.o
parser.o: csrc/parser.c csrc/types.c csrc/tree.c csrc/luautils.c csrc/dynamiclib.c csrc/query.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += query_cursor.o
query_cursor.o: csrc/query_cursor.c csrc/luautils.c csrc/types.c csrc/object.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += luautils.o
luautils.o: csrc/luautils.c csrc/luautils.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += ltreesitter.o
ltreesitter.o: csrc/ltreesitter.c csrc/dynamiclib.c csrc/luautils.c csrc/node.c csrc/object.c csrc/parser.c csrc/tree.c csrc/tree_cursor.c csrc/query.c csrc/query_cursor.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += dynamiclib.o
dynamiclib.o: csrc/dynamiclib.c csrc/dynamiclib.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)
OBJ += object.o
object.o: csrc/object.c csrc/luautils.c
	$(CC) $(CFLAGS) -c $< -o $@ $(INCLUDE)

ifeq ($(USE_STATIC_TREESITTER), 0)
  LDFLAGS += -L$(TREE_SITTER_LIBDIR) -ltree-sitter
else
  OBJ += $(TREESITTER_STATIC_LIB)
endif

ifeq ($(USE_LIBUV), 1)
  LDFLAGS += -luv
endif

ltreesitter.so: $(OBJ)
	@echo -- Building ltreesitter.so
	@echo CFLAGS: $(CFLAGS)
	@echo Using static treesitter? $(USE_STATIC_TREESITTER)
	@echo Using libuv? $(USE_LIBUV)
	$(CC) $(CFLAGS) $(LIBFLAG) -o $@ $(OBJ) $(LDFLAGS)

install: ltreesitter.so
	@echo -- Installing ltreesitter.so
	@echo LUA_VERSION: $(LUA_VERSION)
	@echo INST_PREFIX: $(INST_PREFIX)
	@echo INST_BINDIR: $(INST_BINDIR)
	@echo INST_LIBDIR: $(INST_LIBDIR)
	@echo INST_LUADIR: $(INST_LUADIR)
	@echo INST_CONFDIR: $(INST_CONFDIR)
	mkdir -p $(INST_LIBDIR)/ltreesitter
	mkdir -p $(INST_LUADIR)
	cp ltreesitter.so $(INST_LIBDIR)
	cp ltreesitter.d.tl $(INST_LUADIR)
