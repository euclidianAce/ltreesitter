#ifndef LTREESITTER_LUAUTILS_H
#define LTREESITTER_LUAUTILS_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define TODO(L) luaL_error(L, "TODO: %s %s:%d", __FUNCTION__, __FILE__, __LINE__)
#define ALLOC_FAIL(L) luaL_error(L, "%s %s:%d Memory allocation failed!", __FUNCTION__, __FILE__, __LINE__)
#define UNREACHABLE(L) luaL_error(L, "%s %s:%d Unreachable code reached!", __FUNCTION__, __FILE__, __LINE__)

char *str_ldup(const char *s, const size_t len);
void table_geti(lua_State *L, int idx, int i);
int table_rawget(lua_State *L, int idx);
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

#include <stdio.h>
#define DUMP_STACK(...) do { \
		printf("Lua Stack: "); \
		__VA_OPT__(printf(__VA_ARGS__);) \
		printf("\n"); \
		for (int i = 1; i <= lua_gettop(L); ++i) { \
			printf("   %d: ", i); \
			if (lua_tostring(L, i)) \
				printf("%s", lua_tostring(L, i)); \
			else if (lua_topointer(L, i)) \
				printf("%p", lua_topointer(L, i)); \
			printf(" (%s)\n", lua_typename(L, lua_type(L, i))); \
		} \
		printf("\n\n"); \
	} while (0)

#endif
