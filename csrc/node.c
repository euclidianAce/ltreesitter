
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "luautils.h"
#include "object.h"
#include <ltreesitter/node.h>
#include <ltreesitter/tree.h>
#include <ltreesitter/tree_cursor.h>
#include <ltreesitter/types.h>

#include <tree_sitter/api.h>

ltreesitter_Node *ltreesitter_check_node(lua_State *L, int idx) {
	return luaL_checkudata(L, idx, LTREESITTER_NODE_METATABLE_NAME);
}

/* @teal-export Node.type: function(Node): string [[
   Get the type of the given node
]] */
static int node_type(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	lua_pushstring(L, ts_node_type(n));
	return 1;
}

/* @teal-export Node.start_byte: function(Node): integer [[
   Get the byte of the source string that the given node starts at
]] */
static int node_start_byte(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	pushinteger(L, ts_node_start_byte(n));
	return 1;
}

/* @teal-export Node.end_byte: function(Node): integer [[
   Get the byte of the source string that the given node ends at
]] */
static int node_end_byte(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	pushinteger(L, ts_node_end_byte(n));
	return 1;
}

/* @teal-inline [[
   interface Point
      row: integer
      column: integer
   end
]]*/

/* @teal-export Node.start_point: function(Node): Point [[
   Get the row and column of where the given node starts
]] */
static int node_start_point(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	TSPoint p = ts_node_start_point(n);
	lua_newtable(L);

	pushinteger(L, p.row);
	lua_setfield(L, -2, "row");

	pushinteger(L, p.column);
	lua_setfield(L, -2, "column");

	return 1;
}

/* @teal-export Node.end_point: function(Node): Point [[
   Get the row and column of where the given node ends
]] */
static int node_end_point(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	TSPoint p = ts_node_end_point(n);
	lua_newtable(L);

	pushinteger(L, p.row);
	lua_setfield(L, -2, "row");

	pushinteger(L, p.column);
	lua_setfield(L, -2, "column");
	return 1;
}

/* @teal-export Node.is_named: function(Node): boolean [[
   Get whether or not the current node is named
]] */
static int node_is_named(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	lua_pushboolean(L, ts_node_is_named(n));
	return 1;
}

/* @teal-export Node.is_missing: function(Node): boolean [[
   Get whether or not the current node is missing
]] */
static int node_is_missing(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	lua_pushboolean(L, ts_node_is_missing(n));
	return 1;
}

/* @teal-export Node.is_extra: function(Node): boolean [[
   Get whether or not the current node is extra
]] */
static int node_is_extra(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	lua_pushboolean(L, ts_node_is_extra(n));
	return 1;
}

void ltreesitter_push_node(lua_State *L, int child_idx_, TSNode n) {
	lua_pushvalue(L, child_idx_); // child_copy
	const int child_idx = lua_gettop(L);

	ltreesitter_check_tree(L, child_idx, "Internal error: node child is not a tree");
	ltreesitter_Node *node = lua_newuserdata(L, sizeof(ltreesitter_Node)); // child_copy, node
	node->node = n;

	lua_pushvalue(L, -1); // child_copy, node, node
	set_child(L, child_idx); // child_copy, node
	setmetatable(L, LTREESITTER_NODE_METATABLE_NAME); // child_copy, node
	lua_remove(L, -2); // node
}

/* @teal-export Node.child: function(Node, idx: integer): Node [[
   Get the node's idx'th child (0-indexed)
]] */
static int node_child(lua_State *L) {
	ltreesitter_Node *parent = ltreesitter_check_node(L, 1);
	const uint32_t idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_child_count(parent->node)) {
		lua_pushnil(L);
	} else {
		push_child(L, 1);
		ltreesitter_push_node(
			L, -1,
			ts_node_child(parent->node, (uint32_t)luaL_checknumber(L, 2)));
	}
	return 1;
}

/* @teal-export Node.child_count: function(Node): integer [[
   Get the number of children a node has
]] */
static int node_child_count(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	pushinteger(L, ts_node_child_count(n));
	return 1;
}

/* @teal-export Node.named_child: function(Node, idx: integer): Node [[
   Get the node's idx'th named child (0-indexed)
]] */
static int node_named_child(lua_State *L) {
	ltreesitter_Node *parent = ltreesitter_check_node(L, 1);
	const uint32_t idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_named_child_count(parent->node)) {
		lua_pushnil(L);
	} else {
		push_child(L, 1);
		ltreesitter_push_node(L, -1, ts_node_named_child(parent->node, idx));
	}
	return 1;
}

/* @teal-export Node.named_child_count: function(Node): integer [[
   Get the number of named children a node has
]] */
static int node_named_child_count(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	pushinteger(L, ts_node_named_child_count(n));
	return 1;
}

static int node_children_iterator(lua_State *L) {
	const bool b = lua_toboolean(L, lua_upvalueindex(2));
	if (!b) {
		return 0;
	}

	lua_settop(L, 0);
	ltreesitter_TreeCursor *const c = ltreesitter_check_tree_cursor(L, lua_upvalueindex(1));

	const TSNode n = ts_tree_cursor_current_node(&c->cursor);
	push_child(L, lua_upvalueindex(1));
	ltreesitter_push_node(L, -1, n);

	lua_pushboolean(L, ts_tree_cursor_goto_next_sibling(&c->cursor));
	lua_replace(L, lua_upvalueindex(2));

	return 1;
}

static int node_named_children_iterator(lua_State *L) {
	lua_settop(L, 0);
	ltreesitter_Node *n = ltreesitter_check_node(L, lua_upvalueindex(1));

	const uint32_t idx = lua_tonumber(L, lua_upvalueindex(3));
	pushinteger(L, idx + 1);
	lua_replace(L, lua_upvalueindex(3));

	if (idx >= ts_node_named_child_count(n->node)) {
		lua_pushnil(L);
	} else {
		lua_pushvalue(L, lua_upvalueindex(2));
		ltreesitter_push_node(
			L, -1,
			ts_node_named_child(n->node, idx));
	}

	return 1;
}

/* @teal-export Node.children: function(Node): function(): Node [[
   Iterate over a node's children
]] */
static int node_children(lua_State *L) {
	lua_settop(L, 1);
	ltreesitter_Node *n = ltreesitter_check_node(L, 1);
	push_child(L, 1);
	ltreesitter_TreeCursor *const c = ltreesitter_push_tree_cursor(L, 2, n->node);
	const bool b = ts_tree_cursor_goto_first_child(&c->cursor);
	lua_pushboolean(L, b);
	lua_pushcclosure(L, node_children_iterator, 2);
	return 1;
}

/* @teal-export Node.named_children: function(Node): function(): Node [[
   Iterate over a node's named children
]] */
static int node_named_children(lua_State *L) {
	ltreesitter_check_node(L, 1);
	push_child(L, 1);
	pushinteger(L, 0);

	lua_pushcclosure(L, node_named_children_iterator, 3);
	return 1;
}

/* @teal-export Node.next_sibling: function(Node): Node [[
   Get a node's next sibling
]] */
static int node_next_sibling(lua_State *L) {
	ltreesitter_Node *const n = ltreesitter_check_node(L, 1);
	push_child(L, 1);
	TSNode sibling = ts_node_next_sibling(n->node);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	ltreesitter_push_node(L, -1, sibling);
	return 1;
}

/* @teal-export Node.prev_sibling: function(Node): Node [[
   Get a node's previous sibling
]] */
static int node_prev_sibling(lua_State *L) {
	ltreesitter_Node *const n = ltreesitter_check_node(L, 1);
	push_child(L, 1);
	TSNode sibling = ts_node_prev_sibling(n->node);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	ltreesitter_push_node(L, -1, sibling);
	return 1;
}

/* @teal-export Node.next_named_sibling: function(Node): Node [[
   Get a node's next named sibling
]] */
static int node_next_named_sibling(lua_State *L) {
	ltreesitter_Node *const n = ltreesitter_check_node(L, 1);
	push_child(L, 1);
	TSNode sibling = ts_node_next_named_sibling(n->node);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	ltreesitter_push_node(L, -1, sibling);
	return 1;
}

/* @teal-export Node.prev_named_sibling: function(Node): Node [[
   Get a node's previous named sibling
]] */
static int node_prev_named_sibling(lua_State *L) {
	ltreesitter_Node *const n = ltreesitter_check_node(L, 1);
	push_child(L, 1);
	TSNode sibling = ts_node_prev_named_sibling(n->node);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	ltreesitter_push_node(L, -1, sibling);
	return 1;
}

static int node_string(lua_State *L) {
	TSNode n = ltreesitter_check_node(L, 1)->node;
	char *s = ts_node_string(n);
	lua_pushstring(L, s);
	free(s);
	return 1;
}

static int node_eq(lua_State *L) {
	TSNode n1 = ltreesitter_check_node(L, 1)->node;
	TSNode n2 = ltreesitter_check_node(L, 2)->node;
	lua_pushboolean(L, ts_node_eq(n1, n2));
	return 1;
}

/* @teal-export Node.name: function(Node): string [[
   Returns the name of a given node
   <pre>
   print(node) -- => (comment)
   print(node:name()) -- => comment
   </pre>
]] */

static int node_name(lua_State *L) {
	ltreesitter_Node *n = ltreesitter_check_node(L, 1);
	if (ts_node_is_null(n->node) || !ts_node_is_named(n->node)) {
		lua_pushnil(L);
		return 1;
	}
	TSSymbol sym = ts_node_symbol(n->node);
	const char *name = ts_language_symbol_name(ts_tree_language(n->node.tree), sym);
	lua_pushstring(L, name);
	return 1;
}

/* @teal-export Node.child_by_field_name: function(Node, string): Node [[
   Get a node's child given a field name
]] */
static int node_child_by_field_name(lua_State *L) {
	lua_settop(L, 2);
	ltreesitter_Node *n = ltreesitter_check_node(L, 1);
	const char *name = luaL_checkstring(L, 2);

	TSNode child = ts_node_child_by_field_name(n->node, name, strlen(name));
	if (ts_node_is_null(child)) {
		lua_pushnil(L);
	} else {
		push_child(L, 1);
		ltreesitter_push_node(L, -1, child);
	}
	return 1;
}

MaybeOwnedString get_node_source(lua_State *L) { // node
	TSNode n = ltreesitter_check_node(L, -1)->node;
	push_child(L, -1); // node, tree
	ltreesitter_Tree *const t = ltreesitter_check_tree(L, -1, "Internal error: node child was not a tree");
	if (t->text_or_null_if_function_reader) {
		const uint32_t start = ts_node_start_byte(n);
		const uint32_t end = ts_node_end_byte(n);
		lua_pop(L, 1); // node
		return (MaybeOwnedString){
			.owned = false,
			.data = t->text_or_null_if_function_reader->text + start,
			.length = end - start,
		};
	}
	push_child(L, -1); // node, tree, reader
	StringBuilder sb = {0};

	const uint32_t start_byte = ts_node_start_byte(n);
	const uint32_t end_byte = ts_node_end_byte(n);
	const uint32_t expected_byte_length = end_byte - start_byte;
	uint32_t needed_bytes = expected_byte_length;
	TSPoint position = ts_node_start_point(n);

	while (needed_bytes > 0) {
		lua_pushvalue(L, -1); // ..., reader

		const uint32_t start_index = start_byte + end_byte - needed_bytes;

		pushinteger(L, start_index); // ..., reader, index
		lua_newtable(L); { // ..., reader, index, point
			pushinteger(L, position.row); lua_setfield(L, -2, "row");
			pushinteger(L, position.column); lua_setfield(L, -2, "column");
		} // ..., reader, index, point

		if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
			sb_free(&sb);
			lua_error(L);
		}

		// ..., return value

		int t = lua_type(L, -1);
		switch (t) {
		case LUA_TNIL:
			needed_bytes = 0;
			break;
		case LUA_TSTRING:
			{
				size_t len;
				char const *str = lua_tolstring(L, -1, &len);
				if (len > needed_bytes)
					len = needed_bytes;
				sb_push_lstr(&sb, len, str);
				needed_bytes -= len;

				// According to https://github.com/tree-sitter/tree-sitter/discussions/1286
				// `column` is just a byte offset
				for (size_t i = 0; i < len; ++i) {
					if (str[i] == '\n') {
						position.row += 1;
						position.column = 0;
					} else {
						position.column += 1;
					}
				}
			}
			break;
		default:
			sb_free(&sb);
			luaL_error(L, "read error: Reader function returned %s (expected string)", lua_typename(L, t));
			break;
		}
		lua_pop(L, 1);
	} // node, tree, reader
	lua_pop(L, 2); // node

	return (MaybeOwnedString){
		.owned = true,
		.length = sb.length,
		.data = sb.data,
	};
}

/* @teal-export Node.source: function(Node): string [[
   Get the substring of the source that was parsed to create <code>Node</code>
]]*/
void ltreesitter_push_node_source(lua_State *L) { // node
	MaybeOwnedString str = get_node_source(L);
	mos_push_to_lua(L, str);
	mos_free(&str);
}

int node_get_source_str(lua_State *L) {
	ltreesitter_push_node_source(L);
	return 1;
}

/* @teal-export Node.create_cursor: function(Node): Cursor [[
   Create a new cursor at the given node
]] */
static int node_tree_cursor_create(lua_State *L) {
	lua_settop(L, 1);
	ltreesitter_Node *const n = ltreesitter_check_node(L, 1);
	push_child(L, 1);
	ltreesitter_push_tree_cursor(L, 2, n->node);
	return 1;
}

static const luaL_Reg node_methods[] = {
	{"child", node_child},
	{"child_by_field_name", node_child_by_field_name},
	{"child_count", node_child_count},
	{"children", node_children},
	{"create_cursor", node_tree_cursor_create},
	{"end_byte", node_end_byte},
	{"end_point", node_end_point},
	{"is_extra", node_is_extra},
	{"is_missing", node_is_missing},
	{"is_named", node_is_named},
	{"name", node_name},
	{"named_child", node_named_child},
	{"named_child_count", node_named_child_count},
	{"named_children", node_named_children},
	{"next_named_sibling", node_next_named_sibling},
	{"next_sibling", node_next_sibling},
	{"prev_named_sibling", node_prev_named_sibling},
	{"prev_sibling", node_prev_sibling},
	{"source", node_get_source_str},
	{"start_byte", node_start_byte},
	{"start_point", node_start_point},
	{"type", node_type},
	{NULL, NULL}};
static const luaL_Reg node_metamethods[] = {
	{"__eq", node_eq},
	{"__tostring", node_string},
	{NULL, NULL}};

void ltreesitter_create_node_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_NODE_METATABLE_NAME, node_metamethods, node_methods);
}
