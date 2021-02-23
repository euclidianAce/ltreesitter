#ifndef LTREESITTER_OBJECT_H
#define LTREESITTER_OBJECT_H

void setup_object_table(lua_State *L);
void push_parent(lua_State *L, int obj_idx);
void set_parent(lua_State *L, int obj_idx, int parent_idx);

#endif
