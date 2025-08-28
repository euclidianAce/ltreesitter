#ifndef LTREESITTER_LUAUTILS_H
#define LTREESITTER_LUAUTILS_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TODO(L) luaL_error(L, "TODO: %s:%d", __FILE__, __LINE__)
#define ALLOC_FAIL(L) luaL_error(L, "%s:%d Memory allocation failed!", __FILE__, __LINE__)
#define UNREACHABLE(L) luaL_error(L, "%s:%d Unreachable code reached!", __FILE__, __LINE__)

typedef struct {
	char *data;
	// tree-sitter usually works within u32s
	uint32_t length, capacity;
} StringBuilder;

bool sb_ensure_cap(StringBuilder *sb, size_t n);
bool sb_push_char(StringBuilder *sb, char);
bool sb_push_str(StringBuilder *sb, char const *str);
bool sb_push_lstr(StringBuilder *sb, size_t len, char const *str);
bool sb_push_fmt(StringBuilder *sb, char const *fmt, ...);
void sb_push_to_lua(lua_State *L, StringBuilder *sb);
void sb_free(StringBuilder *sb);

#define sb_push_lit(sb, lit) \
	sb_push_lstr((sb), sizeof(""lit""), (""lit""))

typedef struct {
	char const *data;
	uint32_t length;
	bool owned; // when true, owner is responsible for `free`ing data
} MaybeOwnedString;

void mos_push_to_lua(lua_State *, MaybeOwnedString);
void mos_free(MaybeOwnedString *);
bool mos_eq(MaybeOwnedString, MaybeOwnedString);

char *str_ldup(char const *s, const size_t len);

// ( {T} -- {T} T )
void table_geti(lua_State *L, int idx, int i);

// ( {T} -- {T} T )
int table_rawget(lua_State *L, int idx);

// ( -- int )
void pushinteger(lua_State *L, int n);

// ( table -- table )
void setfuncs(lua_State *L, const luaL_Reg l[]);

// ( -- table )
void create_libtable(lua_State *L, const luaL_Reg *l);

// ( -- table )
void create_metatable(lua_State *L, char const *name, const luaL_Reg *metamethods, const luaL_Reg *index);

int getfield_type(lua_State *L, int idx, char const *field_name);
bool expect_field(lua_State *L, int idx, char const *field_name, int expected_type);
bool expect_nested_field(lua_State *L, int idx, char const *parent_name, char const *field_name, int expected_type);
int absindex(lua_State *L, int idx);

// ( T -- T )
void setmetatable(lua_State *L, char const *mt_name);

void setup_registry_index(lua_State *L);
int push_registry_table(lua_State *L);
void push_registry_field(lua_State *L, char const *f);
void set_registry_field(lua_State *L, char const *f);
void newtable_with_mode(lua_State *L, char const *mode);
size_t length_of(lua_State *L, int index);

bool push_ref_from_registry(lua_State *, int ref);
int ref_into_registry(lua_State *, int object_to_ref);
void unref_from_registry(lua_State *, int ref);

// ( [idx]=any -- )
void *testudata(lua_State *, int idx, char const *);

#if LUA_VERSION_NUM > 501
void dump_stack(lua_State *L, int from);
#endif

#ifndef LUA_OK
#define LUA_OK 0
#endif

#endif
