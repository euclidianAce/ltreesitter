#ifndef LTREESITTER_PARSER_H
#define LTREESITTER_PARSER_H

#include "types.h"
#include <lua.h>

struct ltreesitter_Parser *ltreesitter_check_parser(lua_State *L, int);
void setup_parser_cache(lua_State *L);
int ltreesitter_load_parser(lua_State *L);
int ltreesitter_require_parser(lua_State *L);
void ltreesitter_create_parser_metatable(lua_State *L);

#endif
