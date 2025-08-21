#include "luautils.h"
#include "object.h"
#include "tree.h"
#include "node.h"
#include "types.h"
#include "tree_cursor.h"

TSTreeCursor *tree_cursor_push(lua_State *L, int kept_idx, TSNode n) {
	TSTreeCursor *c = lua_newuserdata(L, sizeof(TSTreeCursor));
	bind_lifetimes(L, -1, kept_idx); // tree cursor keeps [kept_idx] alive
	*c = ts_tree_cursor_new(n);
	setmetatable(L, LTREESITTER_TREE_CURSOR_METATABLE_NAME);
	return c;
}

/* @teal-export Cursor.current_node: function(Cursor): Node [[
   Get the current node under the cursor
]] */
static int tree_cursor_current_node(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	push_kept(L, 1);
	node_push(
		L, -1,
		ts_tree_cursor_current_node(c));
	return 1;
}

/* @teal-export Cursor.current_field_name: function(Cursor): string [[
   Get the field name of the current node under the cursor
]] */
static int tree_cursor_current_field_name(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	char const *field_name = ts_tree_cursor_current_field_name(c);
	if (field_name) {
		lua_pushstring(L, field_name);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

/* @teal-export Cursor.reset: function(Cursor, Node) [[
   Position the cursor at the given node
]] */
static int tree_cursor_reset(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	TSNode n = *node_assert(L, 2);
	ts_tree_cursor_reset(c, n);
	return 0;
}

/* @teal-export Cursor.goto_parent: function(Cursor): boolean [[
   Position the cursor at the parent of the current node
]] */
static int tree_cursor_goto_parent(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_parent(c));
	return 1;
}

/* @teal-export Cursor.goto_next_sibling: function(Cursor): boolean [[
   Position the cursor at the sibling of the current node
]] */
static int tree_cursor_goto_next_sibling(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_next_sibling(c));
	return 1;
}

/* @teal-export Cursor.goto_first_child: function(Cursor): boolean [[
   Position the cursor at the first child of the current node
]] */
static int tree_cursor_goto_first_child(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_first_child(c));
	return 1;
}

/* @teal-export Cursor.goto_first_child_for_byte: function(Cursor, integer): integer [[
   Move the given cursor to the first child of its current node that contains
   or starts after the given offset

   Returns the index of the child node or nil if no such child was found
]] */
static int tree_cursor_goto_first_child_for_byte(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	uint32_t byte = luaL_checknumber(L, 2);
	int64_t idx = ts_tree_cursor_goto_first_child_for_byte(c, byte);
	if (idx == -1) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, idx);
	}
	return 1;
}

/* @teal-export Cursor.goto_first_child_for_point: function(Cursor, Point): integer [[
   Move the given cursor to the first child of its current node that contains
   or starts after the given point

   Returns the index of the child node or nil if no such child was found
]] */
//int64_t ts_tree_cursor_goto_first_child_for_point(TSTreeCursor *self, TSPoint goal_point);
static int tree_cursor_goto_first_child_for_point(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
	TSPoint goal = topoint(L, 2);
	int64_t idx = ts_tree_cursor_goto_first_child_for_point(c, goal);
	if (idx == -1) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, idx);
	}
	return 1;
}


/* @teal-export Cursor.reset_to: function(Cursor, Cursor) [[
   Re-initialize a tree cursor to the same position as another cursor.
]] */
static int tree_cursor_reset_to(lua_State *L) {
	TSTreeCursor *dst = tree_cursor_assert(L, 1);
	TSTreeCursor *src = tree_cursor_assert(L, 2);
	ts_tree_cursor_reset_to(dst, src);
	return 0;
}

/* @teal-export Cursor.goto_descendant: function(Cursor, integer) [[
   Move the cursor to the nth descendant of the original node this cursor was
   constructed with. Zero represents the original node itself.
]] */
static int tree_cursor_goto_descendant(lua_State *L) {
	TSTreeCursor *c = tree_cursor_assert(L, 1);
	lua_Integer index = luaL_checkinteger(L, 2);
    luaL_argcheck(L, index >= 0, 2, "index must be positive");
	ts_tree_cursor_goto_descendant(c, (uint32_t)index);
	return 0;
}

/* @teal-export Cursor.current_descendant_index: function(Cursor): integer [[
   Get the cursor's current node index
]] */
static int tree_cursor_current_descendant_index(lua_State *L) {
	TSTreeCursor const *c = tree_cursor_assert(L, 1);
	lua_pushinteger(L, ts_tree_cursor_current_descendant_index(c));
	return 1;
}

/* @teal-export Cursor.current_depth: function(Cursor): integer [[
   Get the depth of the cursor's current node relative to the node the cursor
   was constructed with
]] */
static int tree_cursor_current_depth(lua_State *L) {
	TSTreeCursor const *c = tree_cursor_assert(L, 1);
	lua_pushinteger(L, ts_tree_cursor_current_depth(c));
	return 1;
}

/* @teal-export Cursor.copy: function(Cursor): Cursor [[
   Create a copy of the given cursor
]] */
static int tree_cursor_copy(lua_State *L) {
	// cursor, ...
	TSTreeCursor const *src = tree_cursor_assert(L, 1);
	push_kept(L, 1); // cursor, ..., kept

	TSTreeCursor *copy = lua_newuserdata(L, sizeof(TSTreeCursor)); // cursor, ..., kept, copy
	*copy = ts_tree_cursor_copy(src);
	setmetatable(L, LTREESITTER_TREE_CURSOR_METATABLE_NAME);
	bind_lifetimes(L, -1, -2); // copy keeps original kept alive

	return 1;
}

// TODO:
//TSFieldId ts_tree_cursor_current_field_id(const TSTreeCursor *self);

static int tree_cursor_gc(lua_State *L) {
	TSTreeCursor *const c = tree_cursor_assert(L, 1);
#ifdef LOG_GC
	printf("Tree Cursor %p is being garbage collected\n", (void *)c);
#endif
	ts_tree_cursor_delete(c);
	return 0;
}

static const luaL_Reg tree_cursor_methods[] = {
	{"copy", tree_cursor_copy},
	{"current_node", tree_cursor_current_node},
	{"current_field_name", tree_cursor_current_field_name},
	{"current_descendant_index", tree_cursor_current_descendant_index},
	{"current_depth", tree_cursor_current_depth},
	{"goto_parent", tree_cursor_goto_parent},
	{"goto_first_child", tree_cursor_goto_first_child},
	{"goto_first_child_for_byte", tree_cursor_goto_first_child_for_byte},
	{"goto_first_child_for_point", tree_cursor_goto_first_child_for_point},
	{"goto_next_sibling", tree_cursor_goto_next_sibling},
	{"goto_descendant", tree_cursor_goto_descendant},
	{"reset", tree_cursor_reset},
	{"reset_to", tree_cursor_reset_to},
	{NULL, NULL}};
static const luaL_Reg tree_cursor_metamethods[] = {
	{"__gc", tree_cursor_gc},
	{NULL, NULL}};

void tree_cursor_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_TREE_CURSOR_METATABLE_NAME, tree_cursor_metamethods, tree_cursor_methods);
}
