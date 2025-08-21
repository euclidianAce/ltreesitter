#ifndef LTREESITTER_TYPES_H
#define LTREESITTER_TYPES_H

#include <lua.h>
#include <tree_sitter/api.h>

#include <stdint.h>

typedef struct ltreesitter_Parser ltreesitter_Parser;
typedef struct ltreesitter_Tree ltreesitter_Tree;
typedef struct ltreesitter_Query ltreesitter_Query;
typedef struct ltreesitter_QueryCursor ltreesitter_QueryCursor;

#define LTREESITTER_PARSER_METATABLE_NAME "ltreesitter.Parser"

// garbage collected source text for trees and queries to hold on to
typedef struct {
	uint32_t length;
	char text[];
} SourceText;
#define LTREESITTER_SOURCE_TEXT_METATABLE_NAME "ltreesitter.SourceText"

struct ltreesitter_Tree {
	TSTree *tree;
	SourceText const *text_or_null_if_function_reader;
};
#define LTREESITTER_TREE_METATABLE_NAME "ltreesitter.Tree"

#define LTREESITTER_TREE_CURSOR_METATABLE_NAME "ltreesitter.TreeCursor"

#define LTREESITTER_NODE_METATABLE_NAME "ltreesitter.Node"

struct ltreesitter_Query {
	TSQuery *query;

	const TSLanguage *lang;
};
#define LTREESITTER_QUERY_METATABLE_NAME "ltreesitter.Query"

struct ltreesitter_QueryCursor {
	ltreesitter_Query *query;
	TSQueryCursor *query_cursor;
};
#define LTREESITTER_QUERY_CURSOR_METATABLE_NAME "ltreesitter.QueryCursor"

// TODO: TSLookaheadIterator

// pointer will only be valid for as long as it is on the stack as it may be garbage collected
// will push nil and return NULL on allocation failure
SourceText *source_text_push_uninitialized(lua_State *, uint32_t length);
SourceText *source_text_push(lua_State *, uint32_t, const char *);
SourceText *source_text_check(lua_State *, int index);
void source_text_init_metatable(lua_State *);

#endif
