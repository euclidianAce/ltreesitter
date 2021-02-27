
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

void push_parent(lua_State *L, int obj_idx) {
	lua_pushvalue(L, obj_idx);
	push_object_table(L);
	lua_insert(L, -2);
	lua_gettable(L, -2);
	lua_remove(L, -2);
	if (lua_isnil(L, -1)) {
		luaL_error(L, "Internal error: object has no parent!");
	}
}

void set_parent(lua_State *L, int obj_idx, int parent_idx) {
	const int abs_obj = absindex(L, obj_idx);
	const int abs_parent = absindex(L, parent_idx);
	push_object_table(L);
	lua_pushvalue(L, abs_obj);
	lua_pushvalue(L, abs_parent);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}
