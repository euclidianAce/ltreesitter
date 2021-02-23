
SRC := $(wildcard csrc/*.c)
HEADERS := $(wildcard csrc/*.h)

CFLAGS += -Wall -Wextra
INCLUDE =
LD_FLAGS = -llua -ltree-sitter

ltreesitter.so: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(SRC) $(LD_FLAGS) $(INCLUDE)
