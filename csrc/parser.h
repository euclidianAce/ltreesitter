#ifndef LTREESITTER_PARSER_H
#define LTREESITTER_PARSER_H

#include "types.h"
#include <lua.h>

ltreesitter_Parser *parser_check(lua_State *L, int);

// ( string ?string -- parser string )
int parser_load(lua_State *L);

// ( string ?string -- parser string )
int parser_require(lua_State *L);

// ( -- table )
void parser_init_metatable(lua_State *L);

#endif

