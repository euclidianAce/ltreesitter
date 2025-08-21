#include "object.h"
#include "luautils.h"

static const char *object_field = "objects";
// map of objects to their parents
// use when an object relies on its parent being alive
void setup_object_table(lua_State *L) {
	newtable_with_mode(L, "k");
	set_registry_field(L, object_field);
}

static void push_object_table(lua_State *L) {
	push_registry_field(L, object_field);
}

void push_kept(lua_State *L, int keeper_idx) {
	lua_pushvalue(L, keeper_idx); // keeper
	push_object_table(L);         // keeper, object_table
	lua_insert(L, -2);            // object_table, keeper
	lua_gettable(L, -2);          // object_table, object_table[keeper]
	lua_remove(L, -2);            // object_table[keeper]
	if (lua_isnil(L, -1)) {
		luaL_error(L, "Internal error: object is not a keeper!");
	}
}

void bind_lifetimes(lua_State *L, int as_long_as_this_object_lives, int so_shall_this_one) {
	as_long_as_this_object_lives = absindex(L, as_long_as_this_object_lives);
	so_shall_this_one = absindex(L, so_shall_this_one);

	push_object_table(L);                           // objtable
	lua_pushvalue(L, as_long_as_this_object_lives); // objtable, keeper
	lua_pushvalue(L, so_shall_this_one);            // objtable, keeper, kept
	lua_rawset(L, -3);                              // objtable
	lua_pop(L, 1);
}

static const char *parser_cache_index = "parsers";
void setup_parser_cache(lua_State *L) {
	newtable_with_mode(L, "v");
	set_registry_field(L, parser_cache_index);
}
void push_parser_cache(lua_State *L) {
	push_registry_field(L, parser_cache_index);
}
