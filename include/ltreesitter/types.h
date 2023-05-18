#ifndef LTREESITTER_TYPES_H
#define LTREESITTER_TYPES_H

#include "dynamiclib.h"
#include <lua.h>
#include <tree_sitter/api.h>

typedef struct ltreesitter_Parser ltreesitter_Parser;
typedef struct ltreesitter_Tree ltreesitter_Tree;
typedef struct ltreesitter_TreeCursor ltreesitter_TreeCursor;
typedef struct ltreesitter_Node ltreesitter_Node;
typedef struct ltreesitter_Query ltreesitter_Query;
typedef struct ltreesitter_QueryCursor ltreesitter_QueryCursor;

struct ltreesitter_Parser {
	ltreesitter_Dynlib *dl;
	TSParser *parser;
};
#define LTREESITTER_PARSER_METATABLE_NAME "ltreesitter.Parser"

// garbage collected source text for trees and queries to hold on to
typedef struct {
	size_t length;
	char text[];
} ltreesitter_SourceText;
#define LTREESITTER_SOURCE_TEXT_METATABLE_NAME "ltreesitter.SourceText"

struct ltreesitter_Tree {
	TSTree *tree;

	// TODO: this should be a tagged union of a reader function and source text
	ltreesitter_SourceText const *source;
};
#define LTREESITTER_TREE_METATABLE_NAME "ltreesitter.Tree"

struct ltreesitter_TreeCursor {
	TSTreeCursor cursor;
};
#define LTREESITTER_TREE_CURSOR_METATABLE_NAME "ltreesitter.TreeCursor"

struct ltreesitter_Node {
	TSNode node;
};
#define LTREESITTER_NODE_METATABLE_NAME "ltreesitter.Node"

struct ltreesitter_Query {
	TSQuery *query;

	const TSLanguage *lang;
	// TODO: this doesn't need to be stored here
	ltreesitter_SourceText const *source;
};
#define LTREESITTER_QUERY_METATABLE_NAME "ltreesitter.Query"

struct ltreesitter_QueryCursor {
	ltreesitter_Query *query;
	TSQueryCursor *query_cursor;
};
#define LTREESITTER_QUERY_CURSOR_METATABLE_NAME "ltreesitter.QueryCursor"

// pointer will only be valid for as long as it is on the stack as it may be garbage collected
// will push nil and return NULL on allocation failure
ltreesitter_SourceText *ltreesitter_source_text_push_uninitialized(lua_State *, size_t length);
ltreesitter_SourceText *ltreesitter_source_text_push(lua_State *, size_t, const char *);
ltreesitter_SourceText *ltreesitter_check_source_text(lua_State *, int index);
void ltreesitter_create_source_text_metatable(lua_State *);

#endif
