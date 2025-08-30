
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "luautils.h"
#include "node.h"
#include "object.h"
#include "tree.h"
#include "tree_cursor.h"
#include "types.h"

#include <tree_sitter/api.h>

#define internal_err "ltreesitter internal error: node kept object is not a tree"

// ( [node_idx]=any | -- tree )
ltreesitter_Tree *node_push_tree(lua_State *L, int node_idx) {
	push_kept(L, node_idx);
	ltreesitter_Tree *const tree = tree_check(L, -1);
	if (!tree)
		luaL_error(L, internal_err);
	return tree;
}

/* @teal-export Node.type: function(Node): string [[
   Get the type of the given node
]] */
static int node_type(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	lua_pushstring(L, ts_node_type(n));
	return 1;
}

/* @teal-export Node.grammar_type: function(Node): string [[
   Returns the type of a given node as a string as it appears in the grammar ignoring aliases
]] */
static int node_grammar_type(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	lua_pushstring(L, ts_node_grammar_type(n));
	return 1;
}

/* @teal-export Node.start_byte_offset: function(Node): integer [[
   Get the byte offset of the source string that the given node starts at
]] */
static int node_start_byte(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	pushinteger(L, ts_node_start_byte(n));
	return 1;
}

/* @teal-export Node.start_index: function(Node): integer [[
   Get the inclusive 1-index of the source string that the given node starts at
]] */
static int node_start_index(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	pushinteger(L, ts_node_start_byte(n) + 1);
	return 1;
}

/* @teal-export Node.end_byte_offset: function(Node): integer [[
   Get the byte offset of the source string that the given node ends at (exclusive)
]] */
/* @teal-export Node.end_index: function(Node): integer [[
   Get the inclusive 1-index of the source string that the given node ends at
]] */
static int node_end_byte(lua_State *L) {
	TSNode n = *node_assert(L, 1);
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
	TSNode n = *node_assert(L, 1);
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
	TSNode n = *node_assert(L, 1);
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
	TSNode n = *node_assert(L, 1);
	lua_pushboolean(L, ts_node_is_named(n));
	return 1;
}

/* @teal-export Node.is_missing: function(Node): boolean [[
   Get whether or not the current node is missing
]] */
static int node_is_missing(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	lua_pushboolean(L, ts_node_is_missing(n));
	return 1;
}

/* @teal-export Node.is_extra: function(Node): boolean [[
   Get whether or not the current node is extra
]] */
static int node_is_extra(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	lua_pushboolean(L, ts_node_is_extra(n));
	return 1;
}

void node_push(lua_State *L, int tree_idx, TSNode n) {
	lua_pushvalue(L, tree_idx); // tree
	tree_idx = lua_gettop(L);

	if (!tree_check(L, tree_idx))
		luaL_error(L, internal_err);
	TSNode *node = lua_newuserdata(L, sizeof(TSNode)); // tree, node
	*node = n;
	setmetatable(L, LTREESITTER_NODE_METATABLE_NAME); // tree, node
	bind_lifetimes(L, -1, tree_idx);                  // node keeps tree alive
	lua_remove(L, -2);                                // node
}

/* @teal-export Node.child: function(Node, idx: integer): Node [[
   Get the node's idx'th child (0-indexed)
]] */
static int node_child(lua_State *L) {
	TSNode *parent = node_assert(L, 1);
	uint32_t const idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_child_count(*parent)) {
		lua_pushnil(L);
	} else {
		push_kept(L, 1);
		node_push(L, -1, ts_node_child(*parent, idx));
	}
	return 1;
}

/* @teal-export Node.child_count: function(Node): integer [[
   Get the number of children a node has
]] */
static int node_child_count(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	pushinteger(L, ts_node_child_count(n));
	return 1;
}

/* @teal-export Node.named_child: function(Node, idx: integer): Node [[
   Get the node's idx'th named child (0-indexed)
]] */
static int node_named_child(lua_State *L) {
	TSNode *parent = node_assert(L, 1);
	uint32_t const idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_named_child_count(*parent)) {
		lua_pushnil(L);
	} else {
		push_kept(L, 1);
		node_push(L, -1, ts_node_named_child(*parent, idx));
	}
	return 1;
}

/* @teal-export Node.named_child_count: function(Node): integer [[
   Get the number of named children a node has
]] */
static int node_named_child_count(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	pushinteger(L, ts_node_named_child_count(n));
	return 1;
}

static int node_children_iterator(lua_State *L) {
	bool const b = lua_toboolean(L, lua_upvalueindex(2));
	if (!b) {
		return 0;
	}

	lua_settop(L, 0);
	TSTreeCursor *const c = tree_cursor_check(L, lua_upvalueindex(1));

	TSNode const n = ts_tree_cursor_current_node(c);
	push_kept(L, lua_upvalueindex(1));
	node_push(L, -1, n);

	lua_pushboolean(L, ts_tree_cursor_goto_next_sibling(c));
	lua_replace(L, lua_upvalueindex(2));

	return 1;
}

static int node_named_children_iterator(lua_State *L) {
	lua_settop(L, 0);
	TSNode *n = node_assert(L, lua_upvalueindex(1));

	uint32_t const idx = lua_tonumber(L, lua_upvalueindex(3));
	pushinteger(L, idx + 1);
	lua_replace(L, lua_upvalueindex(3));

	if (idx >= ts_node_named_child_count(*n)) {
		lua_pushnil(L);
	} else {
		lua_pushvalue(L, lua_upvalueindex(2));
		node_push(
			L, -1,
			ts_node_named_child(*n, idx));
	}

	return 1;
}

/* @teal-export Node.children: function(Node): function(): Node [[
   Iterate over a node's children
]] */
static int node_children(lua_State *L) {
	lua_settop(L, 1);
	TSNode *n = node_assert(L, 1);
	push_kept(L, 1);
	TSTreeCursor *const c = tree_cursor_push(L, 2, *n);
	bool const b = ts_tree_cursor_goto_first_child(c);
	lua_pushboolean(L, b);
	lua_pushcclosure(L, node_children_iterator, 2);
	return 1;
}

/* @teal-export Node.named_children: function(Node): function(): Node [[
   Iterate over a node's named children
]] */
static int node_named_children(lua_State *L) {
	node_assert(L, 1);
	push_kept(L, 1);
	pushinteger(L, 0);

	lua_pushcclosure(L, node_named_children_iterator, 3);
	return 1;
}

/* @teal-export Node.next_sibling: function(Node): Node [[
   Get a node's next sibling
]] */
static int node_next_sibling(lua_State *L) {
	TSNode *const n = node_assert(L, 1);
	push_kept(L, 1);
	TSNode sibling = ts_node_next_sibling(*n);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	node_push(L, -1, sibling);
	return 1;
}

/* @teal-export Node.prev_sibling: function(Node): Node [[
   Get a node's previous sibling
]] */
static int node_prev_sibling(lua_State *L) {
	TSNode *const n = node_assert(L, 1);
	push_kept(L, 1);
	TSNode sibling = ts_node_prev_sibling(*n);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	node_push(L, -1, sibling);
	return 1;
}

/* @teal-export Node.next_named_sibling: function(Node): Node [[
   Get a node's next named sibling
]] */
static int node_next_named_sibling(lua_State *L) {
	TSNode *const n = node_assert(L, 1);
	push_kept(L, 1);
	TSNode sibling = ts_node_next_named_sibling(*n);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	node_push(L, -1, sibling);
	return 1;
}

/* @teal-export Node.prev_named_sibling: function(Node): Node [[
   Get a node's previous named sibling
]] */
static int node_prev_named_sibling(lua_State *L) {
	TSNode *const n = node_assert(L, 1);
	push_kept(L, 1);
	TSNode sibling = ts_node_prev_named_sibling(*n);
	if (ts_node_is_null(sibling)) {
		lua_pushnil(L);
		return 1;
	}
	node_push(L, -1, sibling);
	return 1;
}

static int node_string(lua_State *L) {
	TSNode n = *node_assert(L, 1);
	char *s = ts_node_string(n);
	lua_pushstring(L, s);
	free(s);
	return 1;
}

static int node_eq(lua_State *L) {
	TSNode n1 = *node_assert(L, 1);
	TSNode n2 = *node_assert(L, 2);
	lua_pushboolean(L, ts_node_eq(n1, n2));
	return 1;
}

/* @teal-export Node.name: function(Node): string [[
   Returns the type of a given node as a string
   <pre>
   print(node) -- => (comment)
   print(node:name()) -- => comment
   </pre>
]] */
static int node_name(lua_State *L) {
	TSNode *n = node_assert(L, 1);
	if (ts_node_is_null(*n) || !ts_node_is_named(*n)) {
		lua_pushnil(L);
		return 1;
	}
	TSSymbol sym = ts_node_symbol(*n);
	char const *name = ts_language_symbol_name(ts_tree_language(n->tree), sym);
	lua_pushstring(L, name);
	return 1;
}

/* @teal-export Node.symbol: function(Node): Symbol [[
   Returns the type of a given node as a numeric id
]] */
static int node_symbol(lua_State *L) {
	TSNode *n = node_assert(L, 1);
	if (ts_node_is_null(*n)) {
		lua_pushnil(L);
		return 1;
	}
	TSSymbol sym = ts_node_symbol(*n);
	lua_pushinteger(L, sym);
	return 1;
}

/* @teal-export Node.grammar_symbol: function(Node): Symbol [[
   Returns the type of a given node as a numeric id as it appears in the grammar ignoring aliases

   This is what should be used in `Parser:language_next_state` instead of `Node:symbol`
]] */
static int node_grammar_symbol(lua_State *L) {
	TSNode *n = node_assert(L, 1);
	if (ts_node_is_null(*n)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, ts_node_grammar_symbol(*n));
	return 1;
}

/* @teal-export Node.child_by_field_name: function(Node, string): Node [[
   Get a node's child given a field name
]] */
static int node_child_by_field_name(lua_State *L) {
	lua_settop(L, 2);
	TSNode *n = node_assert(L, 1);
	char const *name = luaL_checkstring(L, 2);

	TSNode child = ts_node_child_by_field_name(*n, name, strlen(name));
	if (ts_node_is_null(child)) {
		lua_pushnil(L);
	} else {
		push_kept(L, 1);
		node_push(L, -1, child);
	}
	return 1;
}

/* @teal-export Node.child_by_field_id: function(Node, FieldId): Node [[
   Get a node's child given a field id
]] */
static int node_child_by_field_id(lua_State *L) {
	lua_settop(L, 2);
	TSNode *n = node_assert(L, 1);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a FieldId)");

	TSNode child = ts_node_child_by_field_id(*n, (TSFieldId)id);
	if (ts_node_is_null(child)) {
		lua_pushnil(L);
	} else {
		push_kept(L, 1);
		node_push(L, -1, child);
	}
	return 1;
}

MaybeOwnedString node_get_source(lua_State *L) { // node
	TSNode n = *node_assert(L, -1);
	ltreesitter_Tree *const tree = node_push_tree(L, -1); // node, tree
	if (tree->text_or_null_if_function_reader) {
		uint32_t const start = ts_node_start_byte(n);
		uint32_t const end = ts_node_end_byte(n);
		lua_pop(L, 1); // node
		return (MaybeOwnedString){
			.owned = false,
			.data = tree->text_or_null_if_function_reader->text + start,
			.length = end - start,
		};
	}
	push_kept(L, -1); // node, tree, reader

	uint32_t const start_byte = ts_node_start_byte(n);
	uint32_t const end_byte = ts_node_end_byte(n);
	uint32_t const expected_byte_length = end_byte - start_byte;
	uint32_t needed_bytes = expected_byte_length;
	TSPoint position = ts_node_start_point(n);

	StringBuilder sb = {0};
	if (!sb_ensure_cap(&sb, expected_byte_length))
		ALLOC_FAIL(L);

	while (needed_bytes > 0) {
		lua_pushvalue(L, -1); // ..., reader

		uint32_t const start_index = start_byte + end_byte - needed_bytes;

		pushinteger(L, start_index); // ..., reader, index
		lua_newtable(L);
		{ // ..., reader, index, point
			pushinteger(L, position.row);
			lua_setfield(L, -2, "row");
			pushinteger(L, position.column);
			lua_setfield(L, -2, "column");
		} // ..., reader, index, point

		if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
			sb_free(&sb);
			lua_error(L);
		}

		// ..., return value

		int ret_type = lua_type(L, -1);
		switch (ret_type) {
		case LUA_TNIL:
			needed_bytes = 0;
			break;
		case LUA_TSTRING: {
			size_t len;
			char const *str = lua_tolstring(L, -1, &len);
			if (len > needed_bytes)
				len = needed_bytes;
			sb_push_lstr(&sb, len, str);
			needed_bytes -= len;

			// According to https://github.com/tree-sitter/tree-sitter/discussions/1286
			// `column` is just a byte offset
			//
			// TODO: what about utf-16?
			for (size_t i = 0; i < len; ++i) {
				if (str[i] == '\n') {
					position.row += 1;
					position.column = 0;
				} else {
					position.column += 1;
				}
			}
		} break;
		default:
			sb_free(&sb);
			luaL_error(L, "Reader function returned %s (expected string)", lua_typename(L, ret_type));
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
static int node_get_source_method(lua_State *L) {
	MaybeOwnedString str = node_get_source(L);
	mos_push_to_lua(L, str);
	mos_free(&str);
	return 1;
}

/* @teal-export Node.create_cursor: function(Node): Cursor [[
   Create a new cursor at the given node
]] */
static int node_tree_cursor_create(lua_State *L) {
	lua_settop(L, 1);
	TSNode *const n = node_assert(L, 1);
	push_kept(L, 1);
	tree_cursor_push(L, 2, *n);
	return 1;
}

/* @teal-export Node.parse_state: function(Node): StateId [[ Get this node's parse state ]] */
static int node_parse_state(lua_State *L) {
	TSNode *n = node_assert(L, 1);
	lua_pushinteger(L, ts_node_parse_state(*n));
	return 1;
}

/* @teal-export Node.next_parse_state: function(Node): StateId [[ Get the parse state after this node ]] */
static int node_next_parse_state(lua_State *L) {
	TSNode *n = node_assert(L, 1);
	lua_pushinteger(L, ts_node_next_parse_state(*n));
	return 1;
}

static const luaL_Reg node_methods[] = {
	{"child", node_child},
	{"child_by_field_name", node_child_by_field_name},
	{"child_by_field_id", node_child_by_field_id},
	{"child_count", node_child_count},
	{"children", node_children},
	{"create_cursor", node_tree_cursor_create},
	{"end_index", node_end_byte},
	{"end_byte_offset", node_end_byte},
	{"end_point", node_end_point},
	{"is_extra", node_is_extra},
	{"is_missing", node_is_missing},
	{"is_named", node_is_named},
	{"name", node_name},
	{"symbol", node_symbol},
	{"grammar_symbol", node_grammar_symbol},
	{"named_child", node_named_child},
	{"named_child_count", node_named_child_count},
	{"named_children", node_named_children},
	{"next_named_sibling", node_next_named_sibling},
	{"next_sibling", node_next_sibling},
	{"prev_named_sibling", node_prev_named_sibling},
	{"prev_sibling", node_prev_sibling},
	{"source", node_get_source_method},
	{"start_index", node_start_index},
	{"start_byte_offset", node_start_byte},
	{"start_point", node_start_point},
	{"type", node_type},
	{"grammar_type", node_grammar_type},

	{"parse_state", node_parse_state},
	{"next_parse_state", node_next_parse_state},

	{NULL, NULL}};
static const luaL_Reg node_metamethods[] = {
	{"__eq", node_eq},
	{"__tostring", node_string},
	{NULL, NULL}};

void node_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_NODE_METATABLE_NAME, node_metamethods, node_methods);
}
