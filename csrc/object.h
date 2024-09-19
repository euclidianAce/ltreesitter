#ifndef LTREESITTER_OBJECT_H
#define LTREESITTER_OBJECT_H

#include <lua.h>

// The object table is a table in the registry with __mode = 'k' that we use to represent ownership.
// This allows lua to garbage collect our data structures properly.
//
// The basic idea is just a table with weak keys:
//
//    object_table[parent] = child
//
// As long as `parent` lives, so does `child`

void setup_object_table(lua_State *);
void push_child(lua_State *, int obj_idx);

void setup_parser_cache(lua_State *);
void push_parser_cache(lua_State *);

// ( object -- ) -> object_table[object] = value at child_idx
void set_child(lua_State *, int child_idx);

#endif
