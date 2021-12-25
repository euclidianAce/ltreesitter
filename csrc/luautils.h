#ifndef LTREESITTER_LUAUTILS_H
#define LTREESITTER_LUAUTILS_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define TODO(L) luaL_error(L, "TODO: %s:%d", __FILE__, __LINE__)
#define ALLOC_FAIL(L) luaL_error(L, "%s:%d Memory allocation failed!", __FILE__, __LINE__)
#define UNREACHABLE(L) luaL_error(L, "%s:%d Unreachable code reached!", __FILE__, __LINE__)

char *str_ldup(const char *s, const size_t len);
void table_geti(lua_State *L, int idx, int i);
int table_rawget(lua_State *L, int idx);
void pushinteger(lua_State *L, int n);
void setfuncs(lua_State *L, const luaL_Reg l[]);
void create_libtable(lua_State *L, const luaL_Reg l[]);
void create_metatable(lua_State *L, const char *name, const luaL_Reg metamethods[], const luaL_Reg index[]);
int getfield_type(lua_State *L, int idx, const char *field_name);
bool expect_field(lua_State *L, int idx, const char *field_name, int expected_type);
bool expect_nested_field(lua_State *L, int idx, const char *parent_name, const char *field_name, int expected_type);
int absindex(lua_State *L, int idx);
void setmetatable(lua_State *L, const char *mt_name);
void setup_registry_index(lua_State *L);
int push_registry_table(lua_State *L);
void push_registry_field(lua_State *L, const char *f);
void set_registry_field(lua_State *L, const char *f);
void newtable_with_mode(lua_State *L, const char *mode);
size_t length_of(lua_State *L, int index);

#ifndef LUA_OK
#define LUA_OK 0
#endif

#endif
