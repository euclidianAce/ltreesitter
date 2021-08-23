
#include <ltreesitter/luautils.h>
#include <ltreesitter/node.h>
#include <ltreesitter/tree.h>
#include <ltreesitter/object.h>
#include <ltreesitter/types.h>

struct ltreesitter_TreeCursor *ltreesitter_check_tree_cursor(lua_State *L, int idx) {
	return luaL_checkudata(L, idx, LTREESITTER_TREE_CURSOR_METATABLE_NAME);
}

struct ltreesitter_TreeCursor *ltreesitter_push_tree_cursor(lua_State *L, int parent_idx, const TSLanguage *lang, TSNode n) {
	struct ltreesitter_TreeCursor *c = lua_newuserdata(L, sizeof(struct ltreesitter_TreeCursor));
	lua_pushvalue(L, -1); set_parent(L, parent_idx);
	c->cursor = ts_tree_cursor_new(n);
	c->lang = lang;
	setmetatable(L, LTREESITTER_TREE_CURSOR_METATABLE_NAME);
	return c;
}

/* @teal-export Cursor.current_node: function(Cursor): Node [[
   Get the current node under the cursor
]] */
static int tree_cursor_current_node(lua_State *L) {
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
	push_parent(L, 1);
	push_node(
		L, -1,
		ts_tree_cursor_current_node(&c->cursor),
		c->lang
	);
	return 1;
}

/* @teal-export Cursor.current_field_name: function(Cursor): string [[
   Get the field name of the current node under the cursor
]] */
static int tree_cursor_current_field_name(lua_State *L) {
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
	const char *field_name = ts_tree_cursor_current_field_name(&c->cursor);
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
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
	TSNode n = ltreesitter_check_node(L, 2)->node;
	ts_tree_cursor_reset(&c->cursor, n);
	return 0;
}

/* @teal-export Cursor.goto_parent: function(Cursor): boolean [[
   Position the cursor at the parent of the current node
]] */
static int tree_cursor_goto_parent(lua_State *L) {
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_parent(&c->cursor));
	return 1;
}

/* @teal-export Cursor.goto_next_sibling: function(Cursor): boolean [[
   Position the cursor at the sibling of the current node
]] */
static int tree_cursor_goto_next_sibling(lua_State *L) {
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_next_sibling(&c->cursor));
	return 1;
}

/* @teal-export Cursor.goto_first_child: function(Cursor): boolean [[
   Position the cursor at the first child of the current node
]] */
static int tree_cursor_goto_first_child(lua_State *L) {
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_first_child(&c->cursor));
	return 1;
}

/* @teal-export Cursor.goto_first_child_for_byte: function(Cursor, integer): integer [[
   Move the given cursor to the first child of its current node that extends
   beyond the given byte offset.

   Returns the index of the found node, if a node wasn't found, returns nil
]] */
static int tree_cursor_goto_first_child_for_byte(lua_State *L) {
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
	uint32_t byte = luaL_checknumber(L, 2);
	int64_t idx = ts_tree_cursor_goto_first_child_for_byte(&c->cursor, byte);
	if (idx == -1) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, idx);
	}
	return 1;
}

static int tree_cursor_gc(lua_State *L) {
	struct ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, 1);
#ifdef LOG_GC
	printf("Tree Cursor %p is being garbage collected\n", c);
#endif
	ts_tree_cursor_delete(&c->cursor);
	return 0;
}

static const luaL_Reg tree_cursor_methods[] = {
	{"current_node", tree_cursor_current_node},
	{"current_field_name", tree_cursor_current_field_name},
	{"goto_parent", tree_cursor_goto_parent},
	{"goto_first_child", tree_cursor_goto_first_child},
	{"goto_first_child_for_byte", tree_cursor_goto_first_child_for_byte},
	{"goto_next_sibling", tree_cursor_goto_next_sibling},
	{"reset", tree_cursor_reset},
	{NULL, NULL}
};
static const luaL_Reg tree_cursor_metamethods[] = {
	{"__gc", tree_cursor_gc},
	{NULL, NULL}
};

void ltreesitter_create_tree_cursor_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_TREE_CURSOR_METATABLE_NAME, tree_cursor_metamethods, tree_cursor_methods);
}
