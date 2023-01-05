
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
	lua_pushvalue(L, obj_idx); // object_copy
	push_object_table(L);      // object_copy, object_table
	lua_rotate(L, -2, 1);      // object_table, object_copy
	lua_gettable(L, -2);       // object_table, object_table[object_copy]
	lua_remove(L, -2);         // object_table[object_copy]
	if (lua_isnil(L, -1)) {
		luaL_error(L, "Internal error: object has no parent!");
	}
}

// sets the parent of an object at the top of the stack
// pops the object off of the stack
void set_parent(lua_State *L, int parent_idx) {
	// object
	lua_pushvalue(L, parent_idx); // object, parent
	lua_insert(L, -2);            // parent, object
	push_object_table(L);         // parent, object, object_table
	lua_insert(L, -2);            // parent, object_table, object
	lua_rotate(L, -3, -1);        // object_table, object, parent
	lua_rawset(L, -3);            // object_table
	lua_pop(L, 1);
}
