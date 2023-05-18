
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

void push_child(lua_State *L, int obj_idx) {
	lua_pushvalue(L, obj_idx); // object_copy
	push_object_table(L);      // object_copy, object_table
	lua_insert(L, -2);         // object_table, object_copy
	lua_gettable(L, -2);       // object_table, object_table[object_copy]
	lua_remove(L, -2);         // object_table[object_copy]
	if (lua_isnil(L, -1)) {
		luaL_error(L, "Internal error: object has no parent!");
	}
}

// sets the parent of an object at the top of the stack
// pops the object off of the stack
// ( object -- ) -> object_table[object] = value at child_idx
void set_child(lua_State *L, int child_idx) {
	// object
	lua_pushvalue(L, child_idx); // object, child
	push_object_table(L);        // object, child, object_table
	lua_insert(L, -3);           // object_table, object, child
	lua_rawset(L, -3);           // object_table
	lua_pop(L, 1);
}
