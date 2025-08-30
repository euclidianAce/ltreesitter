#ifndef LTREESITTER_TYPES_H
#define LTREESITTER_TYPES_H

#include <lauxlib.h>
#include <lua.h>
#include <tree_sitter/api.h>

#include <stdint.h>

#include "luautils.h"

#define def_check_assert(t, prefix, name) \
	static inline t *(prefix##_check)(lua_State *L, int idx) { return testudata(L, idx, name); } \
	static inline t *(prefix##_assert)(lua_State *L, int idx) { return luaL_checkudata(L, idx, name); }

typedef struct ltreesitter_Tree ltreesitter_Tree;

#define LTREESITTER_LANGUAGE_METATABLE_NAME "ltreesitter.Language"
#define LTREESITTER_PARSER_METATABLE_NAME "ltreesitter.Parser"
#define LTREESITTER_TREE_METATABLE_NAME "ltreesitter.Tree"
#define LTREESITTER_TREE_CURSOR_METATABLE_NAME "ltreesitter.TreeCursor"
#define LTREESITTER_NODE_METATABLE_NAME "ltreesitter.Node"
#define LTREESITTER_QUERY_METATABLE_NAME "ltreesitter.Query"
#define LTREESITTER_QUERY_CURSOR_METATABLE_NAME "ltreesitter.QueryCursor"
#define LTREESITTER_DYNLIB_METATABLE_NAME "ltreesitter.Dynlib"

// garbage collected source text for trees and queries to hold on to
typedef struct {
	uint32_t length;
	char text[];
} SourceText;
#define LTREESITTER_SOURCE_TEXT_METATABLE_NAME "ltreesitter.SourceText"

struct ltreesitter_Tree {
	TSTree *tree;
	// TODO: the source text is kept in the registry, we don't need this
	SourceText const *text_or_null_if_function_reader;
};

// TODO: TSTreeCursor
// TODO: TSLookaheadIterator

// pointer will only be valid for as long as it is on the stack as it may be garbage collected
// will push nil and return NULL on allocation failure
SourceText *source_text_push_uninitialized(lua_State *, uint32_t length);
SourceText *source_text_push(lua_State *, uint32_t, char const *);
void source_text_init_metatable(lua_State *);

def_check_assert(SourceText, source_text, LTREESITTER_SOURCE_TEXT_METATABLE_NAME)

TSPoint topoint(lua_State *L, int idx);

#endif
