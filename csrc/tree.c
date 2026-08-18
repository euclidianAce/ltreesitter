#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <tree_sitter/api.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "luautils.h"
#include "language.h"
#include "node.h"
#include "object.h"
#include "tree.h"
#include "types.h"

static TSTree **push_uninitialized_tree(lua_State *L) {
	TSTree **tree = lua_newuserdata(L, sizeof *tree);
	setmetatable(L, LTREESITTER_TREE_METATABLE_NAME);
	return tree;
}

// src will be duplicated
void tree_push(
	lua_State *L,
	TSTree *t,
	size_t src_len,
	char const *src) {
	(void)source_text_push(L, src_len, src); // source text
	TSTree **tree = push_uninitialized_tree(L);    // source text, tree
	*tree = t;
	bind_lifetimes(L, -1, -2); // tree keeps source text alive
	lua_remove(L, -2);         // tree

	// fprintf(stderr, "Created tree %p with source %p\n", (void*)tree, (void*)tree->source);
}

void tree_push_with_reader(
	lua_State *L,
	TSTree *t,
	int reader_function_index) {
	lua_pushvalue(L, reader_function_index);             // reader
	TSTree **tree = push_uninitialized_tree(L); // reader, tree
	*tree = t;

	bind_lifetimes(L, -1, -2); // tree keeps reader alive
	lua_remove(L, -2);         // tree
}

// @teal-export Tree.root: function(Tree): Node [[
//   Returns the root node of the given parse tree
// ]]
static int tree_push_root(lua_State *L) {
	TSTree *const t = *tree_assert(L, 1);
	node_push(L, 1, ts_tree_root_node(t));
	return 1;
}

// @teal-export Tree.root_with_offset: function(Tree, offset_bytes: integer, offset_extent: Point): Node [[
//   Returns the root node of the given parse tree, but with its position shifted
//   forward
// ]]
static int tree_push_root_with_offset(lua_State *L) {
	TSTree *const t = *tree_assert(L, 1);
	luaL_argcheck(L, lua_type(L, 2) == LUA_TNUMBER, 2, "expected integer");
	uint32_t offset_bytes = lua_tointeger(L, 2);
	TSPoint offset_extent = to_clamped_point(L, 3);
	node_push(L, 1, ts_tree_root_node_with_offset(t, offset_bytes, offset_extent));
	return 1;
}

static int tree_to_string(lua_State *L) {
	TSTree *t = *tree_assert(L, 1);
	TSNode const root = ts_tree_root_node(t);
	char *s = ts_node_string(root);
	lua_pushlstring(L, (char const *)s, strlen(s));
	free(s);
	return 1;
}

// @teal-export Tree.copy: function(Tree): Tree [[
//   Creates a copy of the tree. Tree-sitter recommends to create copies if you are going to use multithreading since tree accesses are not thread-safe, but copying them is cheap and quick
// ]]
static int tree_copy(lua_State *L) {
	lua_settop(L, 1);
	TSTree *t = *tree_assert(L, 1); // tree
	push_kept(L, 1);                         // tree, source text/reader
	TSTree **t_copy = push_uninitialized_tree(L); // tree, source text/reader, new tree
	*t_copy = ts_tree_copy(t);
	bind_lifetimes(L, -1, -2); // tree keeps source text/reader alive
	return 1;
}

// Maybe make this Edit?
// @teal-inline [[
//   interface Edit
//      start_byte: integer
//      old_end_byte: integer
//      new_end_byte: integer
//
//      start_point: Point
//      old_end_point: Point
//      new_end_point: Point
//   end
// ]]

// @teal-export Tree.edit_s: function(Tree, Edit) [[
//    Create an edit to the given tree
// ]]
static int tree_edit_s(lua_State *L) {
	lua_settop(L, 2);
	TSTree *t = *tree_assert(L, 1);
	TSInputEdit edit = expect_edit_table_arg(L, 2);
	ts_tree_edit(t, &edit);
	return 0;
}

// @teal-export Tree.edit_p: function(
//    Tree,
//    start_byte: integer,
//    old_end_byte: integer,
//    new_end_byte: integer,
//    start_point_row: integer,
//    start_point_col: integer,
//    old_end_point_row: integer,
//    old_end_point_col: integer,
//    new_end_point_row: integer,
//    new_end_point_col: integer
// ) [[
//   Create an edit to the given tree
// ]]
static int tree_edit_p(lua_State *L) {
	TSTree *t = *tree_assert(L, 1);
	TSInputEdit edit = expect_edit_positional_args(L, 2);
	ts_tree_edit(t, &edit);
	return 0;
}

// @teal-export Tree.edit: function(
//    Tree,
//    start_byte: integer,
//    old_end_byte: integer,
//    new_end_byte: integer,
//    start_point_row: integer,
//    start_point_col: integer,
//    old_end_point_row: integer,
//    old_end_point_col: integer,
//    new_end_point_row: integer,
//    new_end_point_col: integer
// ) & function(Tree, Edit) [[
//   Create an edit to the given tree
// ]]
static int tree_edit(lua_State *L) {
	(void)tree_assert(L, 1);
	if (lua_isnumber(L, 2))
		tree_edit_p(L);
	else
		tree_edit_s(L);
	return 0;
}


static void push_range_array(lua_State *L, uint32_t len, TSRange ranges[static len]) {
	lua_createtable(L, len, 0); // { range }
	for (uint32_t i = 0; i < len; i++) {
		lua_createtable(L, 0, 4); // { range }, range
		pushinteger(L, ranges[i].start_byte);
		lua_setfield(L, -2, "start_byte");
		pushinteger(L, ranges[i].end_byte);
		lua_setfield(L, -2, "end_byte");
		lua_createtable(L, 0, 2); // { range }, range, start_point
		pushinteger(L, ranges[i].start_point.row);
		lua_setfield(L, -2, "row");
		pushinteger(L, ranges[i].start_point.column);
		lua_setfield(L, -2, "column");
		lua_setfield(L, -2, "start_point"); // { range }, range
		lua_createtable(L, 0, 2);           // { range }, range, end_point
		pushinteger(L, ranges[i].end_point.row);
		lua_setfield(L, -2, "row");
		pushinteger(L, ranges[i].end_point.column);
		lua_setfield(L, -2, "column");
		lua_setfield(L, -2, "end_point"); // { range }, range
		lua_rawseti(L, -2, i + 1);        // { range }
	}
}

// @teal-export Tree.get_changed_ranges: function(old: Tree, new: Tree): {Range} [[
//    Compare an old syntax tree to a new syntax tree.
//    This would usually be called right after a set of calls to <code>Tree.edit(_s)</code> and <code>Parser.parse_{string,with}</code>
// ]]
static int tree_get_changed_ranges(lua_State *L) {
	TSTree *old = *tree_assert(L, 1);
	TSTree *new = *tree_assert(L, 2);
	uint32_t len = 0;
	TSRange *ranges = ts_tree_get_changed_ranges(old, new, &len);
	push_range_array(L, len, ranges);
	free(ranges);
	return 1;
}

// @teal-export Tree.included_ranges: function(Tree): {Range} [[
//    Returns the array of ranges used to parse the syntax tree
// ]]
static int tree_included_ranges(lua_State *L) {
	TSTree *t = *tree_assert(L, 1);
	uint32_t len = 0;
	TSRange *ranges = ts_tree_included_ranges(t, &len);
	push_range_array(L, len, ranges);
	free(ranges);
	return 1;
}

// @teal-export Tree.language: function(Tree): Language [[
//     Returns the language used to parse the tree
// ]]
static int tree_lang(lua_State *L) {
	TSTree const *t = *tree_assert(L, 1);
	language_get_by_ptr(L, ts_tree_language(t));
	return 1;
}

static int tree_gc(lua_State *L) {
	TSTree *t = *tree_assert(L, 1);
#ifdef LOG_GC
	printf("Tree %p is being garbage collected\n", (void const *)t);
#endif
	ts_tree_delete(t);
	return 0;
}

// @teal-export Tree.print_dot_graph: function(Tree, ?FILE) [[
//    Write a DOT graph to the given file. Defaults to standard error.
// ]]
static int print_dot_graph(lua_State *L) {
	TSTree const *t = *tree_assert(L, 1);
	FILE *f = testfile(L, 2);
	if (!f) f = stderr;
	fflush(f);
	int fd = fd_from_file(f);
	ts_tree_print_dot_graph(t, fd);
	return 0;
}

static const luaL_Reg tree_methods[] = {
	{"copy", tree_copy},
	{"edit", tree_edit},
	{"edit_p", tree_edit_p},
	{"edit_s", tree_edit_s},
	{"get_changed_ranges", tree_get_changed_ranges},
	{"included_ranges", tree_included_ranges},
	{"language", tree_lang},
	{"print_dot_graph", print_dot_graph},
	{"root", tree_push_root},
	{"root_with_offset", tree_push_root_with_offset},
	{NULL, NULL}};
static const luaL_Reg tree_metamethods[] = {
	{"__gc", tree_gc},
	{"__tostring", tree_to_string},
	{NULL, NULL}};

void tree_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_TREE_METATABLE_NAME, tree_metamethods, tree_methods);
}
