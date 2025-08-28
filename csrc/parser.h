#ifndef LTREESITTER_PARSER_H
#define LTREESITTER_PARSER_H

#include "types.h"
#include <lua.h>

def_check_assert(TSParser *, parser, LTREESITTER_PARSER_METATABLE_NAME)

// ( -- table )
void parser_init_metatable(lua_State *L);

#endif
