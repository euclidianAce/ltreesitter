#ifndef LTREESITTER_OBJECT_H
#define LTREESITTER_OBJECT_H

#include <lua.h>

void setup_object_table(lua_State *L);
void push_parent(lua_State *L, int obj_idx);
void set_parent(lua_State *L, int parent_idx);

#endif
